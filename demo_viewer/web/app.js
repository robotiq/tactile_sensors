// Robotiq Tactile Sensor Web Viewer
// WebSocket client + Plotly.js chart rendering.
// All sensors are shown on a single page — the server streams every stream on
// every frame, so there is no tab state to keep in sync.

import * as THREE from 'three';
import { OrbitControls } from './vendor/OrbitControls.js';

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

// Tight chrome: the plots share the page with the gripper drawing.
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
    renderGripper(msg.tipAngle, msg.tipAngleValid);
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

// --- Dynamic Time-Domain ---

// Both fingers share one plot, so their traces are colour-coded in the column
// header (see .finger-key in style.css) rather than by a legend inside it.
const FINGER_COLORS = ['#4fa3e3', '#f2a541'];

function initDynamicChart(divId) {
    Plotly.newPlot(divId, FINGER_COLORS.map((color, f) => ({
        y: [], name: `F${f}`, type: 'scattergl', mode: 'lines',
        line: { width: 1, color }
    })), baseLayout({
        xaxis: Object.assign({}, AXIS_STYLE, { showticklabels: false }),
        yaxis: Object.assign({}, AXIS_STYLE, { range: [-1, 1] })
    }), PLOTLY_CONFIG);
}

function renderDynamic(dynData) {
    if (!dynData) return;
    const traces = [];
    for (let f = 0; f < 2; f++) {
        const samples = dynData[f];
        const mV = new Float32Array(samples.length);
        for (let i = 0; i < samples.length; i++) mV[i] = samples[i] * 1.024 / 32767;
        traces.push(mV);
    }
    Plotly.restyle('dynamic-time', { y: traces });
}

// --- Gripper view ---

// Meshes and the five-bar pivots come from GRIPPER_GEOMETRY (gripper_geometry.js),
// generated from the 2F-85 ROS meshes and the Isaac Sim compliant model. The
// linkage is planar, so all of this is the same maths the flat view used: the
// drawing plane's x is the gripper's x, its y is -z, and a rotation in that
// plane is a rotation about the gripper's y axis by the same angle.
//
// With the drive held at fully open the loop reduces to a four-bar grounded at
// P2 and P5 whose coupler is the distal phalanx — the link the fingertip IMU
// measures. Its angle closes the mechanism; the rest follows in closed form.

const MM = 0.001;   // the meshes are in mm; the scene works in metres
const FINGER_TO_SIDE = ['left', 'right'];
// The server reports "inward", the same number for both fingers in a symmetric
// grasp. The fingertips face each other, so inward is opposite in the shared
// frame — that mirroring belongs here, in the view.
const TIP_SCREEN_SIGN = { left: 1, right: -1 };

// metalness stays at zero throughout: without an environment map to reflect,
// a metallic MeshStandardMaterial renders almost black.
const MATERIALS = {
    metal:  { color: 0x9fb0d2, roughness: 0.5, metalness: 0.0 },
    rubber: { color: 0x39415a, roughness: 0.9, metalness: 0.0 },
};
// The distal phalanges are the live part, so they are tinted rather than left
// the same grey as the body they hang off.
const TIP_MATERIALS = {
    metal:  { color: 0xe4667f, roughness: 0.45, metalness: 0.0 },
    rubber: { color: 0x8d2440, roughness: 0.9,  metalness: 0.0 },
};
const INVALID_TINT = 0x5b6480;

let scene, camera, renderer, controls;
const linkObjects = {};        // mesh name -> THREE.Object3D
const lastPose = [null, null]; // per finger, to hold when unreachable
let meshesReady = false;

const sub = (a, b) => [a[0] - b[0], a[1] - b[1]];
const add = (a, b) => [a[0] + b[0], a[1] + b[1]];
const len = (a) => Math.hypot(a[0], a[1]);
const angleOf = (a) => Math.atan2(a[1], a[0]);

function rotate(v, radians) {
    const c = Math.cos(radians), s = Math.sin(radians);
    return [v[0] * c - v[1] * s, v[0] * s + v[1] * c];
}

// Intersections of two circles, or null when they cannot meet.
function intersectCircles(centreA, radiusA, centreB, radiusB) {
    const between = sub(centreB, centreA);
    const distance = len(between);
    if (distance === 0) return null;
    if (distance > radiusA + radiusB || distance < Math.abs(radiusA - radiusB)) return null;
    const a = (radiusA * radiusA - radiusB * radiusB + distance * distance) / (2 * distance);
    const hSquared = radiusA * radiusA - a * a;
    if (hSquared < 0) return null;
    const h = Math.sqrt(hSquared);
    const unit = [between[0] / distance, between[1] / distance];
    const mid = add(centreA, [unit[0] * a, unit[1] * a]);
    const offset = [-unit[1] * h, unit[0] * h];
    return [add(mid, offset), sub(mid, offset)];
}

// Solve the four-bar for a distal phalanx turned by `radians` from fully open.
function solveLinkage(pivots, radians) {
    const { outerFinger: p2, distal: p3, coupler: p4, innerKnuckle: p5 } = pivots;
    const outerFingerLength = len(sub(p3, p2));
    const innerKnuckleLength = len(sub(p4, p5));
    const coupler = rotate(sub(p4, p3), radians);

    // P3 sits on a circle about P2, and — since P4 hangs off it by the now-known
    // coupler vector — on another about P5 shifted by that vector.
    const solutions = intersectCircles(p2, outerFingerLength,
                                       sub(p5, coupler), innerKnuckleLength);
    if (!solutions) return null;
    // Two branches; keep the one continuous with the pose we started from.
    const distal = len(sub(solutions[0], p3)) <= len(sub(solutions[1], p3))
        ? solutions[0] : solutions[1];

    return {
        distalShift: sub(distal, p3),
        outerFingerTurn: angleOf(sub(distal, p2)) - angleOf(sub(p3, p2)),
        innerKnuckleTurn: angleOf(sub(add(distal, coupler), p5))
                          - angleOf(sub(p4, p5)),
    };
}

// The pivots are stored in the flat view's coordinates (x right, y down). The
// scene keeps the gripper's own frame, where y is the joint axis and z is up.
const pivotToScene = (p) => new THREE.Vector3(p[0] * MM, 0, -p[1] * MM);

async function initGripperView() {
    const host = document.getElementById('gripper-view');
    if (!host || typeof GRIPPER_GEOMETRY === 'undefined') return;

    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x16213e);

    camera = new THREE.PerspectiveCamera(35, 1, 0.01, 10);
    // Start looking straight down the joint axis, which is the view the flat
    // drawing showed and the side the key light is on. The user can orbit away.
    camera.position.set(0, -0.34, 0.075);
    camera.up.set(0, 0, 1);

    renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(window.devicePixelRatio);
    host.appendChild(renderer.domElement);

    controls = new OrbitControls(camera, renderer.domElement);
    controls.target.set(0, 0, 0.075);
    controls.enablePan = false;
    controls.minDistance = 0.15;
    controls.maxDistance = 2.0;
    controls.update();

    scene.add(new THREE.HemisphereLight(0xbcd0ff, 0x2a3350, 1.6));
    const key = new THREE.DirectionalLight(0xffffff, 2.6);
    key.position.set(-0.4, -0.8, 0.9);
    scene.add(key);
    const fill = new THREE.DirectionalLight(0x94a8d8, 1.0);
    fill.position.set(0.6, -0.3, -0.4);
    scene.add(fill);

    await loadMeshes();
    resizeGripperView();
    renderer.setAnimationLoop(() => {
        controls.update();
        renderer.render(scene, camera);
    });
}

async function loadMeshes() {
    const response = await fetch(GRIPPER_GEOMETRY.meshFile);
    const blob = await response.arrayBuffer();

    for (const part of GRIPPER_GEOMETRY.meshes) {
        const positions = new Float32Array(blob, part.byteOffset, part.vertexCount * 3);
        const geometry = new THREE.BufferGeometry();
        // Scale to metres in place rather than scaling the objects, so the
        // pivots and the mesh share one set of units.
        const metres = new Float32Array(positions.length);
        for (let i = 0; i < positions.length; i++) metres[i] = positions[i] * MM;
        geometry.setAttribute('position', new THREE.BufferAttribute(metres, 3));
        // Un-indexed triangles, so this gives per-face normals: these are
        // machined parts and their edges are genuinely sharp.
        geometry.computeVertexNormals();

        const isTip = part.name.endsWith('_finger_tip');
        const spec = (isTip ? TIP_MATERIALS : MATERIALS)[part.cls];
        const mesh = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial(spec));
        mesh.userData.baseColor = spec.color;

        let group = linkObjects[part.name];
        if (!group) {
            group = new THREE.Group();
            group.matrixAutoUpdate = false;
            linkObjects[part.name] = group;
            scene.add(group);
        }
        group.add(mesh);
    }
    meshesReady = true;
}

function setLinkTransform(name, pivot, radians, shift) {
    const group = linkObjects[name];
    if (!group) return;
    const matrix = new THREE.Matrix4()
        .makeTranslation(pivot.x, pivot.y, pivot.z)
        .multiply(new THREE.Matrix4().makeRotationY(radians))
        .multiply(new THREE.Matrix4().makeTranslation(-pivot.x, -pivot.y, -pivot.z));
    if (shift) matrix.premultiply(new THREE.Matrix4().makeTranslation(shift.x, shift.y, shift.z));
    group.matrix.copy(matrix);
}

function tintLink(name, invalid) {
    const group = linkObjects[name];
    if (!group) return;
    for (const mesh of group.children)
        mesh.material.color.setHex(invalid ? INVALID_TINT : mesh.userData.baseColor);
}

function renderGripper(angles, valid) {
    if (!meshesReady || !angles) return;
    const readouts = document.getElementById('gripper-readout').children;

    for (let f = 0; f < 2; f++) {
        const side = FINGER_TO_SIDE[f];
        const pivots = GRIPPER_GEOMETRY.linkage[side];
        const angle = angles[f] || 0;
        // svgRotationSign carried the URDF joint convention (positive about -y)
        // into the flat view; the same factor takes it into the scene.
        const turn = GRIPPER_GEOMETRY.svgRotationSign * TIP_SCREEN_SIGN[side] * angle
                     * Math.PI / 180;
        const pose = solveLinkage(pivots, turn);
        // No solution means the angle is outside anything the linkage can do;
        // hold the last pose it could reach rather than tearing it open.
        const reachable = pose !== null;
        if (reachable) lastPose[f] = { pose, turn };
        const ok = (!valid || valid[f]) && reachable;

        if (lastPose[f]) {
            const { pose: solved, turn: shown } = lastPose[f];
            setLinkTransform(`${side}_finger`, pivotToScene(pivots.outerFinger),
                             solved.outerFingerTurn);
            setLinkTransform(`${side}_inner_knuckle`, pivotToScene(pivots.innerKnuckle),
                             solved.innerKnuckleTurn);
            setLinkTransform(`${side}_finger_tip`, pivotToScene(pivots.distal), shown,
                             new THREE.Vector3(solved.distalShift[0] * MM, 0,
                                               -solved.distalShift[1] * MM));
        }

        tintLink(`${side}_finger_tip`, !ok);
        // A stale angle is worse than no angle: say why it is not trustworthy.
        readouts[f].textContent = `F${f} ` + (ok ? `${angle.toFixed(1)}°`
            : (reachable ? 'no ref' : 'unreachable'));
        readouts[f].classList.toggle('invalid', !ok);
    }
}

function resizeGripperView() {
    const host = document.getElementById('gripper-view');
    if (!renderer || !host) return;
    const { clientWidth: width, clientHeight: height } = host;
    if (!width || !height) return;
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
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

// --- Init ---

function init() {
    for (let f = 0; f < 2; f++) initStaticChart(`static-finger-${f}`);
    initDynamicChart('dynamic-time');
    initGripperView();
    connect();
}

init();
