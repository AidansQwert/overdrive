// Adapted from Orbit Store 1.0.1 and 1.3.5 by saawant12 (https://github.com/saawant12/orbit-store-ps5).
// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only browser capture; original corresponding source is recorded in README.md.
#include "browser_match.h"
#include "browser_scan.h"
#include <string.h>
_Static_assert(sizeof(((BrowserMatch *)0)->url) <= PROBE_MATCH_MAX, "Scanner overlap must cover encoded filenames");
static bool boundary(unsigned char c) {
    return c == 0 || c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '"' || c == '\'' || c == '<' || c == '>';
}
static int hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/* One URL character at *position, in ASCII (step 1) or UTF-16LE (step 2). Vikingfile's verified
 * link is already in the network process once the verification passes, but as JSON, before a
 * click on the page's Download button (and its advertisements) makes a navigation URL of it
 * (Orbit Store 1.3.5). A captured link comes out of live browser memory, so its escapes are
 * whichever the page used: JSON's "\/" for a path slash and "\uXXXX" for a character such as a
 * query's "&". Both are decoded once; any other escape is not a download address. */
static bool next_url_char(const unsigned char *bytes, size_t length, size_t step,
                          size_t *position, unsigned char *out) {
    if (*position > length || length - *position < step) return false;
    unsigned char c = bytes[*position];
    if (step == 2 && bytes[*position + 1]) return false;
    *position += step;
    if (c == '\\') {
        if (length - *position < step || (step == 2 && bytes[*position + 1])) return false;
        if (bytes[*position] == '/') {
            *position += step;
            c = '/';
        } else if (bytes[*position] == 'u') {
            unsigned value = 0;
            for (size_t i = 0; i < 4; i++) {
                const size_t at = *position + (i + 1) * step;
                if (at > length || length - at < step || (step == 2 && bytes[at + 1])) return false;
                const int digit = hex(bytes[at]);
                if (digit < 0) return false;
                value = value * 16 + (unsigned)digit;
            }
            if (value > 126) return false; /* Only printable ASCII continues a link. */
            *position += 5 * step;
            c = (unsigned char)value;
        } else return false;
    }
    *out = c;
    return true;
}
static bool rejected(const BrowserMatch *match, const char *candidate) {
    for (size_t i = 0; i < match->rejected_count; i++)
        if (!strcmp(match->rejected[i], candidate)) return true;
    return false;
}
/* Reads one candidate URL after `prefix` and hands it to the mode's validator. */
static bool scan_prefix(BrowserMatch *match, const unsigned char *bytes, size_t length,
                        const char *prefix, bool anchor) {
    const size_t plen = strlen(prefix);
    for (size_t step = 1; step <= 2; step++) {
        for (size_t i = 0; (plen + 1) * step <= length && i <= length - (plen + 1) * step; i++) {
            if (bytes[i] != 'h' || (i >= step && !boundary(bytes[i - step]))) continue;
            size_t position = i;
            bool valid = true;
            for (size_t j = 0; j < plen; j++) {
                unsigned char c;
                if (!next_url_char(bytes, length, step, &position, &c) ||
                    c != (unsigned char)prefix[j]) { valid = false; break; }
            }
            if (!valid) continue;
            char candidate[sizeof match->url];
            memcpy(candidate, prefix, plen);
            for (size_t j = plen; j < sizeof candidate; j++) {
                unsigned char c;
                if (!next_url_char(bytes, length, step, &position, &c)) break;
                if (boundary(c)) {
                    candidate[j] = 0;
                    const bool hit = anchor ? browser_capture_anchor_url(match->filename, candidate)
                                            : browser_capture_url(match->filename, candidate);
                    if (hit && !rejected(match, candidate)) {
                        memcpy(match->url, candidate, j + 1);
                        return true;
                    }
                    if (c != '\'') break; /* Apostrophe may be inside an unescaped path. */
                }
                if (c < 33 || c > 126) break;
                candidate[j] = (char)c;
            }
        }
    }
    return false;
}
bool browser_match(void *context, const unsigned char *bytes, size_t length) {
    BrowserMatch *match = context;
    if (match->anchor) return match->prefix && scan_prefix(match, bytes, length, match->prefix, true);
    /* Vikingfile's two routes are both scanned: a link page may hand out either one. */
    for (size_t which = 0; which < 2; which++)
        if (scan_prefix(match, bytes, length, browser_capture_prefixes[which], false)) return true;
    return false;
}
