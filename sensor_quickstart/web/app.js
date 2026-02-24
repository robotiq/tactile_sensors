// Robotiq Tactile Sensor Web Viewer
// WebSocket client + Plotly.js chart rendering

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

const COMPACT_MARGIN = { t: 10, b: 40, l: 50, r: 20 };
const IMU_COLORS = ['#1f77b4', '#ff7f0e', '#2ca02c'];

let ws = null;
let activeTab = 'static';
let frameCount = 0;

// --- WebSocket ---

function connect() {
    const wsPort = parseInt(location.port) + WS_PORT_OFFSET;
    ws = new WebSocket(`ws://${location.hostname}:${wsPort}`);
    ws.onopen = () => {
        document.getElementById('connection-status').textContent = 'Connected';
        document.getElementById('connection-status').className = 'status-connected';
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

// --- Data Handling (just render server snapshots directly) ---

function handleData(msg) {
    frameCount++;
    document.getElementById('sample-count').textContent = `Frame: ${frameCount}`;
    switch (msg.tab) {
        case 'static':  renderStatic(msg.static, msg.maxRange); break;
        case 'dynamic': renderDynamic(msg.dynamic); break;
        case 'imu':     renderIMU(msg.accel, msg.gyro); break;
    }
}

// --- Static Heatmaps ---

function initStaticChart(divId) {
    Plotly.newPlot(divId, [{
        z: Array(7).fill(null).map(() => Array(4).fill(0)),
        type: 'heatmap',
        colorscale: TACTILE_COLORSCALE,
        zsmooth: 'best',
        zmin: 0, zmax: 3000,
        colorbar: { title: 'Pressure', len: 0.9 }
    }], {
        xaxis: { title: 'Column', dtick: 1 },
        yaxis: { title: 'Row', dtick: 1, autorange: 'reversed' },
        margin: COMPACT_MARGIN
    }, PLOTLY_CONFIG);
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

function switchTab(tab) {
    document.querySelectorAll('.tab').forEach(b => b.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(tc => tc.classList.remove('active'));
    document.querySelector(`.tab[data-tab="${tab}"]`).classList.add('active');
    document.getElementById(`tab-${tab}`).classList.add('active');
    activeTab = tab;
    if (ws && ws.readyState === WebSocket.OPEN)
        ws.send(JSON.stringify({ type: 'tab_change', tab }));
}

// --- Controls ---

document.querySelectorAll('.tab').forEach(btn =>
    btn.addEventListener('click', () => switchTab(btn.dataset.tab)));

document.getElementById('reset-baseline').addEventListener('click', () => {
    if (ws && ws.readyState === WebSocket.OPEN)
        ws.send(JSON.stringify({ type: 'reset_baseline' }));
});

document.getElementById('raw-values').addEventListener('change', (e) => {
    if (ws && ws.readyState === WebSocket.OPEN)
        ws.send(JSON.stringify({ type: 'set_raw_mode', raw: e.target.checked }));
});

document.getElementById('adaptive-range').addEventListener('change', (e) => {
    if (ws && ws.readyState === WebSocket.OPEN)
        ws.send(JSON.stringify({ type: 'set_adaptive_range', adaptive: e.target.checked }));
});

document.getElementById('reset-imu-axes')?.addEventListener('click', resetIMUAxes);

// --- Init ---

// Pre-compute FFT x-axis
const FFT_FREQS = new Float64Array(2048);
for (let i = 0; i < 2048; i++) FFT_FREQS[i] = i * 500 / 2048;

function init() {
    for (let f = 0; f < 2; f++) {
        initStaticChart(`static-finger-${f}`);
        initDynamicChart(`dynamic-time-${f}`);
        initFFTChart(`dynamic-fft-${f}`);
        initIMUChart(`imu-accel-${f}`, 'Accel');
        initIMUChart(`imu-gyro-${f}`, 'Gyro');
        Plotly.restyle(`dynamic-fft-${f}`, { x: [FFT_FREQS] });
    }
    connect();
}

init();
