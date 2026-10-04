#include "MCNNModel.h"

#include <mc_rtc/logging.h>

#include <algorithm>
#include <stdexcept>

namespace
{

std::string shapeString(const std::vector<int64_t> & shape)
{
  std::string out = "[";
  for(size_t i = 0; i < shape.size(); ++i)
  {
    if(i > 0) { out += ", "; }
    out += std::to_string(shape[i]);
  }
  return out + "]";
}

/**
 * Number of values per sample for one declared tensor shape. A batch dimension
 * (size 1 or dynamic) may come first, or last for 2D transposed tensors. Every
 * other dimension must be static.
 */
size_t sampleSize(const std::vector<int64_t> & shape, const std::string & what)
{
  auto isBatch = [](int64_t d) { return d == 1 || d < 0; };
  auto fail = [&](const std::string & reason) -> size_t
  { throw std::runtime_error(what + " shape " + shapeString(shape) + ": " + reason); };

  if(shape.empty()) { return fail("scalar tensors are not supported"); }
  if(shape.size() == 1)
  {
    if(shape[0] <= 0) { return fail("a 1D tensor must have a static size"); }
    return static_cast<size_t>(shape[0]);
  }
  if(shape.size() == 2)
  {
    if(isBatch(shape[0]) && shape[1] > 0) { return static_cast<size_t>(shape[1]); } // [batch, n]
    if(isBatch(shape[1]) && shape[0] > 0) { return static_cast<size_t>(shape[0]); } // [n, batch]
    return fail("expected [batch, n] or [n, batch] with batch 1 or dynamic");
  }
  if(!isBatch(shape[0])) { return fail("the first dimension must be a batch of 1 or dynamic"); }
  size_t size = 1;
  for(size_t i = 1; i < shape.size(); ++i)
  {
    if(shape[i] <= 0) { return fail("only the batch dimension may be dynamic"); }
    size *= static_cast<size_t>(shape[i]);
  }
  return size;
}

} // namespace

MCNNModel::MCNNModel(const std::string & path, const std::string & device, const std::string & logName)
: path_(path), log_name_(logName)
{
  if(device != "cpu" && device != "cuda" && device != "auto")
  {
    throw std::runtime_error("device must be 'cpu', 'cuda' or 'auto', got '" + device + "'");
  }

  env_.reset(new Ort::Env(ORT_LOGGING_LEVEL_WARNING, log_name_.c_str()));
  Ort::SessionOptions options;
  options.SetIntraOpNumThreads(1);
  options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

  const auto providers = Ort::GetAvailableProviders();
  const bool cudaBuilt = std::find(providers.begin(), providers.end(), "CUDAExecutionProvider") != providers.end();
  if(device == "cuda" && !cudaBuilt)
  {
    throw std::runtime_error("device 'cuda' requested but this ONNX Runtime build has no CUDA execution provider");
  }
  if(device == "auto" && !cudaBuilt)
  {
    mc_rtc::log::info("[MCNN:{}] ONNX Runtime built without CUDA, using CPU for ONNX inference", log_name_);
  }
  else if(device != "cpu")
  {
    try
    {
      OrtCUDAProviderOptions cudaOptions{};
      options.AppendExecutionProvider_CUDA(cudaOptions);
      provider_ = "CUDA";
    }
    catch(const std::exception & error)
    {
      if(device == "cuda") { throw std::runtime_error(std::string("CUDA provider unavailable: ") + error.what()); }
      mc_rtc::log::info("[MCNN:{}] CUDA not available, using CPU for ONNX inference", log_name_);
    }
  }

  session_.reset(new Ort::Session(*env_, path_.c_str(), options));
  Ort::AllocatorWithDefaultOptions allocator;

  if(session_->GetInputCount() != 1)
  {
    throw std::runtime_error("ONNX models must have exactly one input, got " + std::to_string(session_->GetInputCount()));
  }
  input_name_ = session_->GetInputNameAllocated(0, allocator).get();
  input_shape_ = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
  input_size_ = sampleSize(input_shape_, "Input");
  for(auto & dimension : input_shape_) { if(dimension < 0) { dimension = 1; } }

  std::vector<std::string> outputNames;
  for(size_t i = 0; i < session_->GetOutputCount(); ++i)
  {
    outputNames.emplace_back(session_->GetOutputNameAllocated(i, allocator).get());
  }
  auto actions = std::find(outputNames.begin(), outputNames.end(), "actions");
  if(actions == outputNames.end() && outputNames.size() != 1)
  {
    throw std::runtime_error("ONNX model has several outputs and none is named 'actions'");
  }
  const size_t outputIndex = actions != outputNames.end() ? static_cast<size_t>(actions - outputNames.begin()) : 0;
  output_name_ = outputNames[outputIndex];
  output_size_ = sampleSize(session_->GetOutputTypeInfo(outputIndex).GetTensorTypeAndShapeInfo().GetShape(), "Output");

  memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

  // A zero-input inference validates the declared shapes once, before the robot uses the model.
  std::vector<float> testOutput;
  run(std::vector<float>(input_size_, 0.0f), testOutput);

  mc_rtc::log::info("[MCNN:{}] Loaded '{}' on {}: input '{}' ({} values), output '{}' ({} values)", log_name_, path_,
                    provider_, input_name_, input_size_, output_name_, output_size_);
}

void MCNNModel::run(const std::vector<float> & input, std::vector<float> & output)
{
  if(input.size() != input_size_)
  {
    throw std::runtime_error("Observation size mismatch: model expects " + std::to_string(input_size_) + ", got "
                             + std::to_string(input.size()));
  }
  input_scratch_ = input;
  auto tensor = Ort::Value::CreateTensor<float>(memory_info_, input_scratch_.data(), input_scratch_.size(),
                                                input_shape_.data(), input_shape_.size());
  const char * inputName = input_name_.c_str();
  const char * outputName = output_name_.c_str();
  auto outputs = session_->Run(Ort::RunOptions{nullptr}, &inputName, &tensor, 1, &outputName, 1);

  const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
  size_t count = 1;
  for(const auto dimension : shape) { count *= static_cast<size_t>(dimension); }
  if(count != output_size_)
  {
    throw std::runtime_error("Action size mismatch: model declares " + std::to_string(output_size_)
                             + ", inference returned " + std::to_string(count));
  }
  const float * data = outputs[0].GetTensorData<float>();
  output.assign(data, data + count);
}

Eigen::VectorXd MCNNModel::predict(const Eigen::VectorXd & input)
{
  std::vector<float> in(static_cast<size_t>(input.size()));
  for(Eigen::Index i = 0; i < input.size(); ++i) { in[static_cast<size_t>(i)] = static_cast<float>(input(i)); }
  std::vector<float> out;
  run(in, out);
  Eigen::VectorXd result(static_cast<Eigen::Index>(out.size()));
  for(size_t i = 0; i < out.size(); ++i) { result(static_cast<Eigen::Index>(i)) = out[i]; }
  return result;
}
