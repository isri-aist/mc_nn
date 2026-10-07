# 🦾 PostureTaskPolicyContract — posture-task tutorial

This tutorial contract uses the bundled `dummy_example.onnx`, supplies random
observations, and applies the model's outputs as posture-task position targets.
It is an integration example, **not a trained or deployable policy**.

> **Looking for shared mc_nn behavior?** This README focuses on this contract's
> posture-task example and contract-specific settings. The overall policy
> lifecycle, RunNN pipeline, shared configuration and FSM completion behavior
> are covered in the [main mc_nn README](https://github.com/isri-aist/mc_nn/blob/main/README.md)
> and the [mc_nn contracts guide](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md).
> Start there if a shared behavior or option is not explained here.

## Try it

```yaml
states:
  TryPosturePolicy:
    base: RunNNBase
    active_policies: [dummy_example]
    policies:
      - onnx: ["dummy_example"]
        contract: PostureTaskPolicyContract
        policy_hz: 2.0
        joints: [right_wrist_roll_joint, right_wrist_pitch_joint]
        weight: 1.0      # posture-task weight
        stiffness: 10.0   # posture-task stiffness
        seed: 0           # 0 = random input sequence each launch
        input_min: -1.0   # random observation range
        input_max: 1.0
```

The shared `RunNNBase`, model discovery, policy ID and scheduling options are
documented in the [RunNN configuration guide](https://github.com/isri-aist/mc_nn/blob/main/README.md#runnn-configuration).
For wildcard matching and ID construction, also see
[Model discovery and policy IDs](https://github.com/isri-aist/mc_nn/blob/main/README.md#model-discovery-and-policy-ids).
The contract-specific settings are:

| Field | Type | Default | Purpose |
| --- | --- | --- | --- |
| `joints` | list of strings | required | Ordered list of one-DoF joints controlled by the task. |
| `weight`, `stiffness` | number | `1.0`, `1.0` | Posture-task tuning parameters; both must be positive. |
| `seed` | unsigned integer | `0` | Random generator seed; `0` selects a different sequence per launch. |
| `input_min`, `input_max` | number | `-1.0`, `1.0` | Bounds for random demonstration observations. |

For the overall contract API and when these hooks run, see the
[mc_nn hooks reference](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#hooks-reference)
and [lifecycle](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#lifecycle).

## Hooks used

This contract derives from `MCNN`, which owns model loading, inference and
tensor-size checks. It implements the policy lifecycle as follows:

| Hook | How this contract uses it |
| --- | --- |
| `configurePolicy` | Reads the selected joints, random-observation settings, and posture-task weight/stiffness. |
| `startPolicy` | Validates the configured one-DoF joints against model action capacity and robot limits, then creates the posture task. |
| `buildInputs` | Generates random observations before each inference; replace this with training-compatible robot/sensor features for a real policy. |
| `applyActions` | Clamps the first action slots to `[-1, 1]`, maps them to joint position limits, and updates the posture-task target. |
| `teardownPolicy` | Removes the posture task when the policy is paused, removed, or stopped. |

## Build dependency

This contract uses mc_rtc's `mc_tasks::PostureTask` (`mc_tasks/PostureTask.h`)
in addition to the standard mc_nn contract API.

## Joint and action mapping

Replace `joints` with joints on your robot. Each configured joint must exist,
have one DoF and finite position limits. The bundled ONNX model has **9
actions**, so this contract can control at most 9 joints. Actions are assigned
to joints in list order; if fewer joints are configured, only the first action
slots are used. Each action is clamped to `[-1, 1]` and mapped to that joint's
position limits. See [ONNX model requirements](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#onnx-model-requirements)
for model tensor expectations. Lower weight/stiffness values make this
demonstration's task less assertive.

## Adapting the observations

The current `buildInputs()` intentionally generates random values. To adapt this
template for a real policy, replace that code with the exact inputs used in
training and preserve their order and preprocessing. Typical sources include:

- `ctl.robot()` for the controller's current robot state, or
  `ctl.realRobot()` for measured/estimated state;
- each robot's `q()` and `alpha()` values for joint position and velocity;
- another robot selected by name in a multi-robot controller;
- body poses and velocities, sensors, commands, object state, or values stored
  in `ctl.datastore()`.

Check that the constructed observation count and semantics match the model.
MCNN validates the tensor size, but cannot validate the meaning or order of
features. For the boundary between shared inference checks and contract-owned
semantics, see [mc_nn's contract responsibilities](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#responsibilities-and-examples).

## RunNN controls and transitions

The common Ready/Running/Paused states and GUI actions belong to RunNN; see
[GUI and runtime controls](https://github.com/isri-aist/mc_nn/blob/main/README.md#gui-and-runtime-controls).
This contract does not report completion, so configure it as non-blocking or
give it a timeout if it is active; see
[Completion and FSM transitions](https://github.com/isri-aist/mc_nn/blob/main/README.md#completion-and-fsm-transitions).

## Safety note

This example does command a posture task. Test in simulation first and use
appropriate robot safety procedures. Random observations do not produce
meaningful policy behavior.
