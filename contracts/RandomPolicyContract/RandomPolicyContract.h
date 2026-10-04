#pragma once

// RandomPolicyContract: the reference/template contract of mc_nn.
//
// It runs ANY single-input ONNX model: every inference step feeds the model
// uniformly random observations and prints the resulting actions in the
// terminal. It creates no task and never moves the robot, so it is safe to try
// on any model and any robot.
//
// Copy this directory to start a new contract. Every place where a real
// contract adds its own behaviour is marked with "YOUR CONTRACT:".
// See README.md next to this file for the full pipeline.

// MCNN is the ready-made contract base for "one ONNX model, one
// observation -> inference -> action step". It already implements the model
// loading, the inference and the size checks. For contracts that need more
// (several models, work every controller tick, work after the QP solve...),
// derive from MCNNContract (mc_nn/MCNNContract.h) instead.
#include "mc_nn/MCNN.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

struct RandomPolicyContract : MCNN
{
protected:
  // ------------------------------------------------------------------------
  // Required MCNN hooks, in the order RunNN calls them.
  // ------------------------------------------------------------------------

  /**
   * Read the contract-specific YAML fields of the policy entry.
   * Called once per policy when the RunNN state starts, before the model is
   * loaded and before any controller access: only parse and validate here.
   */
  void configurePolicy(const mc_rtc::Configuration & config) override;

  /**
   * Called when the policy is launched or played. The model is loaded, so the
   * observation/action sizes are known (expectedObservationSize() /
   * expectedActionSize()). A real contract validates the robot here (joints,
   * frames, sensors) and creates its tasks.
   */
  void startPolicy(mc_control::fsm::Controller & ctl) override;

  /**
   * Called at the policy rate, right before inference. Must append exactly
   * expectedObservationSize() floats to input_buffer_ (cleared by MCNN).
   */
  void buildInputs(mc_control::fsm::Controller & ctl) override;

  /**
   * Called at the policy rate, right after inference. output_buffer_ holds
   * expectedActionSize() floats produced by the model.
   */
  void applyActions(mc_control::fsm::Controller & ctl) override;

  /** Called every controller tick while running: true when this policy is done. */
  bool isPolicyFinished(mc_control::fsm::Controller & ctl) override;

  /**
   * Called when the policy is paused, removed or the RunNN state ends (and
   * after a failed startPolicy). Undo everything startPolicy did.
   */
  void teardownPolicy(mc_control::fsm::Controller & ctl) override;

public:
  // ------------------------------------------------------------------------
  // Optional MCNNContract hooks. Every hook has a default; override only what
  // you need. Here: a small GUI and one log entry, to show how they plug into
  // RunNN. Both are skipped by RunNN when its `gui` / `logs` settings are false.
  // ------------------------------------------------------------------------

  /** Elements added under `category` (MCNN / <state> / <policy id> / RandomPolicyContract). */
  void addGui(mc_control::fsm::Controller & ctl, const std::vector<std::string> & category) override;

  /** Log entries named prefix + name ("MCNN_<policy id>_"), with `this` as source. */
  void addLog(mc_control::fsm::Controller & ctl, const std::string & prefix) override;

private:
  // Contract-specific YAML fields (see configurePolicy for their meaning).
  unsigned int seed_ = 0;
  double input_min_ = -1.0;
  double input_max_ = 1.0;
  unsigned int print_every_ = 1;
  unsigned int finish_after_ = 0;

  // Runtime state, reset by startPolicy.
  std::mt19937 generator_;
  uint64_t steps_ = 0;
  double last_action_norm_ = 0.0;
};
