// Adapted from Orbit Store 1.0.1 by saawant12 (https://github.com/saawant12/orbit-store-ps5).
// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only browser capture; original corresponding source is recorded in README.md.
#include "browser_match.h"
#include <string.h>
static int hex(unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
// Vikingfile moved its pages to vik1ngfile.site; its download links may follow.
const char *const browser_capture_prefixes[] = {"https://vikingfile.com/d/", "https://vik1ngfile.site/d/"};
const char *browser_capture_prefix(const char *url) {
  for (size_t i = 0; i < sizeof browser_capture_prefixes / sizeof *browser_capture_prefixes; i++)
    if (!strncmp(url, browser_capture_prefixes[i], strlen(browser_capture_prefixes[i]))) return browser_capture_prefixes[i];
  return NULL;
}
// Hosts whose check only a console browser can pass: Akirabox's Turnstile, Datanodes' and
// Filekeeper's Cloudflare pass, LinkVault's mirror pages. Their file link becomes a real address
// only after the human acts, so it is taken from the browser once the file starts. LinkVault
// itself is not here: a vault page lists mirrors, it never serves the file.
static const char *const browser_capture_hosts[] = {
  "akirabox.com", "akirabox.to", "buzzheavier.com", "datanodes.co", "datanodes.to",
  "datavaults.co", "fileditchfiles.st", "filekeeper.net", "mediafire.com", "rootz.so",
  "vikingfile.com", "vik1ngfile.site", "bhs.io.cloud.ovh.net", "r2.cloudflarestorage.com"};
// Archives and console images only: a page's .php/.html/.js can never be the download itself.
static const char *const browser_capture_extensions[] = {
  "7z", "rar", "zip", "pkg", "exfat", "ffpkg", "ffpfsc", "ffpfs", "bin", "iso", "img"};
static unsigned char folded(unsigned char c) {
  return c >= 'A' && c <= 'Z' ? (unsigned char)(c - 'A' + 'a') : c;
}
static bool token(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static bool contains(const char *text, const char *needle) {
  const size_t length = strlen(text), wanted = strlen(needle);
  if (!wanted) return false;
  for (size_t i = 0; i + wanted <= length; i++) {
    size_t j = 0;
    while (j < wanted && folded((unsigned char)text[i + j]) == folded((unsigned char)needle[j])) j++;
    if (j == wanted) return true;
  }
  return false;
}
// A file token anywhere in the URL: ".ext" of a known kind, then a delimiter or the end.
static bool browser_capture_file_token(const char *url) {
  for (const char *dot = strchr(url, '.'); dot; dot = strchr(dot + 1, '.')) {
    const char *name = dot + 1;
    size_t length = 0;
    while (length < 8 && token((unsigned char)name[length])) length++;
    const unsigned char after = (unsigned char)name[length];
    if (!length || (after && after != '?' && after != '&' && after != '#' && after != '/')) continue;
    for (size_t i = 0; i < sizeof browser_capture_extensions / sizeof *browser_capture_extensions; i++) {
      const char *known = browser_capture_extensions[i];
      if (strlen(known) != length) continue;
      size_t j = 0;
      while (j < length && folded((unsigned char)name[j]) == folded((unsigned char)known[j])) j++;
      if (j == length) return true;
    }
  }
  return false;
}
static bool file_host(const char *host, size_t length) {
  for (size_t i = 0; i < sizeof browser_capture_hosts / sizeof *browser_capture_hosts; i++) {
    const char *known = browser_capture_hosts[i];
    const size_t wanted = strlen(known);
    if (length < wanted) continue;
    if (length > wanted && host[length - wanted - 1] != '.') continue;
    size_t j = 0;
    while (j < wanted && folded((unsigned char)host[length - wanted + j]) == folded((unsigned char)known[j])) j++;
    if (j == wanted) return true;
  }
  return false;
}
// Anchor mode: the app names the game's title id and the payload accepts the file link that
// carries it, or, on a known host, a file link that names its archive freely (keeping the id
// nowhere). The name is read from the URL's own last path segment: an advertisement that hides
// the real address inside its query is not the file. Only the app's range probe decides whether
// the captured URL really is the file.
#define BROWSER_CAPTURE_NAME_MAX 320
bool browser_capture_anchor_url(const char *anchor, const char *url) {
  if (!anchor || !*anchor || !url || strncmp(url, "https://", 8)) return false;
  const char *host = url + 8;
  const char *slash = strchr(host, '/');
  if (!slash || slash == host) return false;
  const size_t length = (size_t)(slash - host);
  if (length > 256 || !memchr(host, '.', length)) return false;
  if (strchr(url, '#') || !browser_capture_file_token(url)) return false;
  /* The file's own name: the path's last segment, without the query. An advertisement that keeps
   * the real address inside its query has no file name of its own. */
  const char *const path = slash;
  const char *const query = strchr(path, '?');
  const char *const end = query ? query : url + strlen(url);
  const char *tail = path;
  for (const char *at = path; at < end; at++)
    if (*at == '/') tail = at + 1;
  char name[BROWSER_CAPTURE_NAME_MAX];
  size_t named = 0;
  while (tail + named < end && named + 1 < sizeof name) {
    name[named] = tail[named];
    named++;
  }
  name[named] = 0;
  if (named && browser_capture_file_token(name) && contains(name, anchor)) return true;
  return file_host(host, length) && browser_capture_file_token(url);
}
// An empty filename accepts any safe one: link pages don't tell which file they lead to.
bool browser_capture_url(const char *filename, const char *url) {
  if (!filename || !url) return false;
  const char *prefix = browser_capture_prefix(url);
  if (!prefix) return false;
  const unsigned char *p = (const void *)(url + strlen(prefix));
  if (strlen((const char *)p) < 12 || p[10] != '/') return false;
  for (size_t i = 0; i < 10; i++)
    if (!((p[i] >= 'a' && p[i] <= 'z') || (p[i] >= 'A' && p[i] <= 'Z') || (p[i] >= '0' && p[i] <= '9'))) return false;
  const unsigned char *expected = (const void *)filename;
  const bool any = !*filename;
  if (any && !p[11]) return false;
  for (p += 11; *p; p++) {
    unsigned char c = *p;
    if (c == '%') {
      if (!p[1] || !p[2] || hex(p[1]) < 0 || hex(p[2]) < 0) return false;
      c = (unsigned char)(hex(p[1]) * 16 + hex(p[2])); p += 2;
    } else if (c <= 32 || c >= 127 || c == '?' || c == '#' || c == '/' || c == '\\') return false;
    if (any) {
      if (c < 32 || c == 127 || c == '/' || c == '\\') return false;
    } else if (!*expected || c != *expected++) return false;
  }
  return !*expected;
}
