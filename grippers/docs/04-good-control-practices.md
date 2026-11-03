# Good control practices

The gripper can be controlled in position, speed and force, and it gives
feedback such as object detection. Knowing these control features is not
enough to control the gripper efficiently: you also need to know the good
control practices to get the best gripper performance. These practices are
useful for people or AI agents who want to implement gripper control in
their systems.

This page assumes familiarity with the SDK types introduced in
[How it works](03-how-it-works.md) — `GripperCommand`, `GripperStatus`,
`ActionRequestBit`, `ObjectDetection`, `ActivationState` and
`GripperFault`. The numeric position thresholds below (3 and 230) are
measured on a 2F-85 with stock fingers — see
[`profiles::k2F85`](03-how-it-works.md#commanding-in-si-units); a
different model or custom fingers shifts them.

## Activation and status

- Make sure the gripper fingers are free to move during activation. The
  activation moves the fingers through their full stroke to calibrate the
  open and closed positions, so an object or obstacle between the fingers
  can make it fail or give a wrong calibration.
- Redo the activation if the fingers or fingertips are changed. The
  activation adjusts the gripper to the fingertips, so that 0 is always
  fully open and 255 fully closed.
- Check `gripperFault()` with every status read.

## Position commands

- The C++ SDK exchanges commands and status with the gripper at high
  frequency, but that doesn't mean commands should be changed at the same
  frequency. Changing the *target* at a low frequency works well with the
  gripper: give it one clear, distinct command and let it manage its own
  path to it, rather than micromanaging it with constantly-varying position
  requests. Sending different position commands at high frequency is fine
  only when:
  - small displacement requests are executed at slow speed; or
  - a position request of 0 or 255 is held constant once committed to a
    grip, with the high-frequency variation used only to preposition the
    fingers before that final 0/255 command.
- Command 0 or 255 when committing to a grip, not the end positions read
  back via `GripperStatus::position` (3 fully open, about 230 closed on
  itself, on a 2F-85).
- Update the commanded position, speed and force at the same time. A
  position change starts the motion at once with the speed and force
  fields as they stand.

## Gripping and releasing an object

Grip an object as follows:

1. Position the gripper so that the fingers are on both sides of the
   object. Keep a gap of about 1 cm between the fingers and the object
   surface. The gap is used for the gripper to build speed before closing.
2. Commit to the grip by commanding position 255 for a full close or 0 for
   a full open. When sending the grip command, also set the force and
   speed of your choice.

- Do not grip an object with small position requests. This results
  in a weak grip. The gripper gets its best grip when it receives a clear,
  anticipated grip request. Such a request lets the gripper build kinetic
  energy and use the impact force to secure the object.
- Use a force parameter above 0 when committing to a grip on an object, so
  that the Re-grasp function is enabled.
- Use maximum force and maximum speed to release an object. Significant
  force can be applied to the gripper mechanism during a grip, so high
  force and speed parameters are needed for a proper release.

## Grip force

- Understand that the force parameter is not a force in newtons, and there
  is no single formula to convert it to the actual grip force. The actual
  grip force depends on the force setting, the speed setting, the object
  hardness and the fingertips.
- Above a certain speed setting (lower on soft objects), the speed
  parameter has more effect on the actual grip force than the force
  parameter, because the gripper gets its grip force from kinetic impact;
  below that speed, the force setting dominates. See the manual's
  force-curve section for the actual combination that applies to your
  object and fingertips.
- The force parameter has more effect on soft objects than on hard
  objects.
- Understand that after gripping, the grip force cannot be decreased by
  changing the force and speed parameters, because the gripper has a
  self-locking mechanism. The only way to decrease the grip force is to
  request a position out of the grip.
- Analyzing motor current (`GripperStatus::current`) to estimate the grip
  force is not very relevant. The gripper does not provide actual grip force
  feedback; the grip force is set by choosing the corresponding
  combination of force and speed. The manual provides curves showing the
  approximate actual grip force for different combinations of force and
  speed parameters, for different object stiffnesses and a given type of
  fingertip.
- If you need grip force feedback, use a tactile sensor such as the
  TSF-85.

## Object detection and Re-grasp

- After an object is detected, do not change the position request. It
  should stay at 0 or 255. Changing the position target clears the object
  detection status and forces the gripper to retry a grip.
- Stopping and starting the gripper (clearing then setting
  `ActionRequestBit::GoTo`) resets the object detection.
- If you want to adjust the force after the gripper has detected an
  object, reset the object detection status by clearing then setting
  `ActionRequestBit::GoTo` with the new force and speed parameters. A
  re-grip from contact cannot reach the maximum grip force, which comes
  from the impact force — but it can still increase the grip force when
  the first grip was made with low speed and force, and the parameters are
  increased after object detection.
- Understand that the Re-grasp speed is independent from the speed
  parameter. If you want a faster Re-grasp, clear the object detection by
  changing the position target (for example from 254 to 255) with the
  speed and force you want. Any position change starts a new grip, with
  the impact mechanism.
- Speed and force changes are ignored once the fingers have stopped
  (object detected or target reached) if the position request stays the
  same. They only apply while the fingers are still moving towards the
  target. To apply new values after a stop, change the position request or
  toggle `ActionRequestBit::GoTo`.
- Do not rely on `objectDetection()` to detect a slip during Re-grasp:
  it stays at `ObjectDetection::DetectedWhileClosing` for as long as the
  fingers move to re-grasp. To detect a slip, watch for a change of
  `GripperStatus::position` during that state instead.

---

**Next:** [Robust example walkthrough →](05-robust-example-walkthrough.md)
