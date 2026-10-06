#pragma once

#include "mc_nn/MCNN.h"

#include <mc_tasks/PostureTask.h>

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

/** Tutorial contract: random observations drive a posture task using ONNX actions. */
struct PostureTaskPolicyContract final : MCNN
{
protected:
  void configurePolicy(const mc_rtc::Configuration & config) override;
  void startPolicy(mc_control::fsm::Controller & ctl) override;
  void buildInputs(mc_control::fsm::Controller & ctl) override;
  void applyActions(mc_control::fsm::Controller & ctl) override;
  void teardownPolicy(mc_control::fsm::Controller & ctl) override;

private:
  std::vector<std::string> joints_;
  std::shared_ptr<mc_tasks::PostureTask> posture_task_;
  std::mt19937 generator_;
  unsigned int seed_ = 0;
  double input_min_ = -1.0;
  double input_max_ = 1.0;
  double weight_ = 1.0;
  double stiffness_ = 1.0;
};
