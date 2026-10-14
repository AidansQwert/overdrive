// ../pub-game-store/native/game/GameFacts.jsx
// reconstructed from the esbuild bundle in eboot.bin

import '../assets/Inter-Regular.ttf';
import { jsx, jsxs } from 'react/jsx-runtime';
import { preferredRelease } from './sources.js';
import { useT } from '../i18n/index.js';
import { formatDate3 } from '../state/catalog.js';
import { formatBytes, formatSize } from '../state/size.js';
import { PlatformBadge } from '../ui/PlatformBadge.jsx';
import { Text, View } from '@ps5-react/core';

  function GameFacts({ game }) {
    const t2 = useT();
    const releases = game.releases || [];
    const bytes = releases[preferredRelease(releases)]?.sizeBytes;
    const size = bytes ? [t2("game.facts.downloadSize"), formatBytes(bytes)] : [t2("game.facts.installSize"), game.sizeGb ? formatSize(game.sizeGb) : "\u2014"];
    const platform = /* @__PURE__ */ (0, jsx)(PlatformBadge, { platforms: game.platforms });
    const rows = [
      [t2("game.facts.platform"), platform],
      [t2("game.facts.publisher"), game.publisher || "\u2014"],
      [t2("game.facts.releaseDate"), formatDate3(game.releaseDate)],
      size
    ];
    return /* @__PURE__ */ (0, jsxs)(View, { style: { width: 432 }, children: [
      /* @__PURE__ */ (0, jsx)(Text, { style: { fontFamily: "Inter-Regular", fontSize: 30, fontWeight: 600, color: "#f2f2f3", lineHeight: 42, letterSpacing: -0.75 }, children: t2("game.details") }),
      /* @__PURE__ */ (0, jsx)(View, { style: { marginTop: 24, borderBottomWidth: 2, borderColor: "#2a2a2f" }, children: rows.map(
        ([label, value]) => /* @__PURE__ */ (0, jsxs)(View, { style: { flexDirection: "row", justifyContent: "space-between", gap: 24, borderTopWidth: 2, borderColor: "#2a2a2f", paddingVertical: 18 }, children: [
          /* @__PURE__ */ (0, jsx)(Text, { style: { fontFamily: "Inter-Regular", fontSize: 21, color: "#8a8a94", lineHeight: 30 }, children: label }),
          typeof value === "string" ? /* @__PURE__ */ (0, jsx)(Text, { style: { width: 264, fontFamily: "Inter-Regular", fontSize: 21, color: "#f2f2f3", textAlign: "end", numberOfLines: 1, ellipsizeMode: "tail", lineHeight: 30 }, children: value }) : value
        ] }, label)
      ) })
    ] });
  }

export { GameFacts };
