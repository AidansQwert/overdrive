import { DeviceInfo } from '@ps5-react/core';

  // Fake PKGs install only up to this firmware; above it the system refuses them, after the whole
  // download (issues #6 and #17). Version strings read like "13.200.001": major and two minor digits.
  var LAST_PKG_FIRMWARE = [11, 60];
  var PKG_FIRMWARE_ERROR = "Fake PKGs install only on firmware 11.60 or older. Choose another source.";
  var PKG_FORMATS = /* @__PURE__ */ new Set(["pkg", "fpkg"]);
  function firmwareVersion() {
    let text;
    try {
      text = DeviceInfo.get().firmware;
    } catch {
      return null;
    }
    const match = /(\d+)\.(\d{2})/.exec(text || "");
    return match ? [Number(match[1]), Number(match[2])] : null;
  }
  // Unknown firmware (the desktop preview, an old runtime) keeps PKGs offered, as before.
  function fakePackagesInstall() {
    const version = firmwareVersion();
    if (!version) return true;
    const [major, minor] = version, [lastMajor, lastMinor] = LAST_PKG_FIRMWARE;
    return major < lastMajor || major === lastMajor && minor <= lastMinor;
  }
  function withoutUninstallablePackages(releases) {
    if (fakePackagesInstall()) return releases;
    return releases.map((release) => PKG_FORMATS.has(release.format) && !release.unavailableReason ? { ...release, unavailableReason: PKG_FIRMWARE_ERROR } : release);
  }

export { PKG_FIRMWARE_ERROR, fakePackagesInstall, withoutUninstallablePackages };
