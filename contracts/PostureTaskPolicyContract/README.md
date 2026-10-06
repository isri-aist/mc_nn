# PostureTaskPolicyContract

This tutorial contract uses the bundled `dummy_example.onnx`, supplies random
observations, and applies the model's outputs as posture-task position targets.
It is an integration example, **not a trained or deployable policy**.

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
        stiffness: 1.0   # posture-task stiffness
        seed: 0           # 0 = random input sequence each launch
        input_min: -1.0   # random observation range
        input_max: 1.0
```

Replace `joints` with joints on your robot. Each configured joint must exist,
have one DoF and finite position limits. The bundled ONNX model has **9
actions**, so this contract can control at most 9 joints. Actions are assigned
to joints in list order; if fewer joints are configured, only the first action
slots are used. Each action is clamped to `[-1, 1]` and mapped to that joint's
position limits. Lower weight/stiffness values make this demonstration's task
less assertive.

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
features.

This example does command a posture task. Test in simulation first and use
appropriate robot safety procedures. Random observations do not produce
meaningful policy behavior.
