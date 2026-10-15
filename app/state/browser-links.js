// ../pub-game-store/native/state/browser-links.js

import { webURL } from './download-sources.js';

  // A host whose check only the console browser can pass: the app arms a capture on the page's
  // own URL and takes the file link out of the browser once it appears. The link is the game's
  // own file when its name carries the title id, or when it sits on a host known to serve files.
  var LINK_PREFIX = "https://";
  var FILE_HOSTS = [
    "akirabox.com", "akirabox.to", "buzzheavier.com", "datanodes.co", "datanodes.to",
    "datavaults.co", "fileditchfiles.st", "filekeeper.net", "mediafire.com", "rootz.so",
    "vikingfile.com", "vik1ngfile.site", "bhs.io.cloud.ovh.net", "r2.cloudflarestorage.com"
  ];
  var FILE_EXTENSIONS = /\.(?:7z|rar|zip|pkg|exfat|ffpkg|ffpfsc|ffpfs|bin|iso|img)(?=[?#&/]|$)/i;
  var CATALOG_SLACK = 2 ** 30;
  var plainLink = (url) => url.replace(/&amp;/gi, "&").replace(/&#0*38;/g, "&");
  function captureAnchor(titleId) {
    const anchor = String(titleId || "").toUpperCase().replace(/[^A-Z0-9]/g, "");
    return anchor.length >= 4 && anchor.length <= 32 ? anchor : "";
  }
  function knownFileHost(host) {
    return FILE_HOSTS.some((domain) => host === domain || host.endsWith(`.${domain}`));
  }
  function matchingLink(url, anchor) {
    try {
      const parsed = webURL(url);
      if (!url.startsWith("https://") || parsed.origin !== `https://${parsed.host}` || url.includes("#") ||
          !parsed.host.includes(".") || !FILE_EXTENSIONS.test(url))
        return false;
      const name = parsed.path.split("?")[0].split("/").pop() || "";
      if (name && FILE_EXTENSIONS.test(name) && anchor && name.toLowerCase().includes(anchor.toLowerCase())) return true;
      return knownFileHost(parsed.host);
    } catch {
      return false;
    }
  }
  async function rangeProbe(http, url, signal) {
    for (let redirects = 0; redirects <= 5; redirects++) {
      const response = await http.request(url, {
        method: "GET",
        headers: { Range: "bytes=0-0" },
        signal,
        maxBytes: 8192,
        followRedirects: false
      });
      if (![301, 302, 303, 307, 308].includes(response.status)) return response;
      const location2 = response.headers?.location;
      if (!location2 || redirects === 5) throw new Error("Captured file exceeded its redirect limit.");
      url = location2.startsWith("/") && !location2.startsWith("//") ? webURL(url).origin + location2 : location2;
      if (!url.startsWith("https://")) throw new Error("Captured file redirected to an unsupported download host.");
    }
  }
  async function validateCapturedLink(http, url, metadata, signal) {
    const link = plainLink(url);
    if (!matchingLink(link, metadata.anchor))
      throw new Error("Captured link does not name this game's file. Enter the final file link.");
    const response = await rangeProbe(http, link, signal);
    const ranged = response.status === 206;
    const range = /^bytes 0-0\/(\d+)$/i.exec(response.headers?.["content-range"] || "");
    const length = Number(response.headers?.["content-length"]);
    const bytes = Number(range?.[1]) || length;
    if (!response.ok || !Number.isInteger(length) || length <= 0 || ranged && length !== 1 ||
        /html|json|xml|text\//i.test(response.headers?.["content-type"] || "") ||
        ranged && (await response.text()).length !== 1 || !Number.isSafeInteger(bytes) || bytes <= 0)
      throw new Error("Captured file failed validation or expired. Reopen the page and retry.");
    if (metadata.sizeBytes && bytes + CATALOG_SLACK < metadata.sizeBytes)
      throw new Error("Captured file is smaller than the selected release. Reopen the page and retry.");
    return { url: link, expectedBytes: bytes };
  }

export { LINK_PREFIX, captureAnchor, matchingLink, validateCapturedLink };
