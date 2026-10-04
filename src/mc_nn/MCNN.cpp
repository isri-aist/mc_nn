#include "MCNN.h"

#include <mc_rtc/logging.h>

#include <stdexcept>

namespace
{

Eigen::VectorXd toEigen(const std::vector<float> & values)
{
  Eigen::VectorXd out(static_cast<Eigen::Index>(values.size()));
  for(size_t i = 0; i < values.size(); ++i) { out(static_cast<Eigen::Index>(i)) = values[i]; }
  return out;
}

} // namespace

bool MCNN::load()
{
  try
  {
    model_.reset(new MCNNModel(info().modelPath, info().device, info().id));
    return true;
  }
  catch(const std::exception & error)
  {
    mc_rtc::log::error("[MCNN:{}] Failed to load '{}': {}", info().id, info().modelPath, error.what());
    model_.reset();
    return false;
  }
}

void MCNN::step(mc_control::fsm::Controller & ctl)
{
  input_buffer_.clear();
  buildInputs(ctl);
  if(input_buffer_.size() != expectedObservationSize())
  {
    mc_rtc::log::error_and_throw<std::runtime_error>(
        "[MCNN:{}] Observation size mismatch: model expects {}, policy produced {}",
        info().id, expectedObservationSize(), input_buffer_.size());
  }

  model_->run(input_buffer_, output_buffer_);
  applyActions(ctl);
}

Eigen::VectorXd MCNN::observation() const { return toEigen(input_buffer_); }

Eigen::VectorXd MCNN::action() const { return toEigen(output_buffer_); }
