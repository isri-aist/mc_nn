# RandomPolicyContract: the mc_nn contract template

`RandomPolicyContract` runs **any** single-input ONNX model: at every inference step it
feeds the model uniformly random observations and prints the resulting actions
in the terminal. It creates no task and never moves the robot, so it is safe on
any model and any robot. Use it to:

- check that a model loads and runs in mc_rtc (sizes, rate, execution provider);
- learn the mc_nn pipeline;
- start a new contract: copy this directory and replace the parts marked
  `YOUR CONTRACT:` in the source.

The full hook reference is in [`../README.md`](../README.md#hooks-reference).

## Try it

Any `.onnx` model works. Put it anywhere and point `models_dirs` to its folder:

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

## The pipeline, hook by hook

`RandomPolicyContract` derives from `MCNN`, the ready-made base for "one model, one
observation → inference → action step". mc_nn (RunNN + MCNN) does everything
except the five marked hooks:

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
- **Commanding the robot**: create an mc_rtc task in `startPolicy`, update its
  target in `applyActions`, remove it in `teardownPolicy`.
- **Work between inferences** (e.g. interpolating targets, reading a joystick):
  override `update(ctl, dt)`, called every controller tick.
- **Work after the QP solve** (e.g. writing torques directly): override
  `afterSolve(ctl)` and return true from `requiresAfterSolve()`.
- **Own rate or exclusivity defaults**: override `defaultRateHz()` /
  `defaultExclusive()`.
- **Several models, or a model loaded differently**: derive from `MCNNContract`
  instead of `MCNN`, and create your `MCNNModel`s in `load()`.
- **Skip GUI/log-only computations** when RunNN's `gui` / `logs` are false:
  check `guiEnabled()` / `logsEnabled()`.

See [`SafeCBFTorquePolicyContract`](TODO_SAFE_CBF_TORQUE_POLICY_CONTRACT_REPOSITORY_URL) for a contract using `MCNNContract` directly with
`update`, `afterSolve`, a model created in `load()`, and a larger GUI.

## Start your own contract from this one

1. Copy `contracts/RandomPolicyContract/` to `contracts/MyPolicy/` and rename the files,
   the class and the name in `REGISTER_MC_NN_CONTRACT("MyPolicy", MyPolicy)`.
2. Rename the contract in its `CMakeLists.txt` (`mc_nn_add_contract(MyPolicy ...)`)
   and add `add_subdirectory(MyPolicy)` to `contracts/CMakeLists.txt`.
3. Replace every `YOUR CONTRACT:` part, and document your YAML fields in its README.
4. Build and install mc_nn, then select it with `contract: MyPolicy`.
