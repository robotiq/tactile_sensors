// Robotiq Tactile Sensor Web Viewer
// WebSocket client + Plotly.js chart rendering.
//
// Three tabs over one connection. The server streams only the tab the browser
// says it is showing and tags each frame with it, so a hidden tab costs nothing
// on either side and a frame still in flight when you switch cannot restyle the
// wrong plots. Charts are built on a tab's first reveal rather than at load:
// every line plot is scattergl, and creating all twelve up front would spend
// most of the browser's WebGL contexts on views nobody has opened.
//
// This is an ES module because the 3D gripper is loaded on demand — see
// enableGripper() at the bottom.

const WS_PORT_OFFSET = 1;
const PLOTLY_CONFIG = { responsive: true, displayModeBar: false };

// Colorscale matching MathGL: {B,0}{b,0.17}{c,0.25}{y,0.35}{r,0.55}{R,0.85}
const TACTILE_COLORSCALE = [
    [0,    'rgb(0,0,128)'],
    [0.17, 'rgb(0,0,255)'],
    [0.25, 'rgb(0,255,255)'],
    [0.35, 'rgb(255,255,0)'],
    [0.55, 'rgb(255,0,0)'],
    [0.85, 'rgb(128,0,0)'],
    [1.0,  'rgb(128,0,0)']
];

// Chrome for the signal tabs: room for axis titles and a legend.
const COMPACT_MARGIN = { t: 10, b: 40, l: 50, r: 20 };
const IMU_COLORS = ['#1f77b4', '#ff7f0e', '#2ca02c'];

// Chrome for the overview tab: tight, because those plots share the page with the
// gripper drawing and the column headings already say what they are.
const OVERVIEW_MARGIN = { t: 6, b: 20, l: 36, r: 6 };
const OVERVIEW_AXIS_STYLE = {
    gridcolor: '#243b6b',
    zerolinecolor: '#2f4a86',
    linecolor: '#2f4a86',
    tickfont: { size: 9 },
    automargin: false
};

function overviewBaseLayout(extra) {
    return Object.assign({
        paper_bgcolor: 'rgba(0,0,0,0)',
        plot_bgcolor: '#0e1630',
        font: { color: '#8fa0c0', size: 9 },
        margin: OVERVIEW_MARGIN,
        showlegend: false
    }, extra);
}

let ws = null;
let activeTab = 'overview';   // must match WebViewer.active_tab on the server
let frameCount = 0;

// --- WebSocket ---

function connect() {
    const wsPort = parseInt(location.port) + WS_PORT_OFFSET;
    ws = new WebSocket(`ws://${location.hostname}:${wsPort}`);
    ws.onopen = () => {
        document.getElementById('connection-status').textContent = 'Connected';
        document.getElementById('connection-status').className = 'status-connected';
        // Re-announce the tab: it may have been switched before we connected.
        ws.send(JSON.stringify({ type: 'tab_change', tab: activeTab }));
    };
    ws.onclose = () => {
        // Server stopped — try to close the tab, otherwise show overlay
        window.close();
        document.getElementById('connection-status').textContent = 'Server stopped';
        document.getElementById('connection-status').className = 'status-disconnected';
        const overlay = document.createElement('div');
        overlay.style.cssText = 'position:fixed;inset:0;background:rgba(0,0,0,0.7);display:flex;align-items:center;justify-content:center;z-index:9999';
        overlay.innerHTML = '<div style="color:#fff;font-size:1.5rem;text-align:center">Server stopped.<br>You can close this tab.</div>';
        document.body.appendChild(overlay);
    };
    ws.onmessage = (event) => {
        const msg = JSON.parse(event.data);
        if (msg.type === 'data') handleData(msg);
        else if (msg.type === 'fft') renderFFT(msg.fft);
    };
}

function send(msg) {
    if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(msg));
}

// --- Data Handling (just render server snapshots directly) ---

function handleData(msg) {
    frameCount++;
    document.getElementById('sample-count').textContent = `Frame: ${frameCount}`;
    switch (msg.tab) {
        case 'overview':
            renderHeatmaps(msg.static, msg.maxRange);
            renderOverviewDynamic(msg.dynamic);
            renderGripperControl(msg.gripper);
            renderOrientation(msg.orientationSeen);
            if (gripper) {
                gripper.renderGripper(msg.tipAngle, msg.tipAngleValid, msg.gripper?.position ?? null);
                gripper.renderWrench(msg.wrench, msg.wrenchError, msg.ftOrigin);
            }
            break;
        case 'dynamic': renderDynamic(msg.dynamic); break;
        case 'imu':     renderIMU(msg.accel, msg.gyro); break;
    }
}

// --- Static Heatmaps ---

// Taxel centres, in the axes' units: one per cell of the 4x7 grid.
const TAXEL_X = [0.5, 1.5, 2.5, 3.5];
const TAXEL_Y = [0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5];

// Two ways to show the pads, picked from the toolbar:
//   - raw: one flat cell per taxel, exactly as measured;
//   - interpolated: squares half a taxel pitch wide, alternating along each
//     axis between a taxel and the gap to its neighbour. So there is one
//     square on each taxel centre holding its raw value, one between each two
//     neighbouring taxels holding their mean, and one between each four
//     holding the mean of those four. Nothing is extrapolated: the outermost
//     squares just stretch out to the border with the edge taxels' values.
//     The taxel boundaries are drawn on top, so it stays clear where each
//     measurement came from; the squares themselves have no outline.
const HEATMAP_MODE_KEY = 'viewer.heatmapMode';
let heatmapMode = 'raw';

// Square edges in axis units, so 4 x 7 taxels give 7 x 13 squares. Taxel
// centres sit at half-integers and the midpoints between them at integers, so
// the squares are half a pitch wide, centred on those -- except the outermost,
// which reach the border: 0, 0.75, 1.25, ..., n - 0.75, n.
const interpEdges = (n) => [0, ...Array.from({ length: 2 * n - 2 }, (_, i) => 0.75 + i / 2), n];
const INTERP_X = interpEdges(4);
const INTERP_Y = interpEdges(7);

// The raw taxel boundaries, drawn over the interpolated squares.
const TAXEL_GRID_SHAPES = [
    ...[1, 2, 3].map(x => ({ x0: x, x1: x, y0: 0, y1: 7 })),
    ...[1, 2, 3, 4, 5, 6].map(y => ({ x0: 0, x1: 4, y0: y, y1: y })),
].map(s => Object.assign({ type: 'line', xref: 'x', yref: 'y', layer: 'above',
                           line: { color: 'rgba(255,255,255,0.6)', width: 1.5 } }, s));

// Even index i is taxel i/2; odd is the mean of the taxels either side.
function interpLine(values) {
    const out = [];
    for (let i = 0; i < values.length; i++) {
        if (i) out.push((values[i - 1] + values[i]) / 2);
        out.push(values[i]);
    }
    return out;
}

// Along rows, then along columns: a square between four taxels comes out as
// the mean of all four.
function interpolateTaxels(rows) {
    const wide = rows.map(interpLine);
    const columns = wide[0].map((_, c) => interpLine(wide.map(row => row[c])));
    return columns[0].map((_, r) => columns.map(col => col[r]));
}

function renderHeatmaps(data, maxRanges) {
    if (!data) return;
    for (let f = 0; f < 2; f++) {
        const z = [];
        for (let row = 0; row < 7; row++)
            z.push(data[f].slice(row * 4, (row + 1) * 4));
        const zmax = Math.max(maxRanges[f], 1);
        // Traces: 0 raw, 1 interpolated. Only the visible one is updated.
        if (heatmapMode === 'interpolated')
            Plotly.restyle(`overview-static-${f}`, { z: [interpolateTaxels(z)], zmax }, [1]);
        else
            Plotly.restyle(`overview-static-${f}`, { z: [z], zmax }, [0]);
    }
}

const HEATMAP_COLORBAR = { thickness: 6, outlinewidth: 0, tickfont: { size: 8 }, len: 1, x: 1.02 };

function initOverviewStaticChart(divId) {
    const interpolated = heatmapMode === 'interpolated';
    Plotly.newPlot(divId, [{
        x: TAXEL_X,
        y: TAXEL_Y,
        z: Array(7).fill(null).map(() => Array(4).fill(0)),
        type: 'heatmap',
        colorscale: TACTILE_COLORSCALE,
        // One flat cell per taxel, as measured. The gaps show the grid.
        zsmooth: false,
        xgap: 2,
        ygap: 2,
        zmin: 0, zmax: 3000,
        visible: !interpolated,
        colorbar: HEATMAP_COLORBAR
    }, {
        x: INTERP_X,
        y: INTERP_Y,
        // One more edge than squares on each axis.
        z: INTERP_Y.slice(1).map(() => INTERP_X.slice(1).map(() => 0)),
        type: 'heatmap',
        colorscale: TACTILE_COLORSCALE,
        zsmooth: false,
        zmin: 0, zmax: 3000,
        visible: interpolated,
        colorbar: HEATMAP_COLORBAR
    }], overviewBaseLayout({
        xaxis: Object.assign({}, OVERVIEW_AXIS_STYLE,
                             { dtick: 1, range: [0, 4], constrain: 'domain' }),
        yaxis: Object.assign({}, OVERVIEW_AXIS_STYLE, {
            dtick: 1, range: [7, 0], scaleanchor: 'x', scaleratio: 1, constrain: 'domain'
        }),
        shapes: interpolated ? TAXEL_GRID_SHAPES : [],
        margin: { t: 6, b: 18, l: 18, r: 0 }
    }), PLOTLY_CONFIG);
}

function setHeatmapMode(mode) {
    heatmapMode = mode;
    try { localStorage.setItem(HEATMAP_MODE_KEY, mode); } catch (e) { /* private mode */ }
    if (!tabsReady.has('overview')) return;
    const interpolated = mode === 'interpolated';
    for (let f = 0; f < 2; f++) {
        const id = `overview-static-${f}`;
        Plotly.restyle(id, { visible: [!interpolated, interpolated] }, [0, 1]);
        Plotly.relayout(id, { shapes: interpolated ? TAXEL_GRID_SHAPES : [] });
    }
}

// --- Dynamic Time-Domain + FFT (signal tabs) ---

function initDynamicChart(divId) {
    Plotly.newPlot(divId, [{
        y: [], type: 'scattergl', mode: 'lines',
        line: { width: 1, color: '#1f77b4' }
    }], {
        xaxis: { title: 'Sample' },
        yaxis: { title: 'mV', range: [-1, 1] },
        margin: COMPACT_MARGIN
    }, PLOTLY_CONFIG);
}

function initFFTChart(divId) {
    Plotly.newPlot(divId, [{
        y: [], type: 'scattergl', mode: 'lines',
        line: { width: 1, color: '#ff7f0e' }
    }], {
        xaxis: { title: 'Hz', type: 'log', range: [Math.log10(0.5), Math.log10(500)] },
        yaxis: { title: 'Magnitude' },
        margin: COMPACT_MARGIN
    }, PLOTLY_CONFIG);
}

function renderDynamic(dynData) {
    if (!dynData) return;
    for (let f = 0; f < 2; f++) {
        const samples = dynData[f];
        const mV = new Float32Array(samples.length);
        for (let i = 0; i < samples.length; i++) mV[i] = samples[i] * 1.024 / 32767;
        Plotly.restyle(`dynamic-time-${f}`, { y: [mV] });
    }
}

function renderFFT(fftData) {
    if (!fftData) return;
    for (let f = 0; f < 2; f++) {
        if (fftData[f]) {
            Plotly.restyle(`dynamic-fft-${f}`, { y: [fftData[f]] });
        }
    }
}

// --- Dynamic Time-Domain (demo tab) ---

// One plot per finger, sitting under that finger's pad, so each keeps its own
// colour rather than needing a legend.
const FINGER_COLORS = ['#4fa3e3', '#f2a541'];

// The dynamic signal spans four orders of magnitude between a brush of a
// fingertip and a knock, so a fixed axis either flattens the quiet end or
// clips the loud one. The axis follows the window's own peak instead, but
// never closes tighter than +-DYN_MIN_HALF_RANGE. The floor trades hiding the
// noise against sensitivity: a finger at rest measures about +-0.007 mV, which
// at +-0.1 mV is a thin band, so a still finger looks still while a light touch
// still clearly rises out of it. 0.1 was the best compromise on the bench.
const DYN_FULL_SCALE_MV = 1.024;    // what a full-scale sample is worth
const DYN_MIN_HALF_RANGE = 0.1;     // mV, the floor
const DYN_HEADROOM = 1.15;          // keep the peak off the frame edge
const dynRange = [0, 0];            // what each chart is currently showing

// Snapping to a 1-2-5 ladder keeps the axis from creeping a little on every
// frame, which reads as the trace breathing rather than as the scale
// changing. From the floor to full scale is a decade, so even steps would be
// either too coarse at the quiet end or too many at the loud one. The ladder
// starts at the floor: a smaller step would zoom in past it.
const DYN_RANGE_LADDER = [0.02, 0.05, 0.1, 0.2, 0.5, DYN_FULL_SCALE_MV]
    .filter(step => step >= DYN_MIN_HALF_RANGE);

function initOverviewDynamicChart(divId, finger) {
    Plotly.newPlot(divId, [{
        y: [], type: 'scattergl', mode: 'lines',
        line: { width: 1, color: FINGER_COLORS[finger] }
    }], overviewBaseLayout({
        xaxis: Object.assign({}, OVERVIEW_AXIS_STYLE, { showticklabels: false }),
        yaxis: Object.assign({}, OVERVIEW_AXIS_STYLE,
                             { range: [-DYN_MIN_HALF_RANGE, DYN_MIN_HALF_RANGE] }),
        margin: { t: 18, b: 20, l: 36, r: 6 }
    }), PLOTLY_CONFIG);
    dynRange[finger] = DYN_MIN_HALF_RANGE;
}

function renderOverviewDynamic(dynData) {
    if (!dynData) return;
    for (let f = 0; f < 2; f++) {
        const samples = dynData[f];
        const mV = new Float32Array(samples.length);
        let peak = 0;
        for (let i = 0; i < samples.length; i++) {
            mV[i] = samples[i] * DYN_FULL_SCALE_MV / 32767;
            if (Math.abs(mV[i]) > peak) peak = Math.abs(mV[i]);
        }
        Plotly.restyle(`overview-dynamic-${f}`, { y: [mV] });

        // The ladder ends at full scale: no reading can land outside it, so a
        // wider axis would only add empty space.
        const wanted = peak * DYN_HEADROOM;
        const half = DYN_RANGE_LADDER.find(step => step >= wanted) ?? DYN_FULL_SCALE_MV;
        if (half !== dynRange[f]) {
            dynRange[f] = half;
            Plotly.relayout(`overview-dynamic-${f}`, { 'yaxis.range': [-half, half] });
        }
    }
}

// --- IMU ---

const imuRange = {};  // global min/max per chart: { divId: { min, max } }

function initIMUChart(divId, yTitle) {
    imuRange[divId] = { min: Infinity, max: -Infinity };
    Plotly.newPlot(divId, ['X', 'Y', 'Z'].map((axis, i) => ({
        y: [], name: axis, type: 'scattergl', mode: 'lines',
        line: { width: 1, color: IMU_COLORS[i] }
    })), {
        xaxis: { title: 'Sample' },
        yaxis: { title: yTitle },
        margin: COMPACT_MARGIN,
        legend: { orientation: 'h', y: 1.12 }
    }, PLOTLY_CONFIG);
}

function renderIMU(accelData, gyroData) {
    if (!accelData || !gyroData) return;
    for (let f = 0; f < 2; f++) {
        renderIMUChart(`imu-accel-${f}`, accelData[f]);
        renderIMUChart(`imu-gyro-${f}`, gyroData[f]);
    }
}

function renderIMUChart(divId, data) {
    if (!data || data.x.length === 0) return;
    Plotly.restyle(divId, { y: [data.x, data.y, data.z] });
    // Update global min/max
    const r = imuRange[divId];
    for (const arr of [data.x, data.y, data.z]) {
        for (const v of arr) {
            if (v < r.min) r.min = v;
            if (v > r.max) r.max = v;
        }
    }
    const pad = Math.max((r.max - r.min) * 0.05, 1);
    Plotly.relayout(divId, { 'yaxis.range': [r.min - pad, r.max + pad] });
}

function resetIMUAxes() {
    for (const divId in imuRange) {
        imuRange[divId] = { min: Infinity, max: -Infinity };
    }
}

// --- Tab Switching ---

// Charts are created the first time their tab is revealed. Doing it at load
// would build fourteen plots — ten of them scattergl, i.e. ten WebGL contexts
// against a browser limit of around sixteen — for views that may never be
// opened, and Plotly sizes a chart wrongly if its container is display:none.
const tabsReady = new Set();

function initTab(tab) {
    if (tabsReady.has(tab)) return;
    tabsReady.add(tab);
    for (let f = 0; f < 2; f++) {
        switch (tab) {
            case 'overview':
                initOverviewStaticChart(`overview-static-${f}`);
                initOverviewDynamicChart(`overview-dynamic-${f}`, f);
                break;
            case 'dynamic':
                initDynamicChart(`dynamic-time-${f}`);
                initFFTChart(`dynamic-fft-${f}`);
                Plotly.restyle(`dynamic-fft-${f}`, { x: [FFT_FREQS] });
                break;
            case 'imu':
                initIMUChart(`imu-accel-${f}`, 'Accel');
                initIMUChart(`imu-gyro-${f}`, 'Gyro');
                break;
        }
    }
}

// Each scattergl plot holds a WebGL context, and browsers keep only so many
// across the whole process -- other pages included. Holding all three tabs'
// plots open meant ten of them plus the gripper's, and the one the browser
// dropped under pressure could be the gripper's, which then just went blank.
// Only the visible tab keeps its plots.
function purgeTab(tab) {
    if (!tabsReady.has(tab)) return;
    document.querySelectorAll(`#tab-${tab} .js-plotly-plot`)
            .forEach(el => Plotly.purge(el));
    tabsReady.delete(tab);
}

function switchTab(tab) {
    if (tab !== activeTab) purgeTab(activeTab);
    document.querySelectorAll('.tab').forEach(b => b.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(tc => tc.classList.remove('active'));
    document.querySelector(`.tab[data-tab="${tab}"]`).classList.add('active');
    document.getElementById(`tab-${tab}`).classList.add('active');
    activeTab = tab;

    // Controls that only mean something on one tab.
    document.querySelectorAll('[data-tabs]').forEach(el => {
        el.hidden = !el.dataset.tabs.split(' ').includes(tab);
    });

    // The section is visible now, so its cells have a real size to build into.
    initTab(tab);
    scheduleResize();
    if (gripper) gripper.setRunning(tab === 'overview');
    send({ type: 'tab_change', tab });
}

// --- 3D gripper (opt-in) ---

// Roughly 3 MB of three.js and baked meshes, which the default page must not
// pay for: the module is imported the first time the box is ticked, and the
// import is what pulls in three, OrbitControls and the geometry.
const GRIPPER_KEY = 'viewer.gripper3d';
let gripper = null;          // the module namespace, once resolved
let gripperLoading = null;   // in-flight import, so a double-click loads once

async function enableGripper() {
    document.body.classList.add('gripper-on');
    if (!gripperLoading) {
        gripperLoading = (async () => {
            const m = await import('./gripper3d.js');
            await m.initGripperView(document.getElementById('gripper-view'));
            return m;
        })();
    }
    try {
        gripper = await gripperLoading;
    } catch (err) {
        // Loading the model used to be fire-and-forget, so a missing mesh file
        // failed silently. Keep the column so the failure has somewhere to be
        // said, and let unticking and re-ticking retry.
        gripperLoading = null;
        const note = document.querySelector('.gripper-error');
        note.textContent = `3D gripper failed to load: ${err.message}`;
        note.hidden = false;
        return;
    }
    document.querySelector('.gripper-error').hidden = true;
    gripper.setRunning(activeTab === 'overview');
    gripper.resizeGripperView();
}

// The renderer holds a context that is gone for good, so throw it away and
// build another. Bounded, because if the browser simply has no context to give
// then retrying forever would only spin.
const GRIPPER_MAX_REBUILDS = 2;
let gripperRebuilds = 0;

async function rebuildGripper() {
    if (!gripper) return;
    gripper.disposeGripperView();
    gripper = null;
    gripperLoading = null;
    if (!document.getElementById('gripper-3d').checked) return;
    if (gripperRebuilds >= GRIPPER_MAX_REBUILDS) {
        const note = document.querySelector('.gripper-error');
        note.textContent = 'The 3D graphics context keeps being lost. ' +
            'Closing other browser tabs that use 3D usually frees one; ' +
            'untick and re-tick to try again.';
        note.hidden = false;
        return;
    }
    gripperRebuilds++;
    await enableGripper();
}

document.getElementById('gripper-view')
        .addEventListener('gripper-context-lost', rebuildGripper);

function setGripper(on) {
    try { localStorage.setItem(GRIPPER_KEY, on ? '1' : '0'); } catch (e) { /* private mode */ }
    if (on) {
        gripperRebuilds = 0;
        enableGripper();
    } else {
        document.body.classList.remove('gripper-on');
        // Keep the context and meshes so re-enabling is instant and needs no
        // second download; just stop drawing.
        if (gripper) gripper.setRunning(false);
    }
    // The column appears and disappears with the option, so the two pads and
    // their traces change width and Plotly has to be told.
    scheduleResize();
}

// --- Gripper control ---

// The server owns the command, so two browsers on one viewer show the same
// sliders. A slider being dragged is left alone, though: the frame echoing the
// previous value can arrive mid-drag and would yank it back.
const GC_KEYS = ['position', 'speed', 'force'];
const GC_ECHO_GRACE_MS = 500;
const gcTouched = {};   // key -> time of the last local change

// gOBJ, the gripper's own account of how the last move ended, as the object
// indicator shows it: [label, state class].
const GC_OBJECT = [
    ['moving', 'moving'],
    ['object detected (opening)', 'detected'],
    ['object detected', 'detected'],
    ['no object', 'none'],
];

// Every slider is a 0-255 byte on the wire. Position reads as how closed the
// gripper is: 0% fully open, 100% fully closed.
function gcPercent(v) {
    return v == null ? '–' : `${Math.round(v * 100 / 255)}%`;
}

function renderObjectIndicator(g) {
    const el = document.getElementById('gc-object');
    const [label, cls] = (g.activated && !g.activating && GC_OBJECT[g.object])
        || ['object: –', 'unknown'];
    el.className = `gc-object ${cls}`;
    document.getElementById('gc-object-text').textContent = label;
}

function renderGripperControl(g) {
    // Every overview frame carries the key; null means no gripper to drive.
    const on = !!g;
    if (document.body.classList.contains('gripper-ctl-on') !== on) {
        document.body.classList.toggle('gripper-ctl-on', on);
        scheduleResize();
    }
    if (!on) return;

    const ready = g.activated && !g.activating;
    const now = performance.now();
    for (const key of GC_KEYS) {
        const slider = document.getElementById(`gc-${key}`);
        slider.disabled = !ready;
        const v = g.command[key];
        if (v != null && !(now - (gcTouched[key] || -Infinity) < GC_ECHO_GRACE_MS)) {
            slider.value = v;
            document.getElementById(`gc-${key}-value`).textContent = gcPercent(v);
        }
    }

    const button = document.getElementById('gc-activate');
    button.hidden = g.activated && !g.activating;
    button.disabled = g.activating;
    button.textContent = g.activating ? 'Activating…' : 'Activate';

    const state = document.getElementById('gc-state');
    let text, bad = false;
    if (g.error) { text = g.error; bad = true; }
    else if (g.fault) { text = `fault 0x${g.fault.toString(16).toUpperCase()}`; bad = true; }
    else if (g.activating) text = 'activating: the fingers open and close fully';
    else if (!g.activated) text = 'not activated';
    else {
        text = `at ${gcPercent(g.position)} closed`;
    }
    state.textContent = `${g.port} · ${text}`;
    state.classList.toggle('bad', bad);
    renderObjectIndicator(g);
}

for (const key of GC_KEYS) {
    const slider = document.getElementById(`gc-${key}`);
    slider.addEventListener('input', () => {
        gcTouched[key] = performance.now();
        const v = parseInt(slider.value);
        document.getElementById(`gc-${key}-value`).textContent = gcPercent(v);
        // The server sends only the latest request, so a fast drag cannot
        // queue up a backlog of moves on the gripper.
        send({ type: 'gripper_move', [key]: v });
    });
}

document.getElementById('gc-activate').addEventListener('click', () => {
    if (confirm('Activation fully opens and closes the gripper.\n' +
                'Make sure nothing is between the fingers.'))
        send({ type: 'gripper_activate' });
});

// --- Gripper orientation ---

// Detected on the server from gravity at the fingertip IMUs (see
// TIP_ORIENTATION_MIN_Y in web_viewer.py); the page only reports it.
const ORIENTATION_TEXT = { up: 'fingers pointing up', down: 'fingers pointing down' };

function renderOrientation(seen) {
    // Per finger: 'up', 'down', 'side', or null while a finger has nothing to
    // go on yet (no IMU data, or its startup calibration still running).
    // Usually both agree; a finger with no reading defers to the other.
    const views = seen || [];
    const known = [...new Set(views.filter(v => v === 'up' || v === 'down'))];
    let text = '', bad = false;
    if (known.length > 1) { text = 'Detected: fingers disagree'; bad = true; }
    else if (views.includes('side')) { text = 'Detected: on its side'; bad = true; }
    else if (known.length === 1) text = `Detected: ${ORIENTATION_TEXT[known[0]]}`;

    const detected = document.getElementById('orientation-detected');
    detected.textContent = text;
    detected.classList.toggle('bad', bad);
    // The warning is only shown when the tilt cannot be measured; the angle
    // readouts then show "no ref".
    document.getElementById('orientation-note').hidden = !bad;
}

// --- Resizing ---

// Plotly's built-in `responsive` config relies on its own ResizeObserver, which
// doesn't reliably re-fire when a CSS aspect-ratio/vh-driven container grows back
// after shrinking. Force a resize pass ourselves.
let resizeTimeout = null;

function resizeAllPlots() {
    // Only the visible tab: Plotly resizes a display:none plot to nonsense.
    document.querySelectorAll('.tab-content.active .js-plotly-plot').forEach(el => {
        Plotly.Plots.resize(el);
        // The static heatmaps use scaleanchor/constrain to lock a 4:7 aspect
        // ratio; repeated resize passes can drift the axis range instead of
        // just the domain, so pin it back to the sensor's fixed grid each time.
        if (el.classList.contains('heatmap'))
            Plotly.relayout(el, { 'xaxis.range': [0, 4], 'yaxis.range': [7, 0] });
    });
}

function scheduleResize() {
    clearTimeout(resizeTimeout);
    resizeTimeout = setTimeout(resizeAllPlots, 100);
}

window.addEventListener('resize', scheduleResize);
// <main> rather than a specific layout's container: it is present on every tab,
// so this cannot go looking for an element the active tab does not have.
const mainEl = document.querySelector('main');
if (mainEl) new ResizeObserver(scheduleResize).observe(mainEl);

// --- Controls ---

document.querySelectorAll('.tab').forEach(btn =>
    btn.addEventListener('click', () => switchTab(btn.dataset.tab)));

document.getElementById('reset-baseline').addEventListener('click',
    () => send({ type: 'reset_baseline' }));

// The sensor carries the gripper's weight, and that load changes with the
// gripper's orientation — so re-zeroing is a thing you do, not a one-off.
document.getElementById('zero-wrench').addEventListener('click',
    () => send({ type: 'zero_wrench' }));

document.getElementById('raw-values').addEventListener('change',
    (e) => send({ type: 'set_raw_mode', raw: e.target.checked }));

document.getElementById('adaptive-range').addEventListener('change',
    (e) => send({ type: 'set_adaptive_range', adaptive: e.target.checked }));

document.getElementById('reset-imu-axes')?.addEventListener('click', resetIMUAxes);

document.getElementById('heatmap-mode').addEventListener('change',
    (e) => setHeatmapMode(e.target.value));

document.getElementById('gripper-3d').addEventListener('change',
    (e) => setGripper(e.target.checked));

// --- Init ---

// Pre-compute FFT x-axis
const FFT_FREQS = new Float64Array(2048);
for (let i = 0; i < 2048; i++) FFT_FREQS[i] = i * 500 / 2048;

function init() {
    // Before the first switchTab, so the heatmaps are built in the saved mode.
    const modeSelect = document.getElementById('heatmap-mode');
    try {
        if (localStorage.getItem(HEATMAP_MODE_KEY) === 'interpolated')
            heatmapMode = 'interpolated';
    } catch (e) { /* private mode */ }
    modeSelect.value = heatmapMode;

    // The demo tab is always the one that comes up, deliberately: this is the
    // view a booth machine should be showing after a reboot, so the last tab
    // someone poked at is not remembered.
    switchTab('overview');

    const box = document.getElementById('gripper-3d');
    let saved = null;
    try { saved = localStorage.getItem(GRIPPER_KEY); } catch (e) { /* private mode */ }
    box.checked = saved === '1';
    if (box.checked) enableGripper();

    connect();
}

init();
