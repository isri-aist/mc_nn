#include "RandomPolicyContract.h"

// REGISTER_MC_NN_CONTRACT (bottom of this file) makes the contract available to
// RunNN under a YAML name: `contract: RandomPolicyContract`.
#include "mc_nn/MCNNRegistry.h"

#include <mc_rtc/gui/Label.h>
#include <mc_rtc/logging.h>

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

void RandomPolicyContract::configurePolicy(const mc_rtc::Configuration & config)
{
  // `config` is the whole YAML policy entry. RunNN already read the common
  // fields (onnx, prefix, contract, policy_hz, timeout, blocking, exclusive,
  // device); the contract reads only its own ones. Keep defaults here so a
  // field missing from the YAML is never an error, and document every field
  // in README.md.
  seed_ = config("seed", 0u); // 0 = different random inputs on every run
  input_min_ = config("input_min", -1.0);
  input_max_ = config("input_max", 1.0);
  print_every_ = config("print_every", 1u);
  finish_after_ = config("finish_after", 0u); // 0 = never finish

  // Validate early: an exception here stops the RunNN state before anything moves.
  if(input_min_ > input_max_)
  {
    mc_rtc::log::error_and_throw<std::runtime_error>("[RandomPolicyContract:{}] input_min must be <= input_max", info().id);
  }
  if(print_every_ == 0)
  {
    mc_rtc::log::error_and_throw<std::runtime_error>("[RandomPolicyContract:{}] print_every must be at least 1", info().id);
  }

  // YOUR CONTRACT: read joint lists, task gains, frames, object names...
}

void RandomPolicyContract::startPolicy(mc_control::fsm::Controller & ctl)
{
  // Reset the runtime state: startPolicy runs again after every pause/play.
  generator_.seed(seed_ != 0 ? seed_ : std::random_device{}());
  steps_ = 0;
  last_action_norm_ = 0.0;

  // The model is loaded now: its sizes come from the ONNX file.
  mc_rtc::log::info("[RandomPolicyContract:{}] Started on robot '{}': {} random observations -> {} actions at {:.1f} Hz",
                    info().id, ctl.robot().name(), expectedObservationSize(), expectedActionSize(), policyHz());

  // Optional: reserve once so that inference steps never allocate.
  input_buffer_.reserve(expectedObservationSize());
  output_buffer_.reserve(expectedActionSize());

  // YOUR CONTRACT: check that the robot matches the model (joint count/order,
  // frames, sensors) and throw if not; then create tasks, e.g.
  //   posture_task_ = std::make_shared<mc_tasks::PostureTask>(ctl.solver(), ctl.robot().robotIndex(), stiffness, weight);
  //   ctl.solver().addTask(posture_task_);
}

void RandomPolicyContract::buildInputs(mc_control::fsm::Controller &)
{
  // YOUR CONTRACT: replace the random values by the observations the model was
  // trained with, in exactly the training order (joint positions/velocities,
  // previous actions, object poses, commands...), read from ctl.robot(),
  // ctl.realRobot(), ctl.datastore()...
  std::uniform_real_distribution<float> distribution(static_cast<float>(input_min_), static_cast<float>(input_max_));
  for(size_t i = 0; i < expectedObservationSize(); ++i) { input_buffer_.push_back(distribution(generator_)); }
  // MCNN checks input_buffer_.size() against the model after this call.
}

void RandomPolicyContract::applyActions(mc_control::fsm::Controller &)
{
  ++steps_;
  double squaredNorm = 0.0;
  for(const float value : output_buffer_) { squaredNorm += static_cast<double>(value) * value; }
  last_action_norm_ = std::sqrt(squaredNorm);

  // YOUR CONTRACT: turn output_buffer_ into robot commands instead, e.g. scale
  // the actions to joint targets and update your tasks:
  //   posture_task_->target(targets);
  if(steps_ % print_every_ == 0)
  {
    std::ostringstream actions;
    actions << std::fixed << std::setprecision(4);
    for(size_t i = 0; i < output_buffer_.size(); ++i) { actions << (i == 0 ? "" : ", ") << output_buffer_[i]; }
    mc_rtc::log::info("[RandomPolicyContract:{}] step {} | action norm {:.4f} | actions [{}]", info().id, steps_,
                      last_action_norm_, actions.str());
  }
}

bool RandomPolicyContract::isPolicyFinished(mc_control::fsm::Controller &)
{
  // YOUR CONTRACT: return true when the task is achieved (e.g. object at goal
  // for some time). RunNN then outputs "<policy id>(OK)" once every blocking
  // policy is finished. Returning false forever keeps the state running (use
  // `timeout` in the YAML to bound it).
  return finish_after_ > 0 && steps_ >= finish_after_;
}

void RandomPolicyContract::teardownPolicy(mc_control::fsm::Controller &)
{
  // YOUR CONTRACT: remove every task added in startPolicy and restore anything
  // it changed. Guard each step (e.g. `if(posture_task_)`): teardown also runs
  // after a startPolicy that threw halfway.
  mc_rtc::log::info("[RandomPolicyContract:{}] Stopped after {} steps", info().id, steps_);
}

void RandomPolicyContract::addGui(mc_control::fsm::Controller & ctl, const std::vector<std::string> & category)
{
  // RunNN already shows the policy status, rates, sizes and the pause/play,
  // remove, reload and rate controls. Add only what is specific to the
  // contract. The lambdas may be called at any time while the policy runs.
  ctl.gui()->addElement(category,
                        mc_rtc::gui::Label("Inference steps", [this]() { return std::to_string(steps_); }),
                        mc_rtc::gui::Label("Last action norm", [this]() { return last_action_norm_; }),
                        mc_rtc::gui::Label("Input range", [this]()
                                           { return "[" + std::to_string(input_min_) + ", " + std::to_string(input_max_) + "]"; }));
}

void RandomPolicyContract::addLog(mc_control::fsm::Controller & ctl, const std::string & prefix)
{
  // RunNN already logs the observation, action, rates and update count as
  // MCNN_<policy id>_*. Use `this` as the source: RunNN removes the entries
  // with ctl.logger().removeLogEntries(this) when the policy stops.
  ctl.logger().addLogEntry(prefix + "action_norm", this, [this]() { return last_action_norm_; });
}

REGISTER_MC_NN_CONTRACT("RandomPolicyContract", RandomPolicyContract)
