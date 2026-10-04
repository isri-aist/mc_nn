#pragma once

#include <Eigen/Core>
#include <onnxruntime_cxx_api.h>

#include <memory>
#include <string>
#include <vector>

/**
 * One ONNX Runtime inference session, shared by every mc_nn contract.
 *
 * Supported models:
 * - exactly one float input: `[obs]`, `[batch, obs]`, `[obs, batch]` (batch 1
 *   or dynamic) or `[batch, d1, d2, ...]` (flattened, batch first);
 * - the output named `actions`, or the only output when there is no such name,
 *   with the same shape rules.
 *
 * `device` selects the execution provider:
 * - `cpu`: CPU only;
 * - `cuda`: CUDA, loading fails if it is unavailable;
 * - `auto`: CUDA when available, otherwise CPU.
 *
 * The constructor throws std::runtime_error when the model cannot be used.
 */
struct MCNNModel
{
  MCNNModel(const std::string & path, const std::string & device, const std::string & logName);

  /** Run one inference. `input.size()` must equal inputSize(); `output` is resized to outputSize(). */
  void run(const std::vector<float> & input, std::vector<float> & output);

  /** Eigen convenience wrapper around run(). */
  Eigen::VectorXd predict(const Eigen::VectorXd & input);

  size_t inputSize() const noexcept { return input_size_; }
  size_t outputSize() const noexcept { return output_size_; }
  /** Execution provider actually used: "CPU" or "CUDA". */
  const std::string & provider() const noexcept { return provider_; }
  const std::string & path() const noexcept { return path_; }

private:
  std::string path_;
  std::string log_name_;
  std::string provider_ = "CPU";
  std::unique_ptr<Ort::Env> env_;
  std::unique_ptr<Ort::Session> session_;
  Ort::MemoryInfo memory_info_{nullptr};
  std::string input_name_;
  std::string output_name_;
  std::vector<int64_t> input_shape_;
  size_t input_size_ = 0;
  size_t output_size_ = 0;
  std::vector<float> input_scratch_;
};
