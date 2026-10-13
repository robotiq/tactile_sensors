# Vendored libraries

Committed rather than fetched from a CDN so the demo runs with no internet,
which is the normal state of a show floor.

| File | Source | Licence |
|---|---|---|
| `three.module.js` | three.js r160, `build/three.module.js` | MIT |
| `OrbitControls.js` | three.js r160, `examples/jsm/controls/OrbitControls.js` | MIT |
| `plotly-2.35.0.min.js` | plotly.js 2.35.0, `dist/plotly.min.js` (the full bundle: the page needs `heatmap` and `scattergl`, which no partial bundle has together) | MIT |

All carry their licence headers inline, and the full licence texts are here
too, as MIT and BSD require copies to keep them:

- `three.LICENSE`: three.js
- `plotly.LICENSE`: plotly.js
- `plotly.min.js.LICENSE.txt`: the libraries bundled inside plotly.js, which its
  header points to under that name (MIT, and BSD-3-Clause for ieee754 and
  MapLibre GL JS)

Upstream: https://github.com/mrdoob/three.js, https://github.com/plotly/plotly.js
