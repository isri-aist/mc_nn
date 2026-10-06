#include "PostureTaskPolicyContract.h"

#include "mc_nn/MCNNRegistry.h"

#include <mc_rbdyn/Robot.h>
#include <mc_rtc/logging.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>

void PostureTaskPolicyContract::configurePolicy(const mc_rtc::Configuration & config)
{
  config("joints", joints_);
  seed_ = config("seed", 0u);
  input_min_ = config("input_min", -1.0);
  input_max_ = config("input_max", 1.0);
  weight_ = config("weight", 1.0);
  stiffness_ = config("stiffness", 1.0);

  if(!std::isfinite(input_min_) || !std::isfinite(input_max_) || input_min_ > input_max_)
  {
    mc_rtc::log::error_and_throw<std::runtime_error>(
        "[PostureTaskPolicyContract] input_min and input_max must be finite, with input_min <= input_max");
  }
  if(!std::isfinite(weight_) || !std::isfinite(stiffness_) || weight_ <= 0.0 || stiffness_ <= 0.0)
  {
    mc_rtc::log::error_and_throw<std::runtime_error>(
        "[PostureTaskPolicyContract] weight and stiffness must be positive");
  }
}

void PostureTaskPolicyContract::startPolicy(mc_control::fsm::Controller & ctl)
{
  if(joints_.empty())
  {
    mc_rtc::log::error_and_throw<std::runtime_error>(
        "[PostureTaskPolicyContract] configure at least one joint");
  }
  if(std::set<std::string>(joints_.begin(), joints_.end()).size() != joints_.size())
  {
    mc_rtc::log::error_and_throw<std::runtime_error>(
        "[PostureTaskPolicyContract] joints contains duplicate names");
  }
  if(joints_.size() > expectedActionSize())
  {
    mc_rtc::log::error_and_throw<std::runtime_error>(
        "[PostureTaskPolicyContract] configured {} joints, but the model has only {} actions",
        joints_.size(), expectedActionSize());
  }

  const auto & robot = ctl.robot();
  for(const auto & joint : joints_)
  {
    if(!robot.hasJoint(joint))
    {
      mc_rtc::log::error_and_throw<std::runtime_error>(
          "[PostureTaskPolicyContract] joint '{}' is not present on robot '{}'", joint, robot.name());
    }
    const auto index = robot.jointIndexByName(joint);
    const auto & lower = robot.ql()[index];
    const auto & upper = robot.qu()[index];
    if(robot.alpha()[index].size() != 1 || lower.size() != 1 || upper.size() != 1
       || !std::isfinite(lower[0]) || !std::isfinite(upper[0]) || lower[0] >= upper[0])
    {
      mc_rtc::log::error_and_throw<std::runtime_error>(
          "[PostureTaskPolicyContract] joint '{}' must have one DoF and finite position limits", joint);
    }
  }

  generator_.seed(seed_ == 0 ? std::random_device{}() : seed_);
  posture_task_.reset(new mc_tasks::PostureTask(ctl.solver(), robot.robotIndex(), stiffness_, weight_));
  posture_task_->name("mcnn_posture_policy_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
  posture_task_->selectActiveJoints(ctl.solver(), joints_);
  ctl.solver().addTask(posture_task_);

  input_buffer_.reserve(expectedObservationSize());
  output_buffer_.reserve(expectedActionSize());
  mc_rtc::log::info("[PostureTaskPolicyContract:{}] Started on robot '{}': {} random observations, {} model actions, "
                    "{} controlled joints",
                    info().id, robot.name(), expectedObservationSize(), expectedActionSize(), joints_.size());
}

void PostureTaskPolicyContract::buildInputs(mc_control::fsm::Controller & ctl)
{
  // This template deliberately uses random observations, so the model can be
  // exercised without claiming that the inputs match its training data.
  std::uniform_real_distribution<float> distribution(static_cast<float>(input_min_), static_cast<float>(input_max_));
  for(size_t i = 0; i < expectedObservationSize(); ++i) { input_buffer_.push_back(distribution(generator_)); }

  // To make this a real policy, replace the random values above with the exact
  // observations and ordering used during training. For example:
  //
  //   const auto & q = ctl.robot().q()[ctl.robot().jointIndexByName("joint_name")];
  //   const auto & alpha = ctl.robot().alpha()[ctl.robot().jointIndexByName("joint_name")];
  //
  // Append the needed joint positions and velocities to input_buffer_ (convert
  // values to float). For measured/estimated robot state, use ctl.realRobot()
  // instead of ctl.robot(). For another robot in a multi-robot controller,
  // select it by name, then read its q(), alpha(), or body poses/velocities.
  // Object poses, commands, sensor readings, previous actions, and other
  // controller-specific values can be read from the relevant sensor APIs or
  // ctl.datastore(). Validate the resulting feature order and size against the
  // model during startPolicy; MCNN also checks the input size before inference.
  (void)ctl;
}

void PostureTaskPolicyContract::applyActions(mc_control::fsm::Controller & ctl)
{
  std::map<std::string, std::vector<double>> targets;
  for(size_t i = 0; i < joints_.size(); ++i)
  {
    const float action = output_buffer_[i];
    if(!std::isfinite(action))
    {
      mc_rtc::log::error_and_throw<std::runtime_error>(
          "[PostureTaskPolicyContract:{}] model produced a non-finite action at index {}", info().id, i);
    }

    // Interpret each action as a normalized position command. Clamp it to the
    // joint range convention [-1, 1], then map it to that joint's limits.
    const auto & robot = ctl.robot();
    const auto index = robot.jointIndexByName(joints_[i]);
    const double bounded = std::max(-1.0, std::min(1.0, static_cast<double>(action)));
    const double lower = robot.ql()[index][0];
    const double upper = robot.qu()[index][0];
    targets[joints_[i]] = {lower + 0.5 * (bounded + 1.0) * (upper - lower)};
  }
  posture_task_->target(targets);
}

void PostureTaskPolicyContract::teardownPolicy(mc_control::fsm::Controller & ctl)
{
  if(posture_task_)
  {
    ctl.solver().removeTask(posture_task_);
    posture_task_.reset();
  }
}

REGISTER_MC_NN_CONTRACT("PostureTaskPolicyContract", PostureTaskPolicyContract)
