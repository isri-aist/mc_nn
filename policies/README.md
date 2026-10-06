# Bundled models

Models placed here are installed to `<prefix>/lib/mc_controller/policies`, the
default `models_dirs` entry of `RunNNBase` (relative `models_dirs` entries are
resolved from there too). Models can also live anywhere else: list their
folders in `models_dirs`. Contract packages usually ship their own models.

`dummy_example/dummy_example.onnx` is a bundled inference smoke-test model for
the `RandomPolicyContract`; it receives random inputs and does not control a robot.
