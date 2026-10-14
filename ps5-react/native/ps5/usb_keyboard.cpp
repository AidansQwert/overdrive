// Copyright (C) 2026 half144 and PS5 React contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional attribution term: see LICENSE-ATTRIBUTION.
#include "usb_keyboard.hpp"

#include "async_log.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>

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

extern "C" {
int sceUserServiceGetForegroundUser(int* user);
int sceSysmoduleLoadModule(std::uint16_t id);
int sceSysmoduleGetModuleHandleInternal(std::uint32_t id, std::int32_t* handle);
int sceKernelDlsym(std::int32_t handle, const char* symbol, void** address);
}

namespace {

// libSceKeyboard is not loaded for the title. Calling it directly jumps to nothing (CE-108255-1 at
// launch, rip 0x780); the SDK's dlopen works only in payloads; and a fake-signed title may not load
// the module by path (0x80020063). The system loads it as one of its modules by id instead, as
// prospero-win (native/pw_hid_ps5.c) measured on the console: sceSysmoduleLoadModule before
// filesystem elevation, the module's handle from sceSysmoduleGetModuleHandleInternal (by its id or
// its internal id, 0x80000000 | id), and each call from sceKernelDlsym. A step that fails leaves the
// keyboard off and the title running.
constexpr std::uint16_t kKeyboardModule = 0x0106;
constexpr std::int64_t kOpenRetryUs = 3000000;

struct KeyboardCalls {
  int (*init)(void) = nullptr;
  int (*open)(int user, int type, int index, void* param) = nullptr;
  int (*read_state)(int handle, KeyboardState* state) = nullptr;
  int (*close)(int handle) = nullptr;
};
KeyboardCalls calls;
// What preload found, for Session::open to log.
int load_status = 0;
std::int32_t module_handle = -1;
const char* missing_call = nullptr;
bool preloaded = false;

std::int32_t system_module_handle(std::uint16_t id) {
  std::int32_t handle = -1;
  if (sceSysmoduleGetModuleHandleInternal(id, &handle) == 0 && handle >= 0) return handle;
  handle = -1;
  if (sceSysmoduleGetModuleHandleInternal(0x80000000u | id, &handle) == 0 && handle >= 0) return handle;
  return -1;
}

template <typename Function>
bool find_call(Function& function, const char* name) {
  void* address = nullptr;
  if (sceKernelDlsym(module_handle, name, &address) != 0 || !address) {
    if (!missing_call) missing_call = name;
    return false;
  }
  function = reinterpret_cast<Function>(address);
  return true;
}

}  // namespace

namespace usb_keyboard {

void preload() {
  preloaded = true;
  load_status = sceSysmoduleLoadModule(kKeyboardModule);
  module_handle = system_module_handle(kKeyboardModule);
  if (module_handle < 0) return;
  // Every lookup runs, so the log names the first one missing.
  const bool has_init = find_call(calls.init, "sceKeyboardInit");
  const bool has_open = find_call(calls.open, "sceKeyboardOpen");
  const bool has_read = find_call(calls.read_state, "sceKeyboardReadState");
  const bool has_close = find_call(calls.close, "sceKeyboardClose");
  if (!(has_init && has_open && has_read && has_close)) calls = {};
}

Session::~Session() {
  if (handle_ >= 0 && calls.close) calls.close(handle_);
}

bool Session::open() {
  async_log::write("[PS5-REACT] keyboard: module 0x%04x load=0x%08x handle=%d%s%s", static_cast<unsigned>(kKeyboardModule),
                   static_cast<unsigned>(load_status), static_cast<int>(module_handle),
                   missing_call ? " missing " : "", missing_call ? missing_call : "");
  if (!preloaded || !calls.init) return false;
  if (int error = sceUserServiceGetForegroundUser(&user_); error != 0 || user_ == kInvalidUserId) {
    async_log::write("[PS5-REACT] keyboard: no foreground user 0x%08x", static_cast<unsigned>(error));
    return false;
  }
  if (int error = calls.init(); error != 0) {
    async_log::write("[PS5-REACT] keyboard: sceKeyboardInit=0x%08x", static_cast<unsigned>(error));
    return false;
  }
  ready_ = true;
  try_open(0);
  return handle_ >= 0;
}

void Session::try_open(std::int64_t now_us) {
  std::uint8_t param[8] = {};
  const int handle = calls.open(user_, 0, 0, param);
  retry_at_us_ = now_us + kOpenRetryUs;
  if (handle >= 0) {
    handle_ = handle;
    async_log::write("[PS5-REACT] keyboard: open handle=%d", handle);
    return;
  }
  // Without a keyboard plugged in this fails every few seconds: logged the first time only.
  if (!open_failed_) async_log::write("[PS5-REACT] keyboard: sceKeyboardOpen=0x%08x, retrying", static_cast<unsigned>(handle));
  open_failed_ = true;
}

bool Session::poll(std::int64_t now_us, Emit emit) {
  if (handle_ < 0) {
    if (!ready_ || now_us < retry_at_us_) return true;
    try_open(now_us);
    if (handle_ < 0) return true;
  }
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
