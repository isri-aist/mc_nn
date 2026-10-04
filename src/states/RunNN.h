#pragma once

#include "mc_nn/MCNNContract.h"

#include <mc_control/fsm/State.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * Generic FSM state running any number of mc_nn policies.
 *
 * RunNN discovers .onnx files in `models_dirs`, gives every selected model its
 * own contract instance, and owns everything common to policies: scheduling,
 * the running list, pause/play/remove/reload, exclusivity, completion, the GUI
 * and the common logs. Contracts only implement what is specific to them.
 *
 * See contracts/README.md for the YAML schema and the full behaviour.
 */
struct RunNN : mc_control::fsm::State
{
  void configure(const mc_rtc::Configuration & config) override;
  void start(mc_control::fsm::Controller & ctl) override;
  bool run(mc_control::fsm::Controller & ctl) override;
  void teardown(mc_control::fsm::Controller & ctl) override;

private:
  enum class Status
  {
    Ready,   ///< Configured, not in the running list
    Running, ///< Started and stepped every tick
    Paused   ///< In the running list but torn down
  };

  struct Policy
  {
    MCNNPolicyInfo info;
    mc_rtc::Configuration config;
    std::unique_ptr<MCNNContract> contract;
    double desiredHz = 0.0; ///< 0 = contract default, < 0 = every controller tick
    double timeout = 0.0;
    bool blocking = true;
    bool exclusive = false;
    Status status = Status::Ready;
    bool loadAttempted = false;
    double accumulator = 0.0;
    double elapsed = 0.0;
    size_t updates = 0;
    double measuredHz = 0.0;
    std::chrono::steady_clock::time_point lastStepWallTime;
    bool hasLastStepWallTime = false;
    bool finishedNow = false;
    bool timedOut = false;
  };

  enum class Action
  {
    Launch,
    PauseOrPlay,
    Remove,
    Reload
  };

  void buildPolicies();
  /** Pass the policy identity and the gui/logs settings to its contract instance. */
  void bindContract(Policy & entry);
  Policy & policy(const std::string & id);
  bool ensureLoaded(Policy & entry);
  double effectiveHz(const Policy & entry) const;
  void applyRate(Policy & entry);

  bool startPolicy(mc_control::fsm::Controller & ctl, Policy & entry);
  void stopPolicy(mc_control::fsm::Controller & ctl, Policy & entry);
  void launch(mc_control::fsm::Controller & ctl, Policy & entry);
  void pauseOrPlay(mc_control::fsm::Controller & ctl, Policy & entry);
  void remove(mc_control::fsm::Controller & ctl, Policy & entry);
  void reload(mc_control::fsm::Controller & ctl, Policy & entry);
  void applyExclusivity(mc_control::fsm::Controller & ctl, const Policy & starting);
  void processActions(mc_control::fsm::Controller & ctl);

  void stepPolicy(mc_control::fsm::Controller & ctl, Policy & entry);
  bool evaluateCompletion(std::string & output);
  std::string completionText(const Policy & entry) const;

  void rebuildGui(mc_control::fsm::Controller & ctl);
  std::vector<std::string> policyCategory(const Policy & entry) const;
  std::vector<std::string> contractCategory(const Policy & entry) const;
  void addCommonLog(mc_control::fsm::Controller & ctl, Policy & entry);

  mc_rtc::Configuration config_;
  int verbose_ = 1;
  bool preload_ = false;
  bool gui_enabled_ = true;  ///< YAML `gui`: false skips the whole RunNN GUI, contracts' addGui included
  bool logs_enabled_ = true; ///< YAML `logs`: false skips common logs and contracts' addLog

  std::vector<std::unique_ptr<Policy>> policies_; ///< Every configured policy, in YAML order
  std::vector<Policy *> running_; ///< Running and paused policies, in launch order
  std::vector<std::pair<Action, std::string>> actions_; ///< GUI requests, applied at the next run()

  double dt_ = 0.005;
  bool load_error_ = false;
  bool gui_dirty_ = true;
  std::vector<std::string> gui_category_;
  std::string selected_;
  std::string completion_ = "No policy running";
};
