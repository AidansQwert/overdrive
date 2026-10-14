// ../pub-game-store/native/ui/haptics.js
// reconstructed from the esbuild bundle in eboot.bin
// MODIFIED: navigation haptics disabled — see the note on useInterfaceHaptics.

import { Controller } from '@ps5-react/core';

  // Navigation haptics are off: a pulse on every focus move, press and page
  // change made the controller buzz the whole time you were browsing. The hook
  // stays — `index.jsx` still calls it — but it subscribes to nothing now.
  //
  // The original table, should they ever be wanted back: TICK [0.09, 35] on a
  // focus move, PRESS [0.15, 30] on a handled press, BACK [0.11, 20] on a
  // handled back, PAGE [0.18, 35] on l1/r1/l2/r2, EDGE [0.18, 25] when focus is
  // blocked (repeated once after EDGE_GAP_MS = 70).
  function useInterfaceHaptics() {}
  // Still wanted: one distinct pulse when a download finishes, not on navigation.
  var NOTICE = [0.25, 140];
  var pulseNotice = () => Controller.vibrate(NOTICE[0], NOTICE[1]);

export { pulseNotice, useInterfaceHaptics };
