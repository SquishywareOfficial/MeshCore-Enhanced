import { flashFirmware } from "./flash-core.js";

const descriptions = {
  repeater: "Extend the mesh. Optional battery sensing, SHT4x readings and manual GPIO outputs.",
  room: "Host a shared chatroom, with the same peripherals plus memory and storage diagnostics. Message cache remains 32 posts in RAM.",
  ble: "Connect the MeshCore phone app over Bluetooth. Includes configurable battery sensing.",
  usb: "Connect a computer client over USB. Includes configurable battery sensing.",
  serial: "Connect a client through UART on D6/D7. These pins are reserved for the serial connection.",
  wifi: "Connect a client over Wi-Fi. This standard target requires its Wi-Fi settings to be configured before use; see the build guide.",
};

let busy = false;
window.addEventListener("beforeunload", event => {
  if (busy) { event.preventDefault(); event.returnValue = ""; }
});

async function downloadImage(build, eraseData) {
  const name = eraseData ? "firmware-merged.bin" : "firmware.bin";
  const response = await fetch(`${build.base_url}/${name}`, { cache: "no-store" });
  if (!response.ok) throw new Error("Firmware download failed. Please try again.");
  const image = new Uint8Array(await response.arrayBuffer());
  const digest = await crypto.subtle.digest("SHA-256", image);
  const hex = Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, "0")).join("");
  if (hex !== build.sha256[name]) throw new Error("Firmware download verification failed. Nothing was written.");
  return image;
}

function setBusy(value) {
  busy = value;
  for (const control of document.querySelectorAll(".install-button, .erase-data")) control.disabled = value;
}

async function install(build, eraseData, message, meter) {
  if (busy) return;
  // Request the port during the click gesture, before downloads or module loading.
  setBusy(true);
  let transport;
  let loader;
  let wroteFirmware = false;
  message.classList.remove("error");
  const status = text => { message.textContent = text; };
  try {
    status("Select the device’s USB port…");
    const port = await navigator.serial.requestPort();
    status("Downloading and verifying firmware…");
    const [image, tools, md5Module] = await Promise.all([
      downloadImage(build, eraseData),
      import("https://esm.sh/esptool-js@0.7.0?bundle"),
      import("https://esm.sh/spark-md5@3.0.2?bundle"),
    ]);
    const hashMD5 = data => md5Module.default.hashBinary(data);
    transport = new tools.Transport(port, false);
    // Keep low-level logs in the browser console, rather than the installation UI.
    const terminal = { clean() {}, writeLine(text) { console.debug(text); }, write(text) { console.debug(text); } };
    loader = new tools.ESPLoader({ transport, baudrate: 115200, romBaudrate: 115200, terminal });
    status("Connecting… If needed, reconnect while holding BOOT and try again.");
    await loader.main();
    await flashFirmware({ loader, build, image, eraseData, hashMD5, status,
      progress(percent) { wroteFirmware = true; meter.hidden = false; meter.value = percent; },
    });
    wroteFirmware = true;
    // The upstream flasher pulses RTS as well as calling after('hard_reset').
    try {
      await loader.after("hard_reset");
      await transport.setRTS(true);
      await new Promise(resolve => setTimeout(resolve, 100));
      await transport.setRTS(false);
      status(eraseData
        ? "Installed. Data was erased; configure your node before using it."
        : "Updated. Saved settings, identity and stored data were verified unchanged. Restart cleared any messages held only in RAM.");
    } catch (error) {
      console.error(error);
      status("Firmware was installed and verified. Reconnect USB without holding BOOT to start it.");
    }
  } catch (error) {
    if (error.name === "NotFoundError") status("Installation cancelled; nothing was written.");
    else {
      message.classList.add("error");
      status(`${error.message}${wroteFirmware ? " Reconnect in BOOT mode to retry." : ""}`);
      console.error(error);
    }
  } finally {
    if (transport) {
      try { await transport.disconnect(); } catch (error) { console.debug("USB release", error); }
    }
    meter.hidden = true;
    setBusy(false);
  }
}

try {
  const response = await fetch("builds.json", { cache: "no-store" });
  if (!response.ok) throw new Error("Build metadata unavailable");
  const catalog = await response.json();
  const buildStatus = document.getElementById("build-status");
  buildStatus.textContent = `Build ${catalog.builds[0].version} · ${new Date(catalog.built_at).toLocaleDateString()}`;
  const commit = document.createElement("a");
  commit.href = `https://github.com/SquishywareOfficial/MeshCore-Enhanced/commit/${catalog.commit}`;
  commit.textContent = " View source";
  buildStatus.append(commit);
  for (const build of catalog.builds) {
    const card = document.createElement("article");
    card.className = "card";
    const heading = document.createElement("h3");
    heading.textContent = build.title;
    const description = document.createElement("p");
    description.textContent = descriptions[build.slug];
    const eraseLabel = document.createElement("label");
    eraseLabel.className = "erase-option";
    const erase = document.createElement("input");
    erase.type = "checkbox";
    erase.className = "erase-data";
    eraseLabel.append(erase, " Erase data");
    const help = document.createElement("p");
    help.className = "erase-help";
    help.id = `erase-help-${build.slug}`;
    help.textContent = "Unchecked: keep settings and identity. Checked: erase all data for a fresh installation.";
    erase.setAttribute("aria-describedby", help.id);
    const activate = document.createElement("button");
    activate.className = "install-button";
    activate.textContent = "Install via USB";
    const message = document.createElement("p");
    message.className = "install-status";
    message.setAttribute("role", "status");
    const meter = document.createElement("progress");
    meter.max = 100;
    meter.hidden = true;
    meter.setAttribute("aria-label", "Firmware installation progress");
    if (!navigator.serial || !window.isSecureContext) {
      activate.disabled = true;
      erase.disabled = true;
      message.textContent = "Use this HTTPS page in desktop Chrome or Edge for USB installation.";
    }
    activate.addEventListener("click", () => install(build, erase.checked, message, meter));
    const downloads = document.createElement("div");
    downloads.className = "downloads";
    for (const [name, label] of [["firmware.bin", "Application image"], ["firmware-merged.bin", "Full install image"], ["build.json", "Checksums & build details"]]) {
      const link = document.createElement("a");
      link.href = `${build.base_url}/${name}`;
      link.textContent = label;
      if (name.endsWith(".bin")) link.download = `${build.target}-${build.version}-${name}`;
      downloads.append(link);
    }
    card.append(heading, description, eraseLabel, help, activate, meter, message, downloads);
    document.getElementById("cards").append(card);
  }
} catch (error) {
  document.getElementById("build-status").textContent = "Firmware is unavailable. Check the build history on GitHub and try again later.";
  console.error(error);
}
