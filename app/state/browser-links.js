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
  // Generated file routes with no file name of their own (Orbit Store 1.3.5): 1fichier's
  // a-<n>.1fichier.com/<token> and Datanodes' tunnel<n>.dlproxy.uk gateway. The payload's
  // browser_url.c holds the same rules.
  function opaqueFileRoute(host, path) {
    if (/^a-[a-z0-9-]+\.1fichier\.com$/.test(host)) return /^\/[A-Za-z0-9]{10,256}$/.test(path);
    if (/^tunnel\d+\.dlproxy\.uk$/.test(host)) return /^\/[^/?]{8}\/[^/?]{32,1024}\?sig=[^&]+$/.test(path) && /^[!-~]+$/.test(path) && !path.includes("\\");
    return false;
  }
  // A host's own page is never its file, even when it names the archive: LinkVault lists the
  // mirrors' pages and each page links itself, so a page address would end the capture early.
  function hostPage(host, path) {
    const bare = host.replace(/^www\./, "");
    if (bare === "link-vault.org" || host.endsWith(".link-vault.org") || bare === "mediafire.com") return true;
    if (bare === "akirabox.com" || bare === "akirabox.to") return !path.startsWith("/download/");
    if (bare === "vikingfile.com" || bare === "vik1ngfile.site") return path.startsWith("/f/");
    if (["datanodes.to", "datanodes.co", "filekeeper.net", "datavaults.co"].includes(bare)) return /^\/[a-z0-9]{12}(?:[/?]|$)/.test(path);
    return false;
  }
  function matchingLink(url, anchor) {
    try {
      const parsed = webURL(url);
      const host = parsed.host.toLowerCase();
      if (!url.startsWith("https://") || parsed.origin !== `https://${parsed.host}` || url.includes("#") ||
          !host.includes("."))
        return false;
      if (opaqueFileRoute(host, parsed.path)) return true;
      if (hostPage(host, parsed.path) || !FILE_EXTENSIONS.test(url)) return false;
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
