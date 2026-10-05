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
        sendRefreshRate();
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
    // The server hears about a tab switch a round trip late, so a frame for the
    // tab just left is usually still in flight, and its plots are already
    // purged: restyling them would throw out of onmessage.
    if (msg.tab !== activeTab || !tabsReady.has(msg.tab)) return;
    frameCount++;
    document.getElementById('sample-count').textContent = `Frame: ${frameCount}`;
    switch (msg.tab) {
        case 'overview':
            renderHeatmaps(msg.static, msg.maxRange);
            renderOverviewDynamic(msg.dynamic);
            if (gripper) {
                gripper.renderGripper(msg.tipAngle, msg.tipAngleValid);
                gripper.renderWrench(msg.wrench, msg.wrenchError, msg.ftOrigin);
            }
            break;
        case 'dynamic': renderDynamic(msg.dynamic); break;
        case 'imu':     renderIMU(msg.accel, msg.gyro); break;
    }
}

// --- Static Heatmaps ---

function renderHeatmaps(data, maxRanges) {
    if (!data) return;
    for (let f = 0; f < 2; f++) {
        const z = [];
        for (let row = 0; row < 7; row++)
            z.push(data[f].slice(row * 4, (row + 1) * 4));
        Plotly.restyle(`overview-static-${f}`, { z: [z], zmax: Math.max(maxRanges[f], 1) });
    }
}

function initOverviewStaticChart(divId) {
    Plotly.newPlot(divId, [{
        x: [0.5, 1.5, 2.5, 3.5],
        y: [0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5],
        z: Array(7).fill(null).map(() => Array(4).fill(0)),
        type: 'heatmap',
        colorscale: TACTILE_COLORSCALE,
        zsmooth: 'best',
        zmin: 0, zmax: 3000,
        colorbar: { thickness: 6, outlinewidth: 0, tickfont: { size: 8 }, len: 1, x: 1.02 }
    }], overviewBaseLayout({
        xaxis: Object.assign({}, OVERVIEW_AXIS_STYLE,
                             { dtick: 1, range: [0, 4], constrain: 'domain' }),
        yaxis: Object.assign({}, OVERVIEW_AXIS_STYLE, {
            dtick: 1, range: [7, 0], scaleanchor: 'x', scaleratio: 1, constrain: 'domain'
        }),
        margin: { t: 6, b: 18, l: 18, r: 0 }
    }), PLOTLY_CONFIG);
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
    // Same race as handleData: the spectrum can arrive after leaving the tab.
    if (!fftData || !tabsReady.has('dynamic')) return;
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
// never closes tighter than +-DYN_MIN_HALF_RANGE: below that the trace is
// noise, and zooming into noise makes a still finger look busy.
const DYN_FULL_SCALE_MV = 1.024;    // what a full-scale sample is worth
const DYN_MIN_HALF_RANGE = 0.5;     // mV, the floor
const DYN_HEADROOM = 1.15;          // keep the peak off the frame edge
const dynRange = [0, 0];            // what each chart is currently showing

// Snapping to a step keeps the axis from creeping a little on every frame,
// which reads as the trace breathing rather than as the scale changing. The
// step is 0.1 mV rather than a 1/2/5 ladder because the whole span from the
// floor to full scale is only 0.5 mV wide — a ladder would have two rungs in it.
const DYN_RANGE_STEP = 0.1;

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

        // Clamped at full scale: no reading can land outside it, so a wider
        // axis would only add empty space.
        const wanted = Math.ceil(peak * DYN_HEADROOM / DYN_RANGE_STEP) * DYN_RANGE_STEP;
        const half = Math.min(Math.max(wanted, DYN_MIN_HALF_RANGE), DYN_FULL_SCALE_MV);
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
let gripperRebuildPending = false;  // context lost before the load finished

async function enableGripper() {
    document.body.classList.add('gripper-on');
    if (!gripperLoading) {
        gripperLoading = (async () => {
            const m = await import('./gripper3d.js');
            try {
                await m.initGripperView(document.getElementById('gripper-view'));
            } catch (err) {
                // Leave nothing half-built for the retry: link groups still
                // attached to this scene, or a renderer and its WebGL context.
                m.disposeGripperView();
                throw err;
            }
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
        gripperRebuildPending = false;
        const note = document.querySelector('.gripper-error');
        note.textContent = `3D gripper failed to load: ${err.message}`;
        note.hidden = false;
        return;
    }
    if (gripperRebuildPending) {
        gripperRebuildPending = false;
        await rebuildGripper();
        return;
    }
    // The box may have been unticked while the module was loading, and
    // initGripperView starts the render loop regardless.
    const on = document.getElementById('gripper-3d').checked;
    gripper.setRunning(on && activeTab === 'overview');
    if (!on) return;
    document.querySelector('.gripper-error').hidden = true;
    gripper.resizeGripperView();
}

// The renderer holds a context that is gone for good, so throw it away and
// build another. Bounded, because if the browser simply has no context to give
// then retrying forever would only spin.
const GRIPPER_MAX_REBUILDS = 2;
let gripperRebuilds = 0;

async function rebuildGripper() {
    if (!gripper) {
        // Lost while still loading: let the load finish, then rebuild from
        // there (see enableGripper), rather than drop the event.
        if (gripperLoading) gripperRebuildPending = true;
        return;
    }
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

document.getElementById('gripper-3d').addEventListener('change',
    (e) => setGripper(e.target.checked));

// --- Refresh rate ---

// Per browser, like the tab: a booth PC can run at 30 Hz while a sales
// laptop on the same viewer stays at 5.
const REFRESH_KEY = 'viewer.refreshHz';

function sendRefreshRate() {
    send({ type: 'set_rate', hz: Number(document.getElementById('refresh-rate').value) });
}

document.getElementById('refresh-rate').addEventListener('change', (e) => {
    try { localStorage.setItem(REFRESH_KEY, e.target.value); } catch (err) { /* private mode */ }
    sendRefreshRate();
});

// --- Init ---

// Pre-compute FFT x-axis
const FFT_FREQS = new Float64Array(2048);
for (let i = 0; i < 2048; i++) FFT_FREQS[i] = i * 500 / 2048;

function init() {
    // The demo tab is always the one that comes up, deliberately: this is the
    // view a booth machine should be showing after a reboot, so the last tab
    // someone poked at is not remembered.
    switchTab('overview');

    const box = document.getElementById('gripper-3d');
    let saved = null;
    try { saved = localStorage.getItem(GRIPPER_KEY); } catch (e) { /* private mode */ }
    box.checked = saved === '1';
    if (box.checked) enableGripper();

    const rate = document.getElementById('refresh-rate');
    let savedHz = null;
    try { savedHz = localStorage.getItem(REFRESH_KEY); } catch (e) { /* private mode */ }
    if ([...rate.options].some(o => o.value === savedHz)) rate.value = savedHz;

    connect();
}

init();
