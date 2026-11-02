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
  MapLibre GL JS). That file only names their licences, so the full texts are
  in the files below.

Licences of the libraries bundled inside plotly.js, each the upstream file as
is, from the version plotly.js 2.35.0 locks (`package-lock.json`, and
`stackgl_modules/package-lock.json` for the ones marked *stackgl*):

- `ieee754.LICENSE`: ieee754 1.2.1 (BSD-3-Clause)
- `maplibre-gl.LICENSE`: MapLibre GL JS v4.5.2 (BSD-3-Clause; also carries the
  notices for the Mapbox GL JS, glfx.js and d3-color code it contains)
- `polybooljs.LICENSE`: polybooljs 1.2.2 (MIT)
- `object-assign.LICENSE`: object-assign 4.1.1 (MIT)
- `is-buffer.LICENSE`: is-buffer 1.1.6, *stackgl* (MIT)
- `buffer.LICENSE`: buffer, the browser polyfill of Node's `Buffer`, which the
  bundle carries twice, from two versions (MIT; the file is the same in all the
  releases checked, from 3.6.2 to 6.0.3)
- `pad-left.LICENSE`: pad-left 1.0.2, *stackgl* (MIT)
- `repeat-string.LICENSE`: repeat-string 1.6.1, *stackgl* (MIT)
- `safe-buffer.LICENSE`: safe-buffer 5.2.1 (MIT)
- `native-promise-only.LICENSE`: native-promise-only 0.8.1 (MIT). It ships no
  licence file; its header points to http://getify.mit-license.org, and this
  is that page's text without the year, which the site sets to the current
  one.

Upstream: https://github.com/mrdoob/three.js, https://github.com/plotly/plotly.js
