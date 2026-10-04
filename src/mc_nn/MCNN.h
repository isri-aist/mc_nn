#pragma once

#include "MCNNContract.h"
#include "MCNNModel.h"

#include <memory>
#include <string>
#include <vector>

/**
 * Ready-made contract for policies with one ONNX model and a fixed
 * observation -> inference -> action step.
 *
 * MCNN owns model loading, tensor validation and inference. A concrete policy
 * only implements the mc_rtc-specific hooks that build observations and apply
 * actions. This keeps model contracts beside their model instead of in RunNN.
 *
 * Contracts needing more (per-tick work, several models, post-solve work...)
 * derive from MCNNContract directly, or override the extra MCNNContract hooks
 * (update, afterSolve, addGui, addLog...) on top of MCNN.
 */
struct MCNN : MCNNContract
{
  void configure(const mc_rtc::Configuration & config) override { configurePolicy(config); }
  bool load() override;
  bool loaded() const override { return model_ != nullptr; }
  void start(mc_control::fsm::Controller & ctl) override { startPolicy(ctl); }
  void step(mc_control::fsm::Controller & ctl) override;
  bool finished(mc_control::fsm::Controller & ctl) override { return isPolicyFinished(ctl); }
  void teardown(mc_control::fsm::Controller & ctl) override { teardownPolicy(ctl); }

  size_t observationSize() const override { return expectedObservationSize(); }
  size_t actionSize() const override { return expectedActionSize(); }
  Eigen::VectorXd observation() const override;
  Eigen::VectorXd action() const override;

protected:
  /** Policy extension points. Only buildInputs and applyActions do real work in every policy. */
  virtual void configurePolicy(const mc_rtc::Configuration & config) = 0;
  virtual void startPolicy(mc_control::fsm::Controller & ctl) = 0;
  virtual void buildInputs(mc_control::fsm::Controller & ctl) = 0;
  virtual void applyActions(mc_control::fsm::Controller & ctl) = 0;
  /** Report whether this policy currently considers itself complete. */
  virtual bool isPolicyFinished(mc_control::fsm::Controller &) { return false; }
  virtual void teardownPolicy(mc_control::fsm::Controller & ctl) = 0;

  std::vector<float> input_buffer_;
  std::vector<float> output_buffer_;
  size_t expectedObservationSize() const noexcept { return model_ ? model_->inputSize() : 0; }
  size_t expectedActionSize() const noexcept { return model_ ? model_->outputSize() : 0; }

private:
  std::unique_ptr<MCNNModel> model_;
};
