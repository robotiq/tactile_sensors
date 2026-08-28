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
    renderWrench(msg.wrench, msg.wrenchError, msg.ftOrigin);
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
        colorbar: { thickness: 6, outlinewidth: 0, tickfont: { size: 8 }, len: 1, x: 1.02 }
    }], baseLayout({
        xaxis: Object.assign({}, AXIS_STYLE, { dtick: 1, range: [0, 4] }),
        yaxis: Object.assign({}, AXIS_STYLE, {
            dtick: 1, range: [7, 0], scaleanchor: 'x', scaleratio: 1, constrain: 'domain'
        }),
        margin: { t: 6, b: 18, l: 18, r: 0 }
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

function initDynamicChart(divId, finger) {
    Plotly.newPlot(divId, [{
        y: [], type: 'scattergl', mode: 'lines',
        line: { width: 1, color: FINGER_COLORS[finger] }
    }], baseLayout({
        xaxis: Object.assign({}, AXIS_STYLE, { showticklabels: false }),
        yaxis: Object.assign({}, AXIS_STYLE,
                             { range: [-DYN_MIN_HALF_RANGE, DYN_MIN_HALF_RANGE] }),
        margin: { t: 18, b: 20, l: 36, r: 6 }
    }), PLOTLY_CONFIG);
    dynRange[finger] = DYN_MIN_HALF_RANGE;
}

function renderDynamic(dynData) {
    if (!dynData) return;
    for (let f = 0; f < 2; f++) {
        const samples = dynData[f];
        const mV = new Float32Array(samples.length);
        let peak = 0;
        for (let i = 0; i < samples.length; i++) {
            mV[i] = samples[i] * DYN_FULL_SCALE_MV / 32767;
            if (Math.abs(mV[i]) > peak) peak = Math.abs(mV[i]);
        }
        Plotly.restyle(`dynamic-time-${f}`, { y: [mV] });

        // Clamped at full scale: no reading can land outside it, so a wider
        // axis would only add empty space.
        const wanted = Math.ceil(peak * DYN_HEADROOM / DYN_RANGE_STEP) * DYN_RANGE_STEP;
        const half = Math.min(Math.max(wanted, DYN_MIN_HALF_RANGE), DYN_FULL_SCALE_MV);
        if (half !== dynRange[f]) {
            dynRange[f] = half;
            Plotly.relayout(`dynamic-time-${f}`, { 'yaxis.range': [-half, half] });
        }
    }
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
    camera.position.set(0, -0.40, 0.065);
    camera.up.set(0, 0, 1);

    renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(window.devicePixelRatio);
    host.appendChild(renderer.domElement);

    controls = new OrbitControls(camera, renderer.domElement);
    controls.target.set(0, 0, 0.065);
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
    initWrench();
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
        // Addressed by id, not by position: the wrench readout sits between
        // the two finger ones.
        const readout = document.getElementById(`tip-readout-${f}`);
        readout.textContent = `F${f} ` + (ok ? `${angle.toFixed(1)}°`
            : (reachable ? 'no ref' : 'unreachable'));
        readout.classList.toggle('invalid', !ok);
    }
}

// --- Force/torque wrench ---
//
// A wrench is a force along a line plus a twist about that same line. Drawing
// it that way — rather than as two arrows sharing an origin — makes the lever
// arm visible as geometry: press off-centre and the arrow slides towards where
// the load actually acts.
//
//   Fhat = F / |F|
//   Mpar = (M . Fhat) Fhat      the twist no translation can remove
//   r    = (F x M) / |F|^2      offset to the line of action
//
// r grows as 1/|F|^2, so below a force floor the line of action is meaningless
// and jittery; there the arrow falls back to the sensor origin carrying the
// whole moment as a twist.

const FORCE_FLOOR_N = 3.0;      // below this, no usable line of action
// The gripper is only 150 mm tall, so the arrow has to stay well short of that
// to read as an annotation on it rather than as another part of the scene.
const FORCE_SCALE = 0.002;      // metres of arrow per newton
const FORCE_MAX_LEN = 0.10;
// A couple of newtons would draw a 4 mm stub, too small to read a direction
// off. The arrow's job is to show which way the load points; the number beside
// it carries the magnitude.
const FORCE_MIN_LEN = 0.014;
const OFFSET_MAX_M = 0.15;      // keep the arrow in frame at low force
const TWIST_SCALE = 2.0;        // radians of arc per newton-metre
const TWIST_MIN_NM = 0.02;
const LINE_HALF_LEN = 0.22;     // how far the line of action is drawn either way
// The wrench's anchor is the point on the line closest to the sensor, which
// sits below the gripper and drags the arrow out of frame. The line itself
// already says *where* the load acts, so the arrow is slid along it to the
// height of the fingers, where it can be seen against the thing it is pushing.
const WRENCH_FOCUS_Z = 0.10;

let wrenchGroup, forceArrow, twistArc, anchorDot, actionLine;

function wrenchGeometry(force, moment, origin) {
    const f = new THREE.Vector3().fromArray(force);
    const m = new THREE.Vector3().fromArray(moment);
    const magnitude = f.length();
    if (magnitude < 1e-9) return null;

    if (magnitude < FORCE_FLOOR_N) {
        // Too little force for the offset to mean anything: anchor at the
        // sensor and let the whole moment be the twist.
        return { anchor: origin.clone(), force: f, twist: m, onLine: false };
    }
    const direction = f.clone().divideScalar(magnitude);
    const twist = direction.clone().multiplyScalar(m.dot(direction));
    const offset = new THREE.Vector3().crossVectors(f, m).divideScalar(magnitude * magnitude);
    if (offset.length() > OFFSET_MAX_M) offset.setLength(OFFSET_MAX_M);
    return { anchor: origin.clone().add(offset), force: f, twist, onLine: true };
}

// THREE.ArrowHelper draws its shaft as a line, which stays one pixel wide
// however close the camera gets — a hairline body under a solid cone head. This
// builds the arrow from real geometry so both parts scale together.
function makeArrow(color) {
    const group = new THREE.Group();
    const material = new THREE.MeshStandardMaterial(
        { color, roughness: 0.35, metalness: 0.0 });
    // Unit cylinder and cone, both along +y and centred on the origin, so they
    // can be scaled and shifted into place without rebuilding geometry.
    const shaft = new THREE.Mesh(new THREE.CylinderGeometry(1, 1, 1, 20), material);
    const head = new THREE.Mesh(new THREE.ConeGeometry(1, 1, 24), material);
    group.add(shaft, head);
    return { group, shaft, head, material };
}

function aimArrow(arrow, from, direction, length) {
    const headLength = Math.min(0.018, length * 0.25);
    const headRadius = headLength * 0.45;
    const shaftRadius = headRadius * 0.4;
    const shaftLength = Math.max(length - headLength, 1e-4);

    arrow.shaft.scale.set(shaftRadius, shaftLength, shaftRadius);
    arrow.shaft.position.set(0, shaftLength / 2, 0);
    arrow.head.scale.set(headRadius, headLength, headRadius);
    arrow.head.position.set(0, shaftLength + headLength / 2, 0);

    arrow.group.position.copy(from);
    arrow.group.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), direction);
}

function initWrench() {
    wrenchGroup = new THREE.Group();
    wrenchGroup.visible = false;
    scene.add(wrenchGroup);

    forceArrow = makeArrow(0xffd166);
    wrenchGroup.add(forceArrow.group);

    // The line of action, drawn through the whole scene: it is what makes the
    // lever arm legible, since you can see which finger the force runs through.
    actionLine = new THREE.Line(
        new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), new THREE.Vector3()]),
        new THREE.LineDashedMaterial({ color: 0xffd166, dashSize: 0.006, gapSize: 0.004,
                                       transparent: true, opacity: 0.55 }));
    wrenchGroup.add(actionLine);

    anchorDot = new THREE.Mesh(
        new THREE.SphereGeometry(0.0035, 16, 12),
        new THREE.MeshBasicMaterial({ color: 0xffd166 }));
    wrenchGroup.add(anchorDot);

    twistArc = new THREE.Mesh(
        new THREE.TorusGeometry(0.026, 0.0025, 8, 40, Math.PI),
        new THREE.MeshBasicMaterial({ color: 0x7ee0c0 }));
    wrenchGroup.add(twistArc);
}

function renderWrench(wrench, error, ftOrigin) {
    const readout = document.getElementById('wrench-readout');
    if (!wrenchGroup) return;
    if (!wrench) {
        wrenchGroup.visible = false;
        readout.textContent = error ? `F/T: ${error}` : 'F/T: no sensor';
        readout.classList.add('invalid');
        return;
    }

    // The anchor comes from the server so there is one definition of where the
    // sensor sits, not a copy here that can drift from it.
    const origin = new THREE.Vector3().fromArray(ftOrigin).multiplyScalar(MM);
    const solved = wrenchGeometry(wrench.slice(0, 3), wrench.slice(3), origin);
    if (!solved) {
        wrenchGroup.visible = false;
        return;
    }
    wrenchGroup.visible = true;

    const magnitude = solved.force.length();
    const direction = solved.force.clone().normalize();
    const length = Math.min(Math.max(magnitude * FORCE_SCALE, FORCE_MIN_LEN),
                            FORCE_MAX_LEN);

    // On the line of action, slide along it to finger height so the arrow is
    // drawn against what it is acting on rather than under the gripper. Below
    // the force floor there is no line to slide along — the point of the
    // fallback is that the load cannot be located — so the arrow stays put at
    // the sensor origin, which is where the reading is actually taken.
    let head = solved.anchor.clone();
    if (solved.onLine) {
        const focus = new THREE.Vector3(0, 0, WRENCH_FOCUS_Z);
        head.addScaledVector(direction, focus.clone().sub(solved.anchor).dot(direction));
    }

    aimArrow(forceArrow, head.clone().addScaledVector(direction, -length),
             direction, length);
    anchorDot.position.copy(head);

    const ends = [
        solved.anchor.clone().addScaledVector(direction, -LINE_HALF_LEN),
        solved.anchor.clone().addScaledVector(direction, LINE_HALF_LEN),
    ];
    actionLine.geometry.dispose();
    actionLine.geometry = new THREE.BufferGeometry().setFromPoints(ends);
    actionLine.computeLineDistances();
    actionLine.visible = solved.onLine;

    const twistMagnitude = solved.twist.length();
    twistArc.visible = twistMagnitude > TWIST_MIN_NM;
    if (twistArc.visible) {
        const arc = Math.min(twistMagnitude * TWIST_SCALE, 4.5);
        twistArc.geometry.dispose();
        twistArc.geometry = new THREE.TorusGeometry(0.026, 0.0025, 8, 40, arc);
        twistArc.position.copy(head);
        // The torus turns about its own +z; align that with the twist so the
        // arc sweeps the way the right-hand rule says it should.
        const axis = solved.twist.clone().normalize();
        twistArc.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), axis);
    }

    const moment = new THREE.Vector3().fromArray(wrench.slice(3)).length();
    readout.textContent = `${magnitude.toFixed(1)} N   ${moment.toFixed(2)} Nm`
        + (solved.onLine ? '' : '  (at sensor)');
    readout.classList.remove('invalid');
}

function resizeGripperView() {
    const host = document.getElementById('gripper-view');
    if (!renderer || !host) return;
    const { clientWidth: width, clientHeight: height } = host;
    if (!width || !height) return;
    // Let three.js set the canvas's CSS size as well as its buffer. Skipping
    // that leaves the canvas laid out at width * devicePixelRatio, so on any
    // scaled display — or in fullscreen — it stops matching its container and
    // the gripper drifts off centre.
    renderer.setSize(width, height);
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
// The gripper panel gets its own observer: it is the element whose size
// actually matters here, and this fires on fullscreen and on layout changes
// that leave the grid's own box alone.
const gripperHost = document.getElementById('gripper-view');
if (gripperHost) new ResizeObserver(() => resizeGripperView()).observe(gripperHost);

// --- Controls ---

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

// --- Init ---

function init() {
    for (let f = 0; f < 2; f++) {
        initStaticChart(`static-finger-${f}`);
        initDynamicChart(`dynamic-time-${f}`, f);
    }
    initGripperView();
    connect();
}

init();
