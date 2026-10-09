// Flash planning is separate from the page so preservation failures are testable.
export function binaryString(bytes) {
  let result = "";
  for (let offset = 0; offset < bytes.length; offset += 8192) {
    result += String.fromCharCode(...bytes.subarray(offset, offset + 8192));
  }
  return result;
}

export function parsePartitions(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const partitions = [];
  for (let offset = 0; offset + 32 <= bytes.length; offset += 32) {
    const magic = view.getUint16(offset, true);
    if (magic === 0xffff || magic === 0xebeb) break;
    if (magic !== 0x50aa || view.getUint32(offset + 28, true) !== 0) {
      throw new Error("The installed partition table is unsupported. No firmware was written.");
    }
    const label = bytes.subarray(offset + 12, offset + 28);
    const end = label.indexOf(0);
    partitions.push({
      name: new TextDecoder().decode(end < 0 ? label : label.subarray(0, end)),
      type: bytes[offset + 2], subtype: bytes[offset + 3],
      offset: view.getUint32(offset + 4, true), size: view.getUint32(offset + 8, true),
    });
  }
  return partitions;
}

export function matchingLayout(installed, expected) {
  const keys = ["name", "type", "subtype", "offset", "size"];
  return installed.length === expected.length && installed.every((part, index) =>
    keys.every(key => part[key] === expected[index][key]));
}

// ESP-IDF's OTA sequence CRC uses crc32_le(UINT32_MAX, sequence, 4).
export function otaCRC(bytes) {
  let crc = 0;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

export function activeSlot(ota, partitions) {
  if (ota.length !== 0x2000) throw new Error("Could not read the firmware selection records.");
  const view = new DataView(ota.buffer, ota.byteOffset, ota.byteLength);
  const records = [];
  for (const offset of [0, 0x1000]) {
    const sequence = view.getUint32(offset, true);
    const state = view.getUint32(offset + 24, true);
    const crc = view.getUint32(offset + 28, true);
    if (!sequence || sequence === 0xffffffff || state === 3 || state === 4 ||
        crc !== otaCRC(ota.subarray(offset, offset + 4))) continue;
    if (state !== 2 && state !== 0xffffffff) {
      throw new Error("The previous update is awaiting boot confirmation. Start the device normally before updating again.");
    }
    records.push(sequence);
  }
  if (!records.length) {
    throw new Error("Cannot safely identify the existing firmware slot. No firmware was written. Use Erase data only if you want a fresh installation.");
  }
  const subtype = 0x10 + (Math.max(...records) - 1) % 2;
  const slot = partitions.find(part => part.type === 0 && part.subtype === subtype);
  if (!slot) throw new Error("The selected firmware slot is missing.");
  return slot;
}

// esptool-js 0.7.0 readFlash leaves the stub's final MD5 packet unread.
// Consume and verify it so the next command cannot mistake it for a response.
export async function readVerified(loader, offset, size, hashMD5) {
  const bytes = await loader.readFlash(offset, size);
  const digest = await loader.transport.read(5000);
  const hex = digest instanceof Uint8Array ? Array.from(digest, byte => byte.toString(16).padStart(2, "0")).join("") : "";
  if (bytes.length !== size || digest?.length !== 16 || hex !== hashMD5(binaryString(bytes))) {
    throw new Error("USB read verification failed. No firmware was written.");
  }
  return bytes;
}

export async function flashFirmware({ loader, build, image, eraseData, hashMD5, status = () => {}, progress = () => {} }) {
  if (loader.chip?.CHIP_NAME !== "ESP32-S3") throw new Error("This firmware requires an ESP32-S3.");
  if (build.chip !== "ESP32-S3" || build.flash_bytes !== 0x800000 ||
      await loader.detectFlashSize() !== "8MB") {
    throw new Error("This firmware requires the original XIAO ESP32-S3 with 8 MB flash.");
  }
  const expected = build.partitions;
  const app0 = expected.find(part => part.name === "app0");
  const app1 = expected.find(part => part.name === "app1");
  const ota = expected.find(part => part.name === "otadata");
  if (app0?.offset !== 0x10000 || app0.size !== 0x330000 || app1?.offset !== 0x340000 ||
      app1.size !== 0x330000 || ota?.offset !== 0xe000 || ota.size !== 0x2000) {
    throw new Error("Unexpected build partition layout.");
  }
  if (!image.length || image[0] !== 0xe9 || image.length % 4 !== 0) {
    throw new Error("Invalid firmware image.");
  }

  let address = 0;
  const preserved = [];
  if (!eraseData) {
    status("Checking the existing installation…");
    const table = await readVerified(loader, 0x8000, 0x1000, hashMD5);
    const installed = parsePartitions(table);
    if (!matchingLayout(installed, expected)) {
      throw new Error("The existing storage layout does not match. No firmware was written. Use Erase data only for a fresh installation.");
    }
    const selection = await readVerified(loader, ota.offset, ota.size, hashMD5);
    const slot = activeSlot(selection, installed);
    if (image.length > slot.size) throw new Error("Firmware is too large for the existing slot.");
    address = slot.offset;
    // Validate sector erasure cannot reach any non-application partition.
    const end = address + Math.ceil(image.length / 4096) * 4096;
    if (end > slot.offset + slot.size) throw new Error("Firmware would overlap saved data.");
    status("Checking saved data before updating…");
    for (const part of [{ name: "partition table", offset: 0x8000, size: 0x1000 }, ...installed.filter(part => part.type === 1)]) {
      preserved.push({ ...part, digest: await loader.flashMd5sum(part.offset, part.size) });
    }
  } else if (image.length > app0.offset + app0.size) {
    throw new Error("The full installation image is too large.");
  }

  status(eraseData ? "Erasing data and installing…" : "Installing firmware; keeping saved data…");
  await loader.writeFlash({
    fileArray: [{ data: binaryString(image), address }],
    flashSize: "keep", flashMode: "keep", flashFreq: "keep", compress: true,
    eraseAll: eraseData, calculateMD5Hash: hashMD5,
    reportProgress: (_file, written, total) => progress(Math.floor(100 * written / total)),
  });
  // Verify explicitly too: never report success if a backend skipped its callback.
  if (await loader.flashMd5sum(address, image.length) !== hashMD5(binaryString(image))) {
    throw new Error("Firmware verification failed. Reconnect in BOOT mode and retry.");
  }
  if (!eraseData) {
    status("Verifying saved data was preserved…");
    for (const part of preserved) {
      if (await loader.flashMd5sum(part.offset, part.size) !== part.digest) {
        throw new Error("Saved-data verification failed. Keep your backup and do not erase the device.");
      }
    }
  }
  return { address, erased: eraseData, preserved: !eraseData };
}
