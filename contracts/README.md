# 🧩 mc_nn contracts

A **contract** is a reusable C++ interface between a neural network and mc_rtc.
It builds the inputs, interprets the outputs and manages the resources the
network needs: tasks, constraints, datastore entries or other controller state.
A **policy** is a model deployed with a contract and a configuration; the term
also covers estimation, perception and supervision networks.

This guide is for **using, writing and packaging contracts**. For installation,
Quick start, and RunNN's common YAML settings, see the
[mc_nn README](../README.md).

## Table of contents

| Understand the contract API | Build and integrate |
| --- | --- |
| [Responsibilities and examples](#responsibilities-and-examples) · [Lifecycle](#lifecycle) · [Hooks reference](#hooks-reference) · [ONNX model requirements](#onnx-model-requirements) | [Host controller requirements](#host-controller-requirements) · [Add a contract](#add-a-contract) · [External contracts](#external-contracts) · [Contract guidelines](#contract-guidelines) |

> **Using a contract?** Configure shared model paths, policy IDs, rates, GUI
> and transitions in the [RunNN configuration guide](../README.md#runnn-configuration).
> **Writing one?** Choose a base in [Hooks reference](#hooks-reference), then
> follow [Add a contract](#add-a-contract).

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

#### ONNX model access

When deriving from `MCNNContract`, keep an
[MCNNModel](../src/mc_nn/MCNNModel.h) and create it in `load()`. For example:

```cpp
model_ = std::make_shared<MCNNModel>(info().modelPath, info().device, info().id); // throws on error
Eigen::VectorXd action = model_->predict(observation); // or model_->run(std::vector<float>, std::vector<float>&)
```

## ONNX model requirements

The `MCNNModel` wrapper supports:

| Property | Requirement |
| --- | --- |
| Input | Exactly one float tensor. |
| Input shape | `[obs]`, `[batch, obs]`, `[obs, batch]` or `[batch, d1, ...]`; batch is 1 or dynamic, other dimensions are static. |
| Selected output | The output named `actions`, or the only output if none has that name. |
| Load-time check | A zero-input inference validates the declared shapes. |

Shape checks do not validate observation ordering, normalization or action
semantics; validate those in your contract. For runtime and device details, see
[Runtime and performance](../README.md#runtime-and-performance).

## Contract library layout

An internal contract lives in `contracts/<Name>/` and is built as its own
optional shared library, `libmcnn_contract_<Name>.so`. `RunNN` loads libraries
from mc_nn's contract directory and any additional `contracts_dirs`.
Each library registers its contract name, so adding one does not require
changes to the core or host controller.

Contracts can also live in an independent project; see
[External contracts](#external-contracts). If a declared dependency is missing,
CMake skips that contract with a message. Disable an optional bundled contract
with `-DMC_NN_CONTRACT_<Name>=OFF`.

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
