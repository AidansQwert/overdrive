// Adapted from Orbit Store 1.0.1 by saawant12 (https://github.com/saawant12/orbit-store-ps5).
// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only browser capture; original corresponding source is recorded in README.md.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#define BROWSER_CAPTURE_REJECTED_MAX 6
typedef struct {
  const char *prefix;   /* The one prefix to scan for; the request's, in anchor mode. */
  const char *filename; /* The expected filename, or the anchor that stands for it. */
  bool anchor;          /* Anchor mode: any file link carrying the anchor, or on a known host. */
  /* Links the app already probed and refused this session; the scan passes over them. */
  const char *rejected[BROWSER_CAPTURE_REJECTED_MAX];
  size_t rejected_count;
  char url[544];
} BrowserMatch;
extern const char *const browser_capture_prefixes[2];
const char *browser_capture_prefix(const char *url);
bool browser_capture_url(const char *filename, const char *url);
bool browser_capture_anchor_url(const char *anchor, const char *url);
bool browser_match(void *context, const unsigned char *bytes, size_t length);
