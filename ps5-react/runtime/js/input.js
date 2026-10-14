// Copyright (C) 2026 half144 and PS5 React contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional attribution term: see LICENSE-ATTRIBUTION.
const listeners = new Set();
const touchListeners = new Set();
const keyboardListeners = new Set();

// Actions as strings, touch samples as [x, y] (x null when the finger lifts), keyboard input as
// {type, value}, in arrival order.
const pending = [];

function deliver() {
  for (const item of pending.splice(0)) {
    if (typeof item === 'string') for (const listener of listeners) listener(item);
    else if (Array.isArray(item)) for (const listener of touchListeners) listener(item[0], item[1]);
    else for (const listener of keyboardListeners) listener(item);
  }
}

// Input protocol v5 (versioned apart from the native API's ABI in host_api.hpp): hosts emit
// up/down/left/right/confirm/back/l1/r1/l2/r2/triangle/square/options (v2: the first six; v3: up to
// square) on the JS/render thread, before the frame's pump. Actions are delivered from the pump's microtask
// drain, which runs inside React's batch like the bridge's own events: a direct host call is outside
// it, so each setState of a handler would render and commit on its own, and the frame log would
// count that render outside `react`.
globalThis.__ps5ReactDispatch = action => {
  if (pending.push(action) === 1) Promise.resolve().then(deliver);
};

// Input protocol v5: the touchpad's first finger, each frame it moves, at (x, y) 0..1 across the pad,
// and (null) once when it lifts. The focus runtime turns its travel into moves (focus/touch.js).
globalThis.__ps5ReactTouch = (x, y) => {
  if (pending.push([x ?? null, y ?? null]) === 1) Promise.resolve().then(deliver);
};

// Input protocol v6: a USB keyboard on the PS5 host, as ('text', characters) for what was typed and
// ('key', name) for Backspace, Enter, Escape and Tab. The keys reach only keyboard listeners, never
// the focus runtime, so a keyboard types into fields without moving focus.
globalThis.__ps5ReactKeyboard = (type, value) => {
  if (type !== 'text' && type !== 'key') return;
  if (pending.push({type, value: String(value)}) === 1) Promise.resolve().then(deliver);
};

/**
 * The host actions, as native/shared/actions.hpp names them.
 * @typedef {'l1' | 'r1' | 'l2' | 'r2' | 'triangle' | 'square' | 'options'} ButtonAction shoulders, triggers, Triangle, Square, Options
 * @typedef {'up' | 'down' | 'left' | 'right' | 'confirm' | 'back' | ButtonAction} Action
 */

/**
 * Internal: receive every host action until the returned function is called.
 * @param {(action: Action) => void} listener
 */
export function subscribeInput(listener) {
  listeners.add(listener);
  return () => { listeners.delete(listener); };
}

/**
 * Internal: receive the touchpad's finger until the returned function is called.
 * @param {(x: number | null, y: number | null) => void} listener
 */
export function subscribeTouch(listener) {
  touchListeners.add(listener);
  return () => { touchListeners.delete(listener); };
}

/**
 * Keyboard input: `text` carries typed characters, `key` names an editing key.
 * @typedef {{type: 'text', value: string} | {type: 'key', value: 'Backspace' | 'Enter' | 'Escape' | 'Tab'}} KeyboardInput
 */

/**
 * Internal: receive keyboard input until the returned function is called.
 * @param {(input: KeyboardInput) => void} listener
 */
export function subscribeKeyboard(listener) {
  keyboardListeners.add(listener);
  return () => { keyboardListeners.delete(listener); };
}
