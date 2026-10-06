# 🎲 RandomPolicyContract — inference smoke test and contract template

`RandomPolicyContract` runs **any** single-input ONNX model: at every inference step it
feeds the model uniformly random observations and prints the resulting actions
in the terminal. It creates no task and never moves the robot, so it is safe on
any model and any robot. Use it to:

- check that a model loads and runs in mc_rtc (sizes, rate, execution provider);
- learn the mc_nn pipeline;
- start a new contract: copy this directory and replace the parts marked
  `YOUR CONTRACT:` in the source.

> **Looking for shared mc_nn behavior?** This README covers this contract's
> example and contract-specific settings. The overall policy lifecycle,
> RunNN pipeline, shared configuration and FSM completion behavior are covered
> in the [main mc_nn README](https://github.com/isri-aist/mc_nn/blob/main/README.md)
> and the [mc_nn contracts guide](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md).
> Start there if a shared behavior or option is not explained here.

The full hook reference is in the
[mc_nn contracts guide](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#hooks-reference).

## At a glance

| | |
| --- | --- |
| **Model** | Any ONNX model with one float input and a supported output tensor. |
| **Inputs** | Uniform random values; not representative of a model's training data. |
| **Outputs** | Printed in the terminal; no robot tasks or commands are created. |
| **Use it for** | Smoke-testing inference or as a starting point for a new single-model contract. |

## Try it with your model

Any compatible `.onnx` model works. Put it anywhere and point `models_dirs` to
its folder; see [model discovery and policy IDs](https://github.com/isri-aist/mc_nn/blob/main/README.md#model-discovery-and-policy-ids)
for search behavior and naming, and [ONNX model requirements](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#onnx-model-requirements)
for supported tensor formats.

```yaml
states:
  TryMyModel:
    base: RunNNBase
    # ---------------- Global RunNN configuration
    models_dirs: [/home/me/my_models] # searched recursively for .onnx files
    contracts_dirs: []
    preload: false
    verbose: 1
    gui: true
    logs: true
    active_policies: [my_model]       # file name of my_models/.../my_model.onnx
    # ---------------- Policy configurations
    policies:
      - onnx: ["*"]                   # every model found: all are available in the GUI
        prefix: ""
        contract: RandomPolicyContract
        # -- Common contract configuration (read by RunNN)
        policy_hz: 10.0
        timeout: 0.0
        blocking: true
        exclusive: false
        device: auto
        # -- Contract-specific configuration (RandomPolicyContract)
        seed: 0           # random generator seed; 0 = different inputs on every run
        input_min: -1.0   # observations are drawn uniformly in [input_min, input_max]
        input_max: 1.0
        print_every: 10   # print the actions every N inference steps (>= 1)
        finish_after: 0   # report finished after N steps (0 = never), to try FSM transitions
transitions:
  - [TryMyModel, "my_model(OK)", NextState, Auto]
```

The state-wide and common policy options (`models_dirs`, `active_policies`,
`policy_hz`, `timeout`, and others) are described in the
[RunNN configuration guide](https://github.com/isri-aist/mc_nn/blob/main/README.md#runnn-configuration). This contract
adds the following policy-specific fields:

| Field | Type | Default | Purpose |
| --- | --- | --- | --- |
| `seed` | unsigned integer | `0` | Random generator seed; `0` selects a different seed each launch. |
| `input_min`, `input_max` | number | `-1.0`, `1.0` | Inclusive bounds for generated observations. |
| `print_every` | unsigned integer | `1` | Print one action summary every N inferences; must be at least `1`. |
| `finish_after` | unsigned integer | `0` | Report completion after N steps; `0` never finishes. |

For how policy completion gates state completion and maps to FSM output, see
[Completion and FSM transitions](https://github.com/isri-aist/mc_nn/blob/main/README.md#completion-and-fsm-transitions).

## Hooks used

This contract derives from `MCNN`, which handles model loading, inference and
input/output size checks. It implements these policy hooks:

| Hook | How this contract uses it |
| --- | --- |
| `configurePolicy` | Reads and validates the random-input, logging and completion settings. |
| `startPolicy` | Resets the random generator and counters after launch/play, then reports model tensor sizes. |
| `buildInputs` | Fills the model input with uniformly random values before each inference. |
| `applyActions` | Measures and periodically prints the inferred action vector; it sends no commands to the robot. |
| `isPolicyFinished` | Reports completion when `finish_after` is greater than zero and that many inferences have run. |
| `teardownPolicy` | Reports the final inference count when paused, removed, or stopped. |
| `addGui` (optional) | Shows inference count, action norm, and random input range. |
| `addLog` (optional) | Adds the contract-specific action-norm log entry. |

For hook timing and required versus optional hooks, see the
[MCNN hook reference](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#hooks-reference);
the complete [contract lifecycle](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#lifecycle)
shows when RunNN invokes them.

Terminal output:

```text
[info] [RandomPolicyContract:my_model] Started on robot 'my_robot': 48 random observations -> 12 actions at 10.0 Hz
[info] [RandomPolicyContract:my_model] step 10 | action norm 3.2141 | actions [0.1250, -0.8113, ...]
[info] [RandomPolicyContract:my_model] Stopped after 57 steps
```

In the GUI, `MCNN / TryMyModel / my_model / RandomPolicyContract` shows the number of
inference steps, the last action norm and the input range. The log contains
`MCNN_my_model_action_norm` in addition to the entries every policy gets
(`MCNN_my_model_observation`, `_action`, `_policy_hz`, `_measured_hz`, `_updates`).
For the shared Ready/Running/Paused states and common controls, see
[GUI and runtime controls](https://github.com/isri-aist/mc_nn/blob/main/README.md#gui-and-runtime-controls);
for scheduling, preload and device performance, see
[Runtime and performance](https://github.com/isri-aist/mc_nn/blob/main/README.md#runtime-and-performance).

## The pipeline, hook by hook

`RandomPolicyContract` derives from `MCNN`, the ready-made base for "one model, one
observation → inference → action step". mc_nn (RunNN + MCNN) does everything
except the five marked hooks. For the shared discovery, scheduling, lifecycle
and completion flow, see
the [RunNN configuration guide](https://github.com/isri-aist/mc_nn/blob/main/README.md#runnn-configuration)
and the [contracts lifecycle guide](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#lifecycle).

```text
RunNN state starts
  finds the .onnx files matched by `onnx` in `models_dirs`
  creates one RandomPolicyContract per model
  └─ configurePolicy(config)      read seed, input range, print_every, finish_after
policy launched (active_policies, or Launch / Play in the GUI)
  MCNN loads the model (MCNNModel)  -> sizes known: expectedObservationSize(), expectedActionSize()
  └─ startPolicy(ctl)             reset the generator and counters, print the sizes
     addGui / addLog              GUI elements and log entries (if RunNN gui / logs)
every controller tick, at policy_hz:
  MCNN clears input_buffer_
  └─ buildInputs(ctl)             push expectedObservationSize() random floats
  MCNN checks the size, runs the model, fills output_buffer_
  └─ applyActions(ctl)            compute the norm, print every print_every steps
every controller tick:
  └─ isPolicyFinished(ctl)        true after finish_after steps (if > 0)
policy paused / removed / state ends
  └─ teardownPolicy(ctl)          print the step count
```

| Hook | What RandomPolicyContract does | What your contract does instead |
| --- | --- | --- |
| `configurePolicy` | Reads and validates its 5 fields. | Reads joint lists, gains, frames, object names... |
| `startPolicy` | Resets its state, prints model sizes. | Checks the robot matches the model (joint count, frames), creates tasks. |
| `buildInputs` | Random values. | Real observations in the exact training order. |
| `applyActions` | Prints the actions. | Scales actions to targets, updates tasks. |
| `isPolicyFinished` | After `finish_after` steps. | Task achieved (e.g. object at goal for some time). |
| `teardownPolicy` | Prints the step count. | Removes its tasks, restores what it changed. |
| `addGui` (optional) | 3 labels. | Its own controls and readings. |
| `addLog` (optional) | `action_norm`. | Its own signals. |

## Going further

- **Real observations**: read `ctl.robot()` (control state), `ctl.realRobot()`
  (estimated state), sensors, or `ctl.datastore()` in `buildInputs`. Keep the
  exact training order; validate sizes in `startPolicy` and throw on mismatch.
  See [ONNX model requirements](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#onnx-model-requirements)
  for tensor constraints.
- **Commanding the robot**: create an mc_rtc task in `startPolicy`, update its
  target in `applyActions`, remove it in `teardownPolicy`. See
  [Host controller requirements](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#host-controller-requirements)
  when using hooks that require host support.
- **Work between inferences** (e.g. interpolating targets, reading a joystick):
  override `update(ctl, dt)`, called every controller tick (see the
  [runtime hook reference](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#runtime)).
- **Work after the QP solve** (e.g. writing torques directly): override
  `afterSolve(ctl)` and return true from `requiresAfterSolve()`; see
  [host requirements](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#host-controller-requirements).
- **Own rate or exclusivity defaults**: override `defaultRateHz()` /
  `defaultExclusive()` (see [common policy settings](https://github.com/isri-aist/mc_nn/blob/main/README.md#common-policy-settings)
  and [exclusivity](https://github.com/isri-aist/mc_nn/blob/main/README.md#exclusivity-and-multiple-policies)).
- **Several models, or a model loaded differently**: derive from `MCNNContract`
  instead of `MCNN`, and create your `MCNNModel`s in `load()` (see
  [MCNNContract hooks](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#full-set-mcnncontract-hooks)).
- **Skip GUI/log-only computations** when RunNN's `gui` / `logs` are false:
  check `guiEnabled()` / `logsEnabled()` (see
  [GUI and runtime controls](https://github.com/isri-aist/mc_nn/blob/main/README.md#gui-and-runtime-controls)).

See [mc_nn_SafeCBFTorquePolicyContract](https://github.com/isri-aist/mc_nn_SafeCBFTorquePolicyContract) for a contract using `MCNNContract` directly with
`update`, `afterSolve`, a model created in `load()`, and a larger GUI.

## Start your own contract from this one

1. Copy `contracts/RandomPolicyContract/` to `contracts/MyPolicy/` and rename the files,
   the class and the name in `REGISTER_MC_NN_CONTRACT("MyPolicy", MyPolicy)`.
2. Rename the contract in its `CMakeLists.txt` (`mc_nn_add_contract(MyPolicy ...)`)
   and add `add_subdirectory(MyPolicy)` to `contracts/CMakeLists.txt`.
3. Replace every `YOUR CONTRACT:` part, and document your YAML fields in its README.
4. Build and install mc_nn, then select it with `contract: MyPolicy`; see
   [Add a contract](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#add-a-contract)
   and [External contracts](https://github.com/isri-aist/mc_nn/blob/main/contracts/README.md#external-contracts)
   for the complete build, registration and installation workflow.
