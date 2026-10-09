import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { flashFirmware, otaCRC, activeSlot, binaryString } from "../../web/flasher/flash-core.js";

const md5 = text => createHash("md5").update(text, "latin1").digest("hex");
const parts = [
  { name: "nvs", type: 1, subtype: 2, offset: 0x9000, size: 0x5000 },
  { name: "otadata", type: 1, subtype: 0, offset: 0xe000, size: 0x2000 },
  { name: "app0", type: 0, subtype: 0x10, offset: 0x10000, size: 0x330000 },
  { name: "app1", type: 0, subtype: 0x11, offset: 0x340000, size: 0x330000 },
  { name: "spiffs", type: 1, subtype: 0x82, offset: 0x670000, size: 0x180000 },
  { name: "coredump", type: 1, subtype: 3, offset: 0x7f0000, size: 0x10000 },
];

function fixture(sequence = 1) {
  const memory = new Uint8Array(0x800000).fill(0xff);
  const view = new DataView(memory.buffer);
  parts.forEach((part, index) => {
    const offset = 0x8000 + index * 32;
    memory.fill(0, offset, offset + 32);
    view.setUint16(offset, 0x50aa, true);
    memory[offset + 2] = part.type;
    memory[offset + 3] = part.subtype;
    view.setUint32(offset + 4, part.offset, true);
    view.setUint32(offset + 8, part.size, true);
    memory.set(new TextEncoder().encode(part.name), offset + 12);
    if (part.type === 1 && part.name !== "otadata") memory.fill(index + 17, part.offset, part.offset + part.size);
  });
  function otaRecord(index, seq, state = 0xffffffff) {
    const offset = 0xe000 + index * 0x1000;
    view.setUint32(offset, seq, true);
    view.setUint32(offset + 24, state, true);
    view.setUint32(offset + 28, otaCRC(memory.subarray(offset, offset + 4)), true);
  }
  otaRecord(0, sequence);
  const image = new Uint8Array(4096).fill(0x77);
  image[0] = 0xe9;
  const build = { chip: "ESP32-S3", flash_bytes: 0x800000, partitions: structuredClone(parts) };
  const writes = [];
  const digest = (offset, size) => md5(binaryString(memory.subarray(offset, offset + size)));
  let trailer;
  const loader = {
    chip: { CHIP_NAME: "ESP32-S3" },
    detectFlashSize: async () => "8MB",
    transport: { read: async () => trailer },
    async readFlash(offset, size) {
      const bytes = memory.slice(offset, offset + size);
      trailer = new Uint8Array(Buffer.from(md5(binaryString(bytes)), "hex"));
      return bytes;
    },
    flashMd5sum: async (offset, size) => digest(offset, size),
    async writeFlash(options) {
      writes.push(options);
      if (options.eraseAll) memory.fill(0xff);
      for (const file of options.fileArray) {
        // Model actual sector erasure, not just overwriting image bytes.
        const end = file.address + Math.ceil(file.data.length / 4096) * 4096;
        memory.fill(0xff, file.address, end);
        memory.set(Uint8Array.from(file.data, char => char.charCodeAt(0)), file.address);
      }
      options.reportProgress(0, 1, 1);
    },
  };
  const run = (eraseData = false) => flashFirmware({ loader, build, image, eraseData, hashMD5: md5 });
  return { memory, view, loader, writes, run, otaRecord, image, build, digest };
}

test("OTA CRC matches the ESP-IDF/Python zlib fixture for sequence 1", () => {
  assert.equal(otaCRC(new Uint8Array([1, 0, 0, 0])), 0x4743989a);
});

for (const [sequence, slot] of [[1, "app0"], [2, "app1"]]) {
  test(`preserving update with ${slot} active writes only that application`, async () => {
    const f = fixture(sequence);
    const before = f.memory.slice();
    const result = await f.run();
    const part = parts.find(part => part.name === slot);
    assert.equal(result.address, part.offset);
    assert.equal(result.preserved, true);
    assert.equal(f.writes.length, 1);
    assert.equal(f.writes[0].eraseAll, false);
    assert.equal(f.writes[0].fileArray.length, 1);
    assert.deepEqual(f.memory.slice(0, part.offset), before.slice(0, part.offset));
    assert.deepEqual(f.memory.slice(part.offset + f.image.length), before.slice(part.offset + f.image.length));
    assert.deepEqual(f.memory.slice(part.offset, part.offset + f.image.length), f.image);
  });
}

test("newest valid OTA record determines the slot", async () => {
  const f = fixture(1);
  f.otaRecord(1, 2, 2);
  assert.equal((await f.run()).address, 0x340000);
});

test("a corrupt newer OTA record falls back to the valid older record", async () => {
  const f = fixture(1);
  f.otaRecord(1, 2);
  f.memory[0xf01c] ^= 1;
  assert.equal((await f.run()).address, 0x10000);
});

test("invalid and aborted OTA entries are not selected", () => {
  const f = fixture(1);
  for (const state of [3, 4]) {
    f.otaRecord(1, 2, state);
    assert.equal(activeSlot(f.memory.subarray(0xe000, 0x10000), parts).name, "app0");
  }
});

test("ambiguous or pending OTA selection cannot write or erase anything", async () => {
  for (const kind of ["blank", "corrupt", "pending"]) {
    const f = fixture();
    if (kind === "blank") f.memory.fill(0xff, 0xe000, 0x10000);
    if (kind === "corrupt") f.memory[0xe01c] ^= 1;
    if (kind === "pending") f.otaRecord(0, 1, 1);
    await assert.rejects(f.run(), /slot|boot confirmation/);
    assert.equal(f.writes.length, 0);
  }
});

test("incompatible partition layout refuses update without an automatic erase", async () => {
  const f = fixture();
  f.view.setUint32(0x8000 + 4 * 32 + 8, 0x100000, true);
  await assert.rejects(f.run(), /layout does not match/);
  assert.equal(f.writes.length, 0);
});

test("damaged USB reads are rejected before writing", async () => {
  const f = fixture();
  f.loader.transport.read = async () => new Uint8Array(16);
  await assert.rejects(f.run(), /USB read verification/);
  assert.equal(f.writes.length, 0);
});

test("wrong chip and wrong flash size cannot even erase", async () => {
  for (const wrongChip of [false, true]) {
    const f = fixture();
    if (wrongChip) f.loader.chip.CHIP_NAME = "ESP32";
    else f.loader.detectFlashSize = async () => "4MB";
    await assert.rejects(f.run(true), /ESP32-S3/);
    assert.equal(f.writes.length, 0);
  }
});

test("explicit erase installs the full image at zero and wipes saved data", async () => {
  const f = fixture();
  // A new device does not need a pre-existing compatible partition table.
  f.memory.fill(0xff, 0x8000, 0x10000);
  const result = await f.run(true);
  assert.equal(result.address, 0);
  assert.equal(result.erased, true);
  assert.equal(f.writes[0].eraseAll, true);
  assert.equal(f.memory[0x670000], 0xff);
});

test("oversized and malformed images cannot write", async () => {
  for (const malformed of [false, true]) {
    const f = fixture();
    if (malformed) f.image[0] = 0;
    else f.image = new Uint8Array(0x330004).fill(0xe9);
    await assert.rejects(flashFirmware({ loader: f.loader, build: f.build, image: f.image, eraseData: false, hashMD5: md5 }), /Invalid|too large/);
    assert.equal(f.writes.length, 0);
  }
});

test("firmware verification failure never reports success", async () => {
  const f = fixture();
  const write = f.loader.writeFlash;
  f.loader.writeFlash = async options => { await write(options); f.memory[0x10040] ^= 1; };
  await assert.rejects(f.run(), /Firmware verification failed/);
});

test("saved-data corruption is detected after writing", async () => {
  const f = fixture();
  const write = f.loader.writeFlash;
  f.loader.writeFlash = async options => { await write(options); f.memory[0x670000] ^= 1; };
  await assert.rejects(f.run(), /Saved-data verification failed/);
});
