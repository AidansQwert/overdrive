// Copyright (C) 2026 half144 and PS5 React contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional attribution term: see LICENSE-ATTRIBUTION.
#include "usb_keyboard.hpp"

#include "async_log.hpp"

#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <iterator>
#include <type_traits>

namespace {

// The state sceKeyboardReadState fills, laid out as the ps5-payload-dev SDL port reads it
// (src/video/ps5/SDL_ps5keyboard.c): an availability flag, the modifier bits, and up to sixteen held
// keys as USB HID usage IDs. The SDK ships the library's stubs without headers. The spare tail only
// absorbs a firmware that writes more than that layout, so it never lands on the stack around it.
struct KeyboardState {
  std::uint64_t reserved0[2];
  std::uint8_t available;
  std::uint32_t reserved1[2];
  std::uint32_t modifiers;
  std::uint16_t keys[16];
  std::uint64_t reserved2[4];
  std::uint8_t spare[256];
};

constexpr std::uint32_t kShift = 0x02 | 0x20;  // left and right Shift
constexpr std::uint32_t kCtrlAltGui = 0x01 | 0x04 | 0x08 | 0x10 | 0x40 | 0x80;
constexpr std::int64_t kRepeatDelayUs = 500000, kRepeatIntervalUs = 40000;
constexpr int kInvalidUserId = -1;

// USB HID usages 0x04..0x38 (letters, digits, Enter..Space and punctuation) on a US layout, plain and
// shifted. A zero is a key handled elsewhere or not a character.
constexpr char kPlain[] =
    "abcdefghijklmnopqrstuvwxyz1234567890\0\0\0\0 -=[]\\\0;'`,./";
constexpr char kShifted[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\0\0\0\0 _+{}|\0:\"~<>?";
static_assert(sizeof kPlain == 0x38 - 0x04 + 2 && sizeof kShifted == sizeof kPlain);

constexpr std::uint16_t kEnter = 0x28, kEscape = 0x29, kBackspace = 0x2A, kTab = 0x2B, kCapsLock = 0x39,
                        kDelete = 0x4C, kKeypadEnter = 0x58;

// The editing key a usage names, or nullptr.
const char* key_name(std::uint16_t usage) {
  switch (usage) {
    case kEnter: case kKeypadEnter: return "Enter";
    case kEscape: return "Escape";
    case kBackspace: case kDelete: return "Backspace";
    case kTab: return "Tab";
    default: return nullptr;
  }
}

// The character a usage types, or 0. Caps Lock shifts letters only, as on a computer.
char character(std::uint16_t usage, bool shift, bool caps_lock) {
  if (usage >= 0x04 && usage <= 0x38) {
    const bool letter = usage <= 0x1D;
    const char c = (shift != (letter && caps_lock) ? kShifted : kPlain)[usage - 0x04];
    return c;
  }
  // Keypad: / * - + Enter(handled) 1..9 0 .
  static constexpr char kKeypad[] = "/*-+\0" "1234567890.";
  if (usage >= 0x54 && usage <= 0x63) return kKeypad[usage - 0x54];
  return 0;
}

bool held(const std::uint16_t (&keys)[16], std::uint16_t usage) {
  return usage && std::find(std::begin(keys), std::end(keys), usage) != std::end(keys);
}

}  // namespace

extern "C" int sceUserServiceGetForegroundUser(int* user);

namespace {

// libSceKeyboard is not one of the modules the title links, so its calls are looked up when the
// keyboard opens, like host_platform.cpp's OptionalSymbol: a direct call into a module the process
// never loaded jumps to nothing and ends the title (CE-108255-1 at launch, rip 0x780). Missing any
// of them leaves the keyboard off and the title running.
struct KeyboardCalls {
  int (*init)(void) = nullptr;
  int (*open)(int user, int type, int index, void* param) = nullptr;
  int (*read_state)(int handle, KeyboardState* state) = nullptr;
  int (*close)(int handle) = nullptr;
};
KeyboardCalls calls;

bool resolve_calls() {
  constexpr const char* kModule = "libSceKeyboard.sprx";
  void* module = dlopen(kModule, RTLD_LAZY);
  if (!module) {
    async_log::write("[PS5-REACT] keyboard: %s unavailable", kModule);
    return false;
  }
  auto find = [&](auto& function, const char* name) {
    function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(dlsym(module, name));
    if (!function) async_log::write("[PS5-REACT] keyboard: %s unavailable from %s", name, kModule);
    return function != nullptr;
  };
  const bool found = find(calls.init, "sceKeyboardInit") & find(calls.open, "sceKeyboardOpen") &
                     find(calls.read_state, "sceKeyboardReadState") & find(calls.close, "sceKeyboardClose");
  if (!found) calls = {};
  return found;
}

}  // namespace

namespace usb_keyboard {

Session::~Session() {
  if (handle_ > 0) calls.close(handle_);
}

bool Session::open() {
  int user = kInvalidUserId;
  if (int error = sceUserServiceGetForegroundUser(&user); error != 0 || user == kInvalidUserId) {
    async_log::write("[PS5-REACT] keyboard: no foreground user 0x%08x", static_cast<unsigned>(error));
    return false;
  }
  if (!resolve_calls()) return false;
  if (int error = calls.init(); error != 0) {
    async_log::write("[PS5-REACT] keyboard: sceKeyboardInit=0x%08x", static_cast<unsigned>(error));
    return false;
  }
  long param = 0;
  handle_ = calls.open(user, 0, 0, &param);
  if (handle_ <= 0) {
    async_log::write("[PS5-REACT] keyboard: sceKeyboardOpen=0x%08x", static_cast<unsigned>(handle_));
    handle_ = -1;
    return false;
  }
  return true;
}

bool Session::poll(std::int64_t now_us, Emit emit) {
  if (handle_ <= 0) return true;
  KeyboardState state;
  std::memset(&state, 0, sizeof state);
  if (int error = calls.read_state(handle_, &state); error != 0) {
    // Logged once per run of failures: an unplugged keyboard must not fill the log every frame.
    if (!read_failed_) async_log::write("[PS5-REACT] keyboard: sceKeyboardReadState=0x%08x", static_cast<unsigned>(error));
    read_failed_ = true;
    return true;
  }
  read_failed_ = false;
  std::uint16_t keys[16] = {};
  if (state.available) std::copy(std::begin(state.keys), std::end(state.keys), std::begin(keys));
  // Ctrl, Alt and the system key make shortcuts, not text.
  const bool shortcut = state.available && (state.modifiers & kCtrlAltGui);
  const bool shift = state.available && (state.modifiers & kShift);

  // Sends one press of `usage`; false when the app's input call failed.
  auto press = [&](std::uint16_t usage) {
    if (const char* name = key_name(usage)) return emit("key", name);
    if (shortcut) return true;
    const char c = character(usage, shift, caps_lock_);
    if (!c) return true;
    const char text[2] = {c, 0};
    return emit("text", text);
  };

  bool ok = true;
  for (std::uint16_t usage : keys) {
    if (!usage || held(held_, usage)) continue;  // only newly pressed keys
    if (usage == kCapsLock) {
      caps_lock_ = !caps_lock_;
      continue;
    }
    if (ok) ok = press(usage);
    repeat_key_ = usage;
    repeat_shift_ = shift;
    repeat_at_us_ = now_us + kRepeatDelayUs;
  }
  // The last key pressed repeats while it stays down; a released key or a Shift change stops it.
  if (repeat_key_ && (!held(keys, repeat_key_) || repeat_shift_ != shift)) repeat_key_ = 0;
  if (ok && repeat_key_ && repeat_key_ != kCapsLock && now_us >= repeat_at_us_) {
    ok = press(repeat_key_);
    // Keeps the cadence across frames that don't fall on it; after a stall it resumes, never bursts.
    repeat_at_us_ += kRepeatIntervalUs;
    if (repeat_at_us_ <= now_us) repeat_at_us_ = now_us + kRepeatIntervalUs;
  }
  std::copy(std::begin(keys), std::end(keys), std::begin(held_));
  return ok;
}

}  // namespace usb_keyboard
