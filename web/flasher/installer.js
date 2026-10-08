const descriptions = {
  repeater: "Extend the mesh. Optional battery sensing, SHT4x readings and manual GPIO outputs.",
  room: "Host a shared chatroom, with the same peripherals plus memory and storage diagnostics. Message cache remains 32 posts in RAM.",
  ble: "Connect the MeshCore phone app over Bluetooth. Includes configurable battery sensing.",
  usb: "Connect a computer client over USB. Includes configurable battery sensing.",
  serial: "Connect a client through UART on D6/D7. These pins are reserved for the serial connection.",
  wifi: "Connect a client over Wi-Fi. This standard target requires its Wi-Fi settings to be configured before use; see the build guide.",
};

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
    const installer = document.createElement("esp-web-install-button");
    installer.setAttribute("manifest", `manifest-${build.slug}.json`);
    const activate = document.createElement("button");
    activate.slot = "activate";
    activate.textContent = "Fresh install via USB";
    installer.append(activate);
    const unsupported = document.createElement("span");
    unsupported.slot = "unsupported";
    unsupported.textContent = "Use desktop Chrome or Edge for USB installation.";
    installer.append(unsupported);
    const downloads = document.createElement("div");
    downloads.className = "downloads";
    for (const [name, label] of [["firmware.bin", "Application image"], ["firmware-merged.bin", "Full install image"], ["build.json", "Checksums & build details"]]) {
      const link = document.createElement("a");
      link.href = `firmware/${build.slug}/${name}`;
      link.textContent = label;
      if (name.endsWith(".bin")) link.download = `${build.target}-${build.version}-${name}`;
      downloads.append(link);
    }
    card.append(heading, description, installer, downloads);
    document.getElementById("cards").append(card);
  }
} catch (error) {
  document.getElementById("build-status").textContent = "Firmware is unavailable. Check the build history on GitHub and try again later.";
  console.error(error);
}
