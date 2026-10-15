#include "browser_match.h"
#include "browser_scan.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct { unsigned char *bytes; size_t length; int hole, cancelled; } Memory;
static bool active(void *context) { return !((Memory *)context)->cancelled; }
static int read_memory(void *context, uint64_t at, void *out, size_t length) {
  Memory *memory = context;
  if (memory->hole && at >= PROBE_CHUNK && at < 2 * PROBE_CHUNK) return -1;
  assert(at + length <= memory->length);
  memcpy(out, memory->bytes + at, length); return 0;
}
static ProbeResult scan(Memory *memory, BrowserMatch *match, uint64_t budget) {
  ProbeReader reader = {.context = memory, .read = read_memory, .active = active,
    .match = browser_match, .match_context = match, .remaining = budget};
  return probe_scan_region(&reader, 0, memory->length, "https://vikingfile.com/d/");
}
int main(void) {
  const char *url = "https://vikingfile.com/d/Ab01234567/Game%20DLC.pkg";
  BrowserMatch match = {.filename = "Game DLC.pkg"};
  assert(browser_capture_url(match.filename, url));
  assert(!browser_capture_url("Other.pkg", url));
  assert(browser_capture_url("", url));
  assert(browser_capture_url("Game DLC.pkg", "https://vik1ngfile.site/d/Ab01234567/Game%20DLC.pkg"));
  assert(!browser_capture_url("Game DLC.pkg", "https://vik1ngfile.site.evil.test/d/Ab01234567/Game%20DLC.pkg"));
  assert(!browser_capture_url("", "https://vikingfile.com/d/Ab01234567/"));
  assert(!browser_capture_url("", "https://vikingfile.com/d/Ab01234567/a%2Fb.rar"));
  assert(!browser_capture_url("", "https://vikingfile.com/d/Ab01234567/a%5Cb.rar"));
  assert(!browser_capture_url("", "https://vikingfile.com/d/Ab01234567/a%0Ab.rar"));
  assert(!browser_capture_url(match.filename, "https://vikingfile.com.evil.test/d/Ab01234567/Game%20DLC.pkg"));
  assert(!browser_capture_url(match.filename, "https://vikingfile.com/d/Ab01234567/Game%20DLC.pkg?x=1"));
  Memory memory = {.length = 3 * PROBE_CHUNK};
  memory.bytes = calloc(1, memory.length); assert(memory.bytes);
  for (size_t step = 1; step <= 2; step++) {
    memset(memory.bytes, 0, memory.length); memset(match.url, 0, sizeof match.url);
    const size_t offset = PROBE_CHUNK - 21;
    for (size_t i = 0; i < strlen(url); i++) memory.bytes[offset + i * step] = (unsigned char)url[i];
    assert(scan(&memory, &match, memory.length) == PROBE_FOUND);
    assert(!strcmp(match.url, url));
    memory.hole = 1;
    assert(scan(&memory, &match, memory.length) == PROBE_ABSENT);
    memory.hole = 0;
    assert(scan(&memory, &match, 10) == PROBE_BUDGET);
    memory.cancelled = 1;
    assert(scan(&memory, &match, memory.length) == PROBE_STOPPED);
    memory.cancelled = 0;
  }
  // A complete terminator is required; no clipped or prefixed URL may be returned.
  assert(!browser_match(&match, (const unsigned char *)url, strlen(url)));
  char prefixed[600]; snprintf(prefixed, sizeof prefixed, "x%s", url);
  assert(!browser_match(&match, (const unsigned char *)prefixed, strlen(prefixed) + 1));
  // The selected public page uses punctuation that the catalog's synthetic name loses.
  match.filename = "G_02225(v02.024)-[DLPSGAME.COM].rar";
  const char *real_name_routes[] = {
    "https://vikingfile.com/d/Ab01234567/G_02225(v02.024)-[DLPSGAME.COM].rar",
    "https://vikingfile.com/d/Ab01234567/G_02225%28v02.024%29-%5BDLPSGAME.COM%5D.rar",
    "https://vik1ngfile.site/d/Ab01234567/G_02225%28v02.024%29-%5BDLPSGAME.COM%5D.rar"
  };
  for (size_t route = 0; route < 3; route++) {
    for (size_t step = 1; step <= 2; step++) {
      memset(memory.bytes, 0, memory.length);
      const char *candidate = real_name_routes[route];
      const size_t offset = PROBE_CHUNK - 21;
      for (size_t i = 0; i < strlen(candidate); i++) memory.bytes[offset + i * step] = (unsigned char)candidate[i];
      assert(scan(&memory, &match, memory.length) == PROBE_FOUND);
      assert(!strcmp(match.url, candidate));
    }
  }
  // Anchor mode: the file link of a host whose check only a console browser can pass. The game's
  // title id names the file, or a known file host names it freely; a page or an advertisement that
  // merely mentions the address inside its query is never the file.
  const char *links[] = {
    "https://datanodes.to/ywsurrfeqhj2/PPSA01285.ffpkg",
    "https://akirabox.to/rk9zKDZRQG0l/PPSA01285-Compressed.7z",
    "https://filekeeper.net/9f3a2b/G_02225(v02.024)-[DLPSGAME.COM].rar",
    "https://cdn9.example.ru/9f3/PPSA01285.exfat?token=8f2a",
    "https://bucket.r2.cloudflarestorage.com/GAMES/PPSA01285.ffpkg",
    "https://datanodes.to/x/ppsa01285.ffpkg"
  };
  for (size_t link = 0; link < sizeof links / sizeof *links; link++)
    assert(browser_capture_anchor_url("PPSA01285", links[link]));
  /* On a host of its own, only the anchor can vouch for the file, so another game's id is refused. */
  const char *anchored[] = {
    "https://cdn9.example.ru/9f3/PPSA01285.exfat?token=8f2a",
    "https://games.cdn8.example/2026/PPSA01285.ffpkg",
    "https://files.example.net/x/PPSA01285-Compressed.7z"
  };
  for (size_t link = 0; link < sizeof anchored / sizeof *anchored; link++) {
    assert(browser_capture_anchor_url("PPSA01285", anchored[link]));
    assert(!browser_capture_anchor_url("PPSA01341", anchored[link]));
  }
  const char *not_links[] = {
    "https://link-vault.org/c/oeSJ8lHw",
    "https://link-vault.org/c/oeSJ8lHw#AkiraBox",
    "https://akirabox.to/rk9zKDZRQG0l/file",
    "https://datanodes.to/ywsurrfeqhj2/",
    "https://ads.example.com/click?u=https://datanodes.to/x/PPSA01285.ffpkg&ref=1",
    "https://ads.example.com/video.mp4?file=https://datanodes.to/x/PPSA01285.ffpkg",
    "https://akirabox.to/login.php",
    "https://akirabox.to/x/archive.zip.txt",
    "http://datanodes.to/x/PPSA01285.ffpkg",
    "https://datanodes.to/x/PPSA01285",
    "https://fileditchfiles.st/balpha23/9f3/PPSA01285.7z.001",
    "https://datavaults.co/PPSA01285"
  };
  for (size_t link = 0; link < sizeof not_links / sizeof *not_links; link++)
    assert(!browser_capture_anchor_url("PPSA01285", not_links[link]));
  /* A known file host is also allowed to name its archive freely, without the title id. */
  assert(browser_capture_anchor_url("PPSA01285", "https://www.filekeeper.net/9f3/Atomic_Heart.rar"));
  assert(browser_capture_anchor_url("PPSA01285", "https://datanodes.to/x/PPSA01285.part01.rar?token=1"));
  assert(!browser_capture_anchor_url("PPSA01285", "https://filekeeper.net.evil.test/9f3/Atomic.rar"));
  assert(!browser_capture_anchor_url("", "https://datanodes.to/x/PPSA01285.ffpkg"));
  // The hosted page hands out its own file address, which the anchor mode picks up before any click.
  const char *page = "<a class=\"btn\" href=\"https:\\/\\/datanodes.to\\/ywsurrfeqhj2\\/PPSA01285.ffpkg\">";
  match.prefix = "https://"; match.anchor = true; match.filename = "PPSA01285";
  memset(match.url, 0, sizeof match.url);
  assert(browser_match(&match, (const unsigned char *)page, strlen(page)));
  assert(!strcmp(match.url, "https://datanodes.to/ywsurrfeqhj2/PPSA01285.ffpkg"));
  // A query whose "&" the page escaped as JSON still yields the address the browser will request.
  const char *escaped = "fetch(\"https://cdn9.example.ru/9f3/PPSA01285.exfat?token=8f2a\\u0026expires=99\");";
  memset(match.url, 0, sizeof match.url);
  assert(browser_match(&match, (const unsigned char *)escaped, strlen(escaped)));
  assert(!strcmp(match.url, "https://cdn9.example.ru/9f3/PPSA01285.exfat?token=8f2a&expires=99"));
  for (size_t step = 1; step <= 2; step++) {
    memset(memory.bytes, 0, memory.length);
    memset(match.url, 0, sizeof match.url);
    const char *candidate = links[0];
    const size_t offset = PROBE_CHUNK - 21;
    for (size_t i = 0; i < strlen(candidate); i++) memory.bytes[offset + i * step] = (unsigned char)candidate[i];
    assert(scan(&memory, &match, memory.length) == PROBE_FOUND);
    assert(!strcmp(match.url, candidate));
  }
  free(memory.bytes);
  puts("browser matcher/scanner: ASCII, UTF-16, block overlap, holes, cancellation, budget and URL binding passed");
}
