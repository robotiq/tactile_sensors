// The 3D gripper panel: a 2F-85 posed from the fingertip IMUs, with the
// force/torque wrench drawn at its base.
//
// Loaded on demand. This module is what pulls in three.js, OrbitControls and
// the baked geometry -- roughly 3 MB all told -- so importing it is the whole
// cost of the "3D gripper" option, and a page that never ticks the box never
// fetches any of it. app.js holds it behind a dynamic import for that reason;
// do not import it from the top level of anything eagerly loaded.

import * as THREE from 'three';
import { OrbitControls } from './vendor/OrbitControls.js';
import { GRIPPER_GEOMETRY } from './gripper_geometry.js';

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
let resizeObserver = null;
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

// Solve the linkage for the drive turned by `drive` and the distal phalanx
// turned by `radians`, both from fully open.
//
// The drive turns the outer knuckle about P1, carrying the outer finger's
// pivot P2 with it; from there it is the same four-bar as at fully open, just
// grounded at the moved P2. P1-P3-P4-P5 is a parallelogram, so with the
// fingertip unturned this closes the gripper the way the real one closes in
// free air: the outer finger locked to the knuckle and the pads staying
// parallel. A fingertip turn on top is the encompassing grip.
function solveLinkage(pivots, radians, drive = 0) {
    const { knuckle: p1, outerFinger: p2, distal: p3, coupler: p4, innerKnuckle: p5 } = pivots;
    const driven = add(p1, rotate(sub(p2, p1), drive));
    // Where the distal pivot sits in free closing, to choose the branch by.
    const freeDistal = add(p1, rotate(sub(p3, p1), drive));
    const outerFingerLength = len(sub(p3, p2));
    const innerKnuckleLength = len(sub(p4, p5));
    const coupler = rotate(sub(p4, p3), radians);

    // P3 sits on a circle about P2, and — since P4 hangs off it by the now-known
    // coupler vector — on another about P5 shifted by that vector.
    const solutions = intersectCircles(driven, outerFingerLength,
                                       sub(p5, coupler), innerKnuckleLength);
    if (!solutions) return null;
    // Two branches; keep the one continuous with the pose we started from.
    const distal = len(sub(solutions[0], freeDistal)) <= len(sub(solutions[1], freeDistal))
        ? solutions[0] : solutions[1];

    return {
        knuckleTurn: drive,
        outerFingerShift: sub(driven, p2),
        distalShift: sub(distal, p3),
        outerFingerTurn: angleOf(sub(distal, driven)) - angleOf(sub(p3, p2)),
        innerKnuckleTurn: angleOf(sub(add(distal, coupler), p5))
                          - angleOf(sub(p4, p5)),
    };
}

// --- Opening from the gripper's position feedback ---
//
// gPO is a byte, 0 at fully open. The 2F-85's position is specified in
// millimetres of opening (about 0.4 mm per count), so the count is taken as
// linear in opening, and the opening converted to a drive angle through the
// linkage itself rather than assumed linear in angle. The two ends are where
// this gripper reads after activation: about 3 fully open, about 228 with the
// fingers closed on nothing. Past the closed end the pads are touching, so the
// pose stops there.
const POS_OPEN = 3;
const POS_CLOSED = 228;

// Inner face of a pad at fully open, from the pad outline itself. Half the
// opening; 42.4 mm for the 85 mm stroke.
const PAD_OPEN_X = Math.min(...GRIPPER_GEOMETRY.tips[0].parts
    .find(p => p.cls === 'pad').d.match(/-?\d+(\.\d+)?/g)
    .filter((_, i) => i % 2 === 0).map(n => Math.abs(Number(n))));

// Drive angle, in the drawing's sense for this side, that puts the pad faces
// at the opening the position count stands for. Mirror-symmetric, so solved
// on magnitudes: free closing swings the distal pivot P3 about P1 on a circle,
// and the pad face moves across exactly as P3 does.
function driveTurnFor(position, pivots, side) {
    if (position == null) return 0;
    const closed = Math.min(Math.max((position - POS_OPEN) / (POS_CLOSED - POS_OPEN), 0), 1);
    const { knuckle: p1, distal: p3 } = pivots;
    const arm = sub(p3, p1);
    const radius = len(arm);
    const start = Math.atan2(Math.abs(arm[1]), Math.abs(arm[0]));
    const targetX = Math.abs(p3[0]) - PAD_OPEN_X * closed - Math.abs(p1[0]);
    const turn = Math.acos(Math.min(Math.max(targetX / radius, -1), 1)) - start;
    // Closing turns the left finger the same way as an inward fingertip does.
    return GRIPPER_GEOMETRY.svgRotationSign * TIP_SCREEN_SIGN[side] * turn;
}

// The pivots are stored in the flat view's coordinates (x right, y down). The
// scene keeps the gripper's own frame, where y is the joint axis and z is up.
const pivotToScene = (p) => new THREE.Vector3(p[0] * MM, 0, -p[1] * MM);

export async function initGripperView(host) {
    if (!host) return;

    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x16213e);

    camera = new THREE.PerspectiveCamera(35, 1, 0.01, 10);
    // Start on a three-quarter view rather than square down the joint axis.
    // Face-on is the flat drawing's view and reads as a diagram: the linkage
    // collapses into a single silhouette and the wrench arrow, which can point
    // anywhere in space, loses its depth entirely. Off-axis, both read as solid.
    //
    // 35 degrees around and 25 up — far enough round to give the pads and the
    // five-bar some depth, not so far that the linkage stops reading in
    // profile. Swung about -z, towards +x, so the view matches the side the
    // gripper is actually approached from on the rig. That is 34 degrees off
    // the key light rather than 15, so the near face is still the lit one, just
    // less flatly. 0.44 m rather than the 0.40 the face-on view used: seen from
    // a corner the near corner of the gripper sits closer to the camera, and
    // the old distance framed it tighter. The user can orbit away.
    camera.position.set(0.2287, -0.3267, 0.2510);
    camera.up.set(0, 0, 1);

    renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(window.devicePixelRatio);
    host.appendChild(renderer.domElement);
    watchContext(renderer.domElement, host);

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
    // The panel is the element whose size actually matters, and this fires on
    // fullscreen and on layout changes that leave the grid's own box alone.
    resizeObserver = new ResizeObserver(() => resizeGripperView());
    resizeObserver.observe(host);
    setRunning(true);
}


// Drawing only happens while the demo tab is showing and the option is on.
// three.js stops its own loop when handed null, which is cheaper and more
// complete than gating inside the callback.
let running = false;

function tick() {
    controls.update();
    renderer.render(scene, camera);
}

export function setRunning(on) {
    if (!renderer || on === running) return;
    running = on;
    renderer.setAnimationLoop(on ? tick : null);
    if (on) {
        // Re-measure and draw immediately rather than waiting on the resize
        // observer: the panel has usually just gone from display:none back to
        // visible, and until something re-measures it the canvas keeps
        // whatever size it had when it was hidden.
        resizeGripperView();
        if (meshesReady) tick();
    }
}

// A canvas whose context is lost draws nothing and never recovers on its own,
// which looks exactly like the panel being switched off. Asking for a restore
// is not always enough -- when the context was dropped because the browser was
// short of them, it may simply never come back -- so if nothing has happened
// after a moment, give up on this renderer and tell app.js to build a new one.
const RESTORE_GRACE_MS = 2500;
let restoreTimer = null;

function note(text) {
    const el = document.querySelector('.gripper-error');
    if (!el) return;
    if (text === null) { el.hidden = true; return; }
    el.textContent = text;
    el.hidden = false;
}

function watchContext(canvas, host) {
    canvas.addEventListener('webglcontextlost', (e) => {
        e.preventDefault();          // required, or the context is never restored
        note('Lost the 3D graphics context — restoring…');
        clearTimeout(restoreTimer);
        restoreTimer = setTimeout(
            () => host.dispatchEvent(new CustomEvent('gripper-context-lost')),
            RESTORE_GRACE_MS);
    });
    canvas.addEventListener('webglcontextrestored', () => {
        clearTimeout(restoreTimer);
        note(null);
        resizeGripperView();
        if (meshesReady) tick();
    });
}

// Tear the panel down completely, so a fresh one can be built. Everything the
// renderer holds is on a context that is already gone; what matters is
// releasing it and removing the canvas so the rebuild starts clean.
export function disposeGripperView() {
    clearTimeout(restoreTimer);
    if (renderer) {
        renderer.setAnimationLoop(null);
        renderer.dispose();
        renderer.domElement.remove();
    }
    if (controls) controls.dispose();
    // Otherwise every rebuild leaves another observer on the same host.
    if (resizeObserver) resizeObserver.disconnect();
    resizeObserver = null;
    for (const name of Object.keys(linkObjects)) delete linkObjects[name];
    renderer = scene = camera = controls = null;
    wrenchGroup = forceArrow = twistArc = anchorDot = actionLine = null;
    meshesReady = false;
    running = false;
    lastPose[0] = lastPose[1] = null;
}

async function loadMeshes() {
    const response = await fetch(GRIPPER_GEOMETRY.meshFile);
    if (!response.ok) {
        throw new Error(`${GRIPPER_GEOMETRY.meshFile}: HTTP ${response.status}`);
    }
    const blob = await response.arrayBuffer();
    // A truncated file would otherwise surface as a RangeError from deep in
    // the first Float32Array that runs off the end.
    const needed = Math.max(...GRIPPER_GEOMETRY.meshes.map(
        part => part.byteOffset + part.vertexCount * 3 * 4));
    if (blob.byteLength < needed) {
        throw new Error(`${GRIPPER_GEOMETRY.meshFile} is truncated ` +
                        `(${blob.byteLength} of ${needed} bytes)`);
    }

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

// `position` is the gripper's position feedback (0-255), or null with no
// gripper connected, which draws it fully open as before.
function renderGripper(angles, valid, position = null) {
    if (!meshesReady || !angles) return;

    for (let f = 0; f < 2; f++) {
        const side = FINGER_TO_SIDE[f];
        const pivots = GRIPPER_GEOMETRY.linkage[side];
        const angle = angles[f] || 0;
        // svgRotationSign carried the URDF joint convention (positive about -y)
        // into the flat view; the same factor takes it into the scene.
        const turn = GRIPPER_GEOMETRY.svgRotationSign * TIP_SCREEN_SIGN[side] * angle
                     * Math.PI / 180;
        const pose = solveLinkage(pivots, turn, driveTurnFor(position, pivots, side));
        // No solution means the angle is outside anything the linkage can do;
        // hold the last pose it could reach rather than tearing it open.
        const reachable = pose !== null;
        if (reachable) lastPose[f] = { pose, turn };
        const ok = (!valid || valid[f]) && reachable;

        if (lastPose[f]) {
            const { pose: solved, turn: shown } = lastPose[f];
            setLinkTransform(`${side}_knuckle`, pivotToScene(pivots.knuckle),
                             solved.knuckleTurn);
            setLinkTransform(`${side}_finger`, pivotToScene(pivots.outerFinger),
                             solved.outerFingerTurn,
                             new THREE.Vector3(solved.outerFingerShift[0] * MM, 0,
                                               -solved.outerFingerShift[1] * MM));
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

export { renderGripper, renderWrench, resizeGripperView };
