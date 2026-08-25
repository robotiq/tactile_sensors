// Robotiq Tactile Sensor Web Viewer
// WebSocket client + Plotly.js chart rendering.
// All sensors are shown on a single page — the server streams every stream on
// every frame, so there is no tab state to keep in sync.

"use strict";

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

const IMU_COLORS = ['#1f77b4', '#ff7f0e', '#2ca02c'];

// Tight chrome: with 10 charts on screen every pixel of plot area counts.
const COMPACT_MARGIN = { t: 6, b: 20, l: 36, r: 6 };
const AXIS_STYLE = {
    gridcolor: '#243b6b',
    zerolinecolor: '#2f4a86',
    linecolor: '#2f4a86',
    tickfont: { size: 9 },
    automargin: false
};

function baseLayout(extra) {
    return Object.assign({
        paper_bgcolor: 'rgba(0,0,0,0)',
        plot_bgcolor: '#0e1630',
        font: { color: '#8fa0c0', size: 9 },
        margin: COMPACT_MARGIN,
        showlegend: false
    }, extra);
}

let ws = null;
let frameCount = 0;

// --- WebSocket ---

function connect() {
    const wsPort = parseInt(location.port) + WS_PORT_OFFSET;
    ws = new WebSocket(`ws://${location.hostname}:${wsPort}`);
    ws.onopen = () => {
        document.getElementById('connection-status').textContent = 'Connected';
        document.getElementById('connection-status').className = 'status-connected';
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
    renderStatic(msg.static, msg.maxRange);
    renderDynamic(msg.dynamic);
    renderIMU(msg.accel, msg.gyro);
}

// --- Static Heatmaps ---

function initStaticChart(divId) {
    Plotly.newPlot(divId, [{
        // Cell centers at i+0.5 so each sensor cell spans exactly one integer-to-integer unit (e.g. column 0 = [0,1])
        x: [0.5, 1.5, 2.5, 3.5],
        y: [0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5],
        z: Array(7).fill(null).map(() => Array(4).fill(0)),
        type: 'heatmap',
        colorscale: TACTILE_COLORSCALE,
        zsmooth: 'best',
        zmin: 0, zmax: 3000,
        colorbar: { thickness: 7, outlinewidth: 0, tickfont: { size: 8 }, len: 1 }
    }], baseLayout({
        xaxis: Object.assign({}, AXIS_STYLE, { dtick: 1, range: [0, 4] }),
        yaxis: Object.assign({}, AXIS_STYLE, {
            dtick: 1, range: [7, 0], scaleanchor: 'x', scaleratio: 1, constrain: 'domain'
        }),
        margin: { t: 6, b: 18, l: 20, r: 4 }
    }), PLOTLY_CONFIG);
}

function renderStatic(data, maxRanges) {
    if (!data) return;
    for (let f = 0; f < 2; f++) {
        const z = [];
        for (let row = 0; row < 7; row++)
            z.push(data[f].slice(row * 4, (row + 1) * 4));
        Plotly.restyle(`static-finger-${f}`, { z: [z], zmax: Math.max(maxRanges[f], 1) });
    }
}

// --- Dynamic Time-Domain + FFT ---

function initDynamicChart(divId) {
    Plotly.newPlot(divId, [{
        y: [], type: 'scattergl', mode: 'lines',
        line: { width: 1, color: '#1f77b4' }
    }], baseLayout({
        xaxis: Object.assign({}, AXIS_STYLE, { showticklabels: false }),
        yaxis: Object.assign({}, AXIS_STYLE, { range: [-1, 1] })
    }), PLOTLY_CONFIG);
}

function initFFTChart(divId) {
    Plotly.newPlot(divId, [{
        y: [], type: 'scattergl', mode: 'lines',
        line: { width: 1, color: '#ff7f0e' }
    }], baseLayout({
        xaxis: Object.assign({}, AXIS_STYLE, {
            type: 'log', range: [Math.log10(0.5), Math.log10(500)]
        }),
        yaxis: AXIS_STYLE
    }), PLOTLY_CONFIG);
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

// --- IMU ---

const imuRange = {};  // global min/max per chart: { divId: { min, max } }

function initIMUChart(divId) {
    imuRange[divId] = { min: Infinity, max: -Infinity };
    // Legend lives in the column header (see .axis-key in style.css) to keep
    // the plot area as tall as possible.
    Plotly.newPlot(divId, ['X', 'Y', 'Z'].map((axis, i) => ({
        y: [], name: axis, type: 'scattergl', mode: 'lines',
        line: { width: 1, color: IMU_COLORS[i] }
    })), baseLayout({
        xaxis: Object.assign({}, AXIS_STYLE, { showticklabels: false }),
        yaxis: AXIS_STYLE
    }), PLOTLY_CONFIG);
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

// --- Resizing ---

// Plotly's built-in `responsive` config relies on its own ResizeObserver, which
// doesn't reliably re-fire when a grid cell grows back after shrinking. Observe
// the cells ourselves and force a resize pass.
let resizeTimeout = null;
function resizeAllPlots() {
    document.querySelectorAll('.js-plotly-plot').forEach(el => {
        Plotly.Plots.resize(el);
        // The static heatmaps use scaleanchor/constrain to lock a 4:7 aspect
        // ratio; repeated resize passes can drift the axis range instead of
        // just the domain, so pin it back to the sensor's fixed grid each time.
        if (el.id.startsWith('static-finger-'))
            Plotly.relayout(el, { 'xaxis.range': [0, 4], 'yaxis.range': [7, 0] });
    });
}

function scheduleResize() {
    clearTimeout(resizeTimeout);
    resizeTimeout = setTimeout(resizeAllPlots, 100);
}

window.addEventListener('resize', scheduleResize);
new ResizeObserver(scheduleResize).observe(document.querySelector('.grid'));

// --- Controls ---

document.getElementById('reset-baseline').addEventListener('click',
    () => send({ type: 'reset_baseline' }));

document.getElementById('raw-values').addEventListener('change',
    (e) => send({ type: 'set_raw_mode', raw: e.target.checked }));

document.getElementById('adaptive-range').addEventListener('change',
    (e) => send({ type: 'set_adaptive_range', adaptive: e.target.checked }));

document.getElementById('reset-imu-axes').addEventListener('click', resetIMUAxes);

// --- Init ---

// Pre-compute FFT x-axis
const FFT_FREQS = new Float64Array(2048);
for (let i = 0; i < 2048; i++) FFT_FREQS[i] = i * 500 / 2048;

function init() {
    for (let f = 0; f < 2; f++) {
        initStaticChart(`static-finger-${f}`);
        initDynamicChart(`dynamic-time-${f}`);
        initFFTChart(`dynamic-fft-${f}`);
        initIMUChart(`imu-accel-${f}`);
        initIMUChart(`imu-gyro-${f}`);
        Plotly.restyle(`dynamic-fft-${f}`, { x: [FFT_FREQS] });
    }
    connect();
}

init();
