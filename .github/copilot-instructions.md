# mc_nn Copilot instructions

## Build and validation

- Prefer Nix for reproducible builds and CI-equivalent validation:

  ```bash
  nix flake check -L
  nix build .#mc-nn -L
  ```

- Enter the project development environment before using direct CMake commands:

  ```bash
  nix develop .#mc-nn
  ```

  This shell provides the required mc_rtc development environment and build tools.
- If Nix is unavailable, assume the environment already provides the required dependencies and use CMake directly.
- This is a C++17 CMake project. The repository bundles ONNX Runtime 1.25.1 for the default CPU build.
- For targeted local iteration, configure, build, and install from the repository root:

  ```bash
  cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build build
  cmake --install build
  ```

- To use a separately installed ONNX Runtime, configure with `-DONNXRUNTIME_ROOT=/path/to/onnxruntime`.
- There are no CTest targets, unit-test sources, or linter configuration in this repository, so no single-test command exists. Validate C++ changes with the narrowest affected CMake target, for example `cmake --build build --target MCNN`, `RunNN`, or `mcnn_contract_RandomPolicyContract`.

## Code formatting

Formatting is done using:

- For Nix code (flake.nix)

  ```
  nix fmt
  ```

- For all other code (cmake, cpp, etc):

  - If `prek` is available:

  ```
  prek run --all-files
  ```

  - Otherwise, use `pre-commit` from `pip`:

  ```
  pre-commit run --all-files
  ```

## Architecture

- `libMCNN.so` is the shared core (`src/mc_nn/`): `MCNNModel` wraps ONNX Runtime, `MCNNContract` and `MCNN` define policy interfaces, and `MCNNRegistry` discovers factories.
- `RunNN` (`src/states/RunNN.cpp`) is the reusable mc_rtc FSM state. It merges inherited YAML configuration, finds `.onnx` files, dynamically loads contract libraries, creates one contract instance per selected policy, controls scheduling/lifecycle, and owns common GUI/logging.
- The `MCNN` controller (`src/MCNNController.*`) is a small host implementation. It publishes required host capabilities through the mc_rtc datastore and invokes the registered post-solve callback after the FSM controller's QP solve. `RunNN` can instead be installed into another compatible FSM controller.
- Each directory under `contracts/` produces an optional shared plugin (`libmcnn_contract_<Name>.so`). Plugins self-register with `REGISTER_MC_NN_CONTRACT`; do not add a core registry entry when adding a contract. The reusable CMake function `mc_nn_add_contract` also supports contracts built in external projects through `find_package(mc_nn)`.
- Build/install wiring lives in top-level `CMakeLists.txt`, `src/CMakeLists.txt`, and `cmake/MCNNContract.cmake`. The project installs the core library, the `RunNN` state, configured state YAML, contract libraries, and bundled `.onnx`/`.yaml` policy assets.

## Contract and runtime conventions

- Put policy-specific behavior in a contract, not in `RunNN`: observation layout, normalization, joint mappings, action interpretation, tasks, constraints, datastore use, and sidecar policy files all belong to the contract.
- Prefer deriving from `MCNN` for a single ONNX input/inference/output path. Implement `configurePolicy`, `startPolicy`, `buildInputs`, `applyActions`, and `teardownPolicy`; append observations to `input_buffer_` in training order and consume `output_buffer_` only after model-size validation. Derive directly from `MCNNContract` only for custom or multi-model execution, and manage `MCNNModel` loading/running yourself.
- `configure()` and `load()` run without a controller. Create or validate controller resources in `start()`, and make `teardown()` idempotently undo every resource or setting changed by `start()`: it is also called after a failed start.
- Pause is a full teardown, not suspension; playing calls `start()` again. `RunNN` owns contract GUI/log cleanup, and may rebuild GUI entries, so `addGui()` must be safe to call again.
- A contract instance belongs to exactly one policy deployment; never use shared mutable contract state to coordinate policies. Heavy model resources can be loaded lazily or by YAML `preload: true`.
- Contracts that return `true` from `requiresAfterSolve()` require a host that declares `MCNN::HostSupportsAfterSolve` and calls `MCNN::AfterSolve` after solving. Use the string keys and parsing helpers in `mc_nn/MCNNHost.h`, rather than duplicating them.

## YAML policy selection

- `models_dirs` accepts a string or list. Relative paths are resolved below the installed policies directory; discovery is recursive and skips `files_backup` and `videos`.
- Each `policies` entry needs both `contract` and `onnx`. An `onnx` wildcard with `/` matches the model path relative to its `models_dirs` entry; a wildcard without `/` matches only the filename stem.
- Model stems must be unambiguous across the selected directories. To deploy the same model more than once, give each policy group a distinct `prefix`, since `prefix + stem` is the policy ID.
- `policy_hz: 0` uses the contract's `defaultRateHz()`; a non-positive effective default runs every controller tick. `timeout` cannot be negative. Exclusive policies cannot start alongside another active policy.
- Keep the default contract directory and any extra `contracts_dirs` aligned with installed plugin locations. `MCNNRegistry` loads `.so` files once and deliberately keeps their handles open for the lifetime of their contract instances.
