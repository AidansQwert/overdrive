// Copyright (C) 2026 half144 and PS5 React contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional attribution term: see LICENSE-ATTRIBUTION.
#pragma once

#include <cstdint>

// A USB keyboard read through libSceKeyboard, turned into input protocol v6 keyboard input: typed
// characters (US layout) and the editing keys Backspace, Enter, Escape and Tab, with hold-to-repeat.
// Without a keyboard, or if the console refuses one, the title runs as before and nothing is sent.
namespace usb_keyboard {

// Has the system load libSceKeyboard and finds its calls. Call it from main(), before filesystem
// elevation: afterwards the system refuses module loads (0x80020063, as video_decoder.cpp found).
// It logs nothing itself; Session::open reports what it found once the log is running.
void preload();

// Receives ("text", characters) or ("key", name); false stops the frame, like the other input calls.
using Emit = bool (*)(const char* type, const char* value);

class Session {
 public:
  Session() = default;
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  ~Session();

  // After the pad opens: it initializes the user service the keyboard is opened for. True when a
  // keyboard is open now; without one, poll keeps trying, so a keyboard plugged in later works.
  bool open();
  // Once per frame on the render thread.
  bool poll(std::int64_t now_us, Emit emit);

 private:
  void try_open(std::int64_t now_us);

  int user_ = -1;
  int handle_ = -1;
  bool ready_ = false;
  bool open_failed_ = false;
  std::int64_t retry_at_us_ = 0;
  bool caps_lock_ = false;
  bool read_failed_ = false;
  std::uint16_t held_[16] = {};
  std::uint16_t repeat_key_ = 0;
  bool repeat_shift_ = false;
  std::int64_t repeat_at_us_ = 0;
};

}  // namespace usb_keyboard
