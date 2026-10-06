# mc_nn contracts

A **contract** is a reusable C++ interface between a neural network and mc_rtc.
It builds the inputs, interprets the outputs and manages the resources the
network needs: tasks, constraints, datastore entries or other controller state.
A **policy** is a model deployed with a contract and a configuration; the term
also covers estimation, perception and supervision networks.

This guide covers deployment settings and the contract API. For installation
and a first inference test, start with the [mc_nn README](../README.md#quick-start).

## Table of contents

- [Responsibilities and examples](#responsibilities-and-examples)
- [Directory layout](#directory-layout)
- [Lifecycle](#lifecycle)
- [Hooks reference](#hooks-reference)
- [Single-model hooks](#minimal-mcnn-hooks)
- [Full contract hooks](#full-set-mcnncontract-hooks)
- [ONNX models](#onnx-models)
- [YAML configuration](#yaml-configuration)
- [Model discovery and policy IDs](#finding-models)
- [GUI](#gui)
- [Exclusive policies](#exclusive-policies)
- [Completion, FSM outputs and transitions](#completion-fsm-outputs-and-transitions)
- [Multiple policies](#multiple-policies)
- [Host controller requirements](#host-controller-requirements)
- [Add a contract](#add-a-contract)
- [External contracts](#external-contracts)
- [Contract guidelines](#contract-guidelines)

**Using an existing contract?** Start with [YAML configuration](#yaml-configuration)
and check its [host requirements](#host-controller-requirements).
**Writing an interface?** Choose a base in [Hooks reference](#hooks-reference),
then follow [Add a contract](#add-a-contract) or [External contracts](#external-contracts).

## Responsibilities and examples

mc_nn is the common core. The `RunNN` FSM state owns everything shared by all
policies, and each contract only plugs in what is specific to it:

| mc_nn core (`RunNN`, `src/mc_nn/`) | Contract (`contracts/<Name>/`) |
| --- | --- |
| Finding `.onnx` files, policy ids, loading contract libraries | Observations, action interpretation, joint mapping |
| ONNX Runtime session (`MCNNModel`), execution provider | Tasks, constraints, datastore entries it needs |
| Scheduling at `policy_hz`, rate warnings, timeouts | Its own completion condition |
| Running list: launch, pause/play, remove, reload | Contract-specific GUI (e.g. a QP on/off toggle) |
| Exclusivity, blocking, FSM completion and outputs | Contract-specific log entries |
| Common GUI and common logs (observation, action, rates) | Side files next to its models (e.g. `policy.yaml`) |

Interfacing details (observation layouts, joint groups, conventions, mappings)
always belong to the contract, never to the core.

Reference contracts:

- [`RandomPolicyContract`](RandomPolicyContract/README.md): minimal, heavily commented template.
  Sends random inputs and prints outputs; it does not control the robot. Start
  here when writing a single-model interface.
- [`PostureTaskPolicyContract`](PostureTaskPolicyContract/README.md): tutorial
  contract that maps model outputs to selected joint posture targets. Its
  observations are random; use it to learn task integration, not as a trained policy.
- [mc_nn_SafeCBFTorquePolicyContract](https://github.com/isri-aist/mc_nn_SafeCBFTorquePolicyContract):
  an [external contract](#external-contracts) for torque control through a CBF-QP,
  using the advanced hooks. Its safety mechanisms belong to that contract,
  not to mc_nn itself.

## Directory layout

```text
mc_nn/
  src/mc_nn/          core: MCNNContract, MCNN (single-model helper), MCNNModel, MCNNRegistry, MCNNHost
  src/states/RunNN.*  the FSM state
  contracts/<Name>/   one directory per contract, built as libmcnn_contract_<Name>.so
    RandomPolicyContract/
    PostureTaskPolicyContract/
  policies/           bundled models (models can live anywhere, see models_dirs)
```

Each contract is an optional shared library installed in
`<install prefix>/lib/mc_nn_contracts/`. `RunNN` loads every library found there
(plus `contracts_dirs`) and each contract registers itself, so adding a contract
never changes the core. Contracts can also live in your own project, outside
mc_nn (see [External contracts](#external-contracts)). A contract whose dependencies are missing is skipped at
CMake time with a message; `-DMC_NN_CONTRACT_<Name>=OFF` disables one explicitly.

## Lifecycle

| Event | Calls and effects |
| --- | --- |
| State starts | Load contract libraries, discover models, create one instance per policy, then call `configure(yaml)` without controller access. |
| Preload or first launch | Call `load()` to create heavy resources such as ONNX sessions. |
| Launch or play | Call `start(ctl)`, then add the enabled GUI and log entries. |
| Every running tick | Call `update(ctl, dt)`, then `step(ctl)` if due at the policy rate, then check `finished(ctl)`. |
| After the host QP solve | Call `afterSolve(ctl)` for contracts requesting it, when the host supports it. |
| Pause, remove or state end | Call `teardown(ctl)` and remove managed GUI and log entries. |
| Reload | Create a new instance, configure and load it, then restart it if it was running. |

Policies never share a contract instance, even when they use the same contract
and configuration. **Pause is not a suspended execution context:** play calls
startup again, so cleanup and restart must be safe for your contract.

## Hooks reference

Two base classes are available:

| Base | Choose it for | Provided by the base |
| --- | --- | --- |
| [MCNN](../src/mc_nn/MCNN.h) | One model with an input/inference/output step | Model loading, inference and vector-size checks; implement five required policy hooks. |
| [MCNNContract](../src/mc_nn/MCNNContract.h) | Custom execution, including several coordinated models | Optional lifecycle hooks; you manage model loading and execution. |

`MCNN` derives from `MCNNContract`, so an `MCNN` contract can also override any
optional hook below, including per-tick and post-solve work. Use `MCNN` unless
you need to replace its single-model execution path.

### Minimal: `MCNN` hooks

| Hook | Required | Called | Purpose |
| --- | --- | --- | --- |
| `configurePolicy(config)` | yes | once, at RunNN start | Read the contract-specific YAML fields. No controller access. |
| `startPolicy(ctl)` | yes | launch / play | Validate the robot against the model sizes, create tasks. |
| `buildInputs(ctl)` | yes | policy rate, before inference | Append `expectedObservationSize()` floats to `input_buffer_`, in training order. |
| `applyActions(ctl)` | yes | policy rate, after inference | Read `output_buffer_` (`expectedActionSize()` floats) and command the robot. |
| `teardownPolicy(ctl)` | yes | pause / remove / state end / failed start | Remove everything `startPolicy` created. |
| `isPolicyFinished(ctl)` | no (default `false`) | every tick | Return true when the policy reached its goal. |

Available in these hooks: `input_buffer_`, `output_buffer_`,
`expectedObservationSize()`, `expectedActionSize()`, plus the contract helpers
listed below.

### Full set: `MCNNContract` hooks

Every hook has a do-nothing (or safe) default.

#### Configuration and loading

No controller is available during configuration or loading.

| Hook | Default | Level | Purpose |
| --- | --- | --- | --- |
| `configure(config)` | nothing | minimal | Read contract-specific YAML. (`MCNN` forwards it to `configurePolicy`.) |
| `load()` / `loaded()` | `true` | minimal if you run a model | Load heavy resources, e.g. an `MCNNModel`. Return false on failure: RunNN refuses to start the policy. |
| `defaultRateHz()` | `30.0` | optional | Rate used when YAML `policy_hz` is `0`. `<= 0` means every tick. |
| `defaultExclusive()` | `false` | optional | Exclusivity when YAML `exclusive` is absent. |
| `requiresAfterSolve()` | `false` | advanced | Ask for `afterSolve()`; needs host support (see [Host controller requirements](#host-controller-requirements)). |

#### Runtime

| Hook | Default | Level | Purpose |
| --- | --- | --- | --- |
| `start(ctl)` | nothing | minimal | Launch / play: validate, create tasks and constraints. |
| `step(ctl)` | nothing | minimal | One inference step, at the policy rate. |
| `teardown(ctl)` | nothing | minimal | Undo `start`. Also called when `start` throws, so only undo what was done. |
| `finished(ctl)` | `false` | optional | Completion condition, evaluated every tick. |
| `update(ctl, dt)` | nothing | advanced | Every controller tick while running, before `step`. For work between inferences (hold targets, read inputs, filters...). |
| `afterSolve(ctl)` | nothing | advanced | After every QP solve while running (e.g. write torques that bypass the QP). |

#### GUI and logs

`addGui` and `addLog` are skipped when RunNN's respective `gui` or `logs`
setting is false.

| Hook | Default | Level | Purpose |
| --- | --- | --- | --- |
| `addGui(ctl, category)` | nothing | optional | Contract-specific GUI under `category`. RunNN removes it on pause/remove and may call it again when it rebuilds its GUI. |
| `addLog(ctl, prefix)` | nothing | optional | Log entries named `prefix + name` with `this` as source. RunNN removes them on pause/remove. |
| `observationSize()`, `actionSize()` | `0` | optional | Shown in the common GUI. |
| `observation()`, `action()` | empty | optional | Latest vectors, logged as `MCNN_<id>_observation/action`. |

#### Contract helpers

RunNN sets these values before calling the contract's hooks.

| Helper | Returns |
| --- | --- |
| `info()` | `id` (prefix + file name), `contract`, `modelPath`, `folder` (model directory), `device` |
| `policyHz()` | Effective inference rate in Hz (`1 / timestep` when running every tick) |
| `guiEnabled()`, `logsEnabled()` | RunNN's `gui` / `logs` settings, to skip GUI- or log-only computations |

A contract may still add GUI elements or log entries by itself outside
`addGui`/`addLog`; that always works, but RunNN does not manage or remove them.

#### Direct model access

When deriving from `MCNNContract`, keep an
[MCNNModel](../src/mc_nn/MCNNModel.h) and create it in `load()`. For example:

```cpp
model_ = std::make_shared<MCNNModel>(info().modelPath, info().device, info().id); // throws on error
Eigen::VectorXd action = model_->predict(observation); // or model_->run(std::vector<float>, std::vector<float>&)
```

## ONNX models

The `MCNNModel` wrapper supports:

| Property | Requirement |
| --- | --- |
| Input | Exactly one float tensor. |
| Input shape | `[obs]`, `[batch, obs]`, `[obs, batch]` or `[batch, d1, ...]`; batch is 1 or dynamic, other dimensions are static. |
| Selected output | The output named `actions`, or the only output if none has that name. |
| Load-time check | A zero-input inference validates the declared shapes. |

`device` selects `cpu`, `cuda` or `auto`. The vendored ONNX Runtime 1.25.1 is a
**CPU build**: CUDA needs a runtime build with the CUDA provider and its
dependencies. `cuda` fails when unavailable; `auto` attempts CUDA when supported
and otherwise falls back to CPU.

Each `MCNNModel` owns its own environment and session. Sharing the runtime
library does not share model instances. Shape checks also do not validate
observation ordering, normalization or action semantics; validate those in
your contract.

## YAML configuration

Derive from `RunNNBase` to use mc_nn's installed `policies/` as the default
model directory, or override `models_dirs` with your own paths.

Configuration has three layers: **state settings** for discovery and operator
tools, **common policy settings** for scheduling and completion, and
**contract-specific settings** for the interface itself. The example below
shows all three; model names, paths and the 50 Hz rate are illustrative, not
defaults. `exclusive` defaults to the contract's `defaultExclusive()` unless
explicitly set.

```yaml
states:
  MyPolicies:
    base: RunNNBase
    # ---------------- Global RunNN configuration
    models_dirs: [/home/me/my_models] # searched recursively; relative = inside mc_nn's installed policies/
    contracts_dirs: []   # extra folders of contract libraries (external contracts)
    preload: false       # true: load every model when the state starts; false: when launched
    verbose: 1           # 1: rate warnings; 0: silent
    gui: true            # false: no RunNN GUI at all (contracts' addGui skipped)
    logs: true           # false: no RunNN log entries (contracts' addLog skipped)
    active_policies: [walk_v1] # ids or wildcard patterns running at state start
    # ---------------- Policy configurations
    policies:
      - onnx: ["walk_*"]   # .onnx names without extension, or wildcard patterns
        prefix: ""         # policy id = prefix + .onnx name
        contract: RandomPolicyContract
        # -- Common contract configuration (read by RunNN)
        policy_hz: 50.0   # 0 = contract default, -1 = every controller tick
        timeout: 0.0      # [s], 0 = never
        blocking: true    # must finish or time out before RunNN completes
        exclusive: false  # default comes from the contract
        device: auto      # cpu | cuda | auto
        # -- Contract-specific configuration (here: RandomPolicyContract)
        seed: 0
        print_every: 10
```

### Finding models

- Every `models_dirs` entry is searched recursively for `.onnx` files;
  `files_backup/` and `videos/` directories are skipped. Models can live anywhere
  on the computer.
- A pattern without `/` matches the file name (`walk_*`, `*`); a pattern with `/`
  matches the path relative to its `models_dirs` entry, one level per `/`
  (`2026-*/walk_*`).
- All models matched by one entry share its contract and configuration, but
  each one gets its own independent contract instance.
- The policy id is `prefix` + file name. It identifies the policy in
  `active_policies`, the GUI, the logs (`MCNN_<id>_...`) and FSM outputs.

Errors at state start: a pattern matching nothing; a selected file name found in
two places (ambiguous); the same id selected by two entries. To run one model
with two configurations, give the entries different prefixes:

```yaml
- onnx: ["walk_v1"]
  prefix: ""           # id: walk_v1
  ...
- onnx: ["walk_v1"]
  prefix: "slow_"      # id: slow_walk_v1
  policy_hz: 25.0
  ...
```

## GUI

Everything is under `MCNN / <state name>`:

```text
MCNN / MyPolicies
  Configured policies, Loaded models, Running policies, Paused policies
  Completion: "Not complete: 1 paused blocking policy; waiting for '...'"
  Ready policies [dropdown]
  Exclusive: "This policy is set as exclusive and will pause all others if launched"
  [Launch]
  - <id>: Running | 49.9/50.0 Hz | running, not finished      (one line per running/paused policy)
MCNN / MyPolicies / <id>
  Policy, Status, Run speed [Hz] (desired), Completion, Elapsed, Timeout, Blocking,
  Exclusive, Inference updates, Observation / action size, Model
  [Pause|Play] [Remove] [Reload]
  Rate [Hz] [field][button]    Rate values: 0 = contract default, -1 = every tick
  Contract
MCNN / MyPolicies / <id> / <contract>       contract-specific GUI (while running)
```

- **Launch** adds a ready policy to the running list and starts it.
- **Pause** tears the contract down (its tasks leave the QP); **Play** starts it
  again from scratch.
- **Remove** tears it down and returns it to the ready list; the model stays loaded.
- **Reload** creates a new instance, re-reads the model and the contract's files,
  and restarts it if it was running.
- **Rate** changes the inference rate at runtime.

With `gui: false`, none of this is created (and contracts' `addGui` is not
called); policies still run from `active_policies`.

## Exclusive policies

An exclusive policy pauses every other running policy when it starts. Starting
any policy while an exclusive one runs pauses the exclusive one. What ran before
is not remembered: after an exclusive policy, only the policy you play runs.
`active_policies` may not list an exclusive policy together with other ones.

A contract sets the default (`defaultExclusive()`); YAML `exclusive` overrides
it. Use it for contracts that take over the whole robot, like
`mc_nn_SafeCBFTorquePolicyContract`, which drives every joint in torque.

## Completion, FSM outputs and transitions

Each policy has an independent scheduler, completion condition and timeout. On
every controller cycle, `RunNN` completes when:

- at least one policy is in the running list, and
- no **paused blocking** policy is in the running list, and
- every running **blocking** policy satisfies `finished() || timedOut`.

Non-blocking policies are evaluated but ignored by this condition. The current
reason ("no policy running", "N paused blocking policies", "waiting for ...") is
shown in the GUI. Completion is not latched and has no side effect on inference.
`RunNN` returns `SKIP` if an `active_policies` model failed to load.

mc_rtc states expose one output string. `RunNN` joins the ids of running
policies whose `finished()` returns true, in running-list order:

```text
policy_a(OK), policy_b(OK)
```

Use that exact string in a transition (`OK` when none reported completion):

```yaml
transitions:
  - [MyPolicies, "policy_a(OK), policy_b(OK)", NextState, Auto]
  - [MyPolicies, "policy_a(OK)", PolicyBNotReadyState, Auto]
  - [MyPolicies, SKIP, RecoveryState, Auto]
```

A timeout satisfies a blocking policy's condition but does not produce
`id(OK)`. The FSM consults the output only after `RunNN::run()` returns `true`,
so these are final combined results. A policy with `timeout: 0.0` and the
default completion hook keeps the state running indefinitely.

## Multiple policies

Running policies are stepped in running-list order, but solver tasks are not
"last writer wins". If two policies add separate tasks, both remain in the QP;
use task weights, stiffness, active joints or task dimensions to manage their
interaction, or make one of them exclusive.

## Host controller requirements

`RunNN` works in any mc_rtc FSM controller. The `MCNN` controller shipped with
mc_nn is a minimal one. When mc_nn is installed alongside mc_rtc, use these
paths in your controller's CMake-configured FSM YAML template:

```yaml
StatesLibraries:
- "@MC_STATES_DEFAULT_RUNTIME_INSTALL_PREFIX@"
- "@MC_STATES_RUNTIME_INSTALL_PREFIX@"
- "@MC_STATES_DEFAULT_RUNTIME_INSTALL_PREFIX@/../../MCNN/states"
StatesFiles:
- "@MC_STATES_DEFAULT_RUNTIME_INSTALL_PREFIX@/data"
- "@MC_STATES_RUNTIME_INSTALL_PREFIX@/data"
- "@MC_STATES_DEFAULT_RUNTIME_INSTALL_PREFIX@/../../MCNN/states/data"
```

For a custom mc_nn installation, replace the two MCNN entries with
`<mc_nn install prefix>/lib/mc_controller/MCNN/states` and its `/data` directory.

Contracts that need the controller's cooperation go through the datastore, so
the host does not need to link against mc_nn (see
[MCNNHost.h](../src/mc_nn/MCNNHost.h)):

```cpp
// Host constructor
datastore().make<std::string>("MCNN::FeedbackType", "<FeedbackType used in run()>");
datastore().make<bool>("MCNN::HostSupportsAfterSolve", true);

// Host run()
bool ok = mc_control::fsm::Controller::run(feedbackType);
if(datastore().has("MCNN::AfterSolve")) { datastore().call("MCNN::AfterSolve"); }
return ok;
```

- **Post-solve step**: `RunNN` refuses to start a contract with
  `requiresAfterSolve()` on a host that does not declare
  `"MCNN::HostSupportsAfterSolve"`.
- **Solver feedback**: contracts can read `"MCNN::FeedbackType"` to check the
  host runs the feedback they were designed for.

The `MCNN` controller implements both, with the `FeedbackType` key of its
configuration ([etc/MCNN.in.yaml](../etc/MCNN.in.yaml)).

## Add a contract

Copy [RandomPolicyContract](RandomPolicyContract/) and follow its
`YOUR CONTRACT:` comments. For an independent package, use
[External contracts](#external-contracts) instead of adding a directory inside
mc_nn.

### 1. Implement the interface

A contract directory contains:

```text
contracts/
  MyPolicy/
    MyPolicy.h
    MyPolicy.cpp
    CMakeLists.txt
    README.md
```

The class derives from `MCNN` (or `MCNNContract`), implements its hooks, and
registers itself under its YAML name at the end of the `.cpp`:

```cpp
REGISTER_MC_NN_CONTRACT("MyPolicy", MyPolicy)
```

### 2. Build the library

In the contract's CMake file:

```cmake
mc_nn_add_contract(MyPolicy
  SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/MyPolicy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MyPolicy.h
  # INCLUDE_DIRS ...      private include directories
  # LINK ...              extra libraries, e.g. mcnn_contract_<Other> to reuse another contract
  # REQUIRES_HEADERS ...  skip the contract when a header is missing
)
```

Add the directory to [contracts/CMakeLists.txt](CMakeLists.txt), after any
contract it links to:

```cmake
add_subdirectory(MyPolicy)
```

### 3. Select it in a state

Build and install, then select it in YAML with `contract: MyPolicy`, matching
the registered name. Supply compatible models and the contract-specific
settings alongside the common policy settings.

## External contracts

A contract does not have to be inside mc_nn. mc_nn installs its headers and a
CMake package, so any project can build contracts against an installed mc_nn:

### Build against mc_nn

```text
my_contracts/
  CMakeLists.txt
  MyPolicy/
    MyPolicy.h / MyPolicy.cpp / CMakeLists.txt   (same files as inside mc_nn)
  models/                                       (optional: its .onnx files)
```

```cmake
cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 17)
project(my_contracts CXX)

find_package(mc_rtc REQUIRED)
find_package(mc_nn REQUIRED)   # -Dmc_nn_DIR=<mc_nn prefix>/lib/cmake/mc_nn, or CMAKE_PREFIX_PATH

add_subdirectory(MyPolicy)     # MyPolicy/CMakeLists.txt calls mc_nn_add_contract(MyPolicy SOURCES ...)
```

`find_package(mc_nn)` provides the `mc_nn::MCNN` target (core library, headers
such as `mc_nn/MCNN.h`, ONNX Runtime) and the same `mc_nn_add_contract()`
function, with the same options.

### Deploy the interface and models

Build and install the project, then add these settings to your `RunNN` state:

```yaml
contracts_dirs: ["<my_contracts prefix>/lib/mc_nn_contracts"]
models_dirs: ["/path/to/my_contracts/models"]
```

The contract names are then available as `contract: MyPolicy`. External
contract libraries use the `libMCNN.so` of the mc_nn installation they were
built against; rebuild them when mc_nn changes.

## Contract guidelines

### Separate model conventions from deployment tuning

Keep structural model information in C++ or files next to the model:

- joint names and ordering;
- observation ordering and normalization;
- output names and action scaling;
- required frames and task types;
- recurrent state or previous actions.

Keep deployment tuning in YAML:

- frequency and duration;
- task weights, stiffness, and damping;
- selected object names or frames;
- target poses and thresholds;
- verbosity.

### Validate and clean up

Validate the structural contract in `start`, when robots and frames are
available. Throwing there gives an immediate error instead of running a policy
with shifted observations.

Remove the resources created by `start` in `teardown`. Cleanup must also work
after a partially failed startup. Use the managed `addGui` and `addLog` hooks
so RunNN can remove their entries on pause, removal and state exit.
