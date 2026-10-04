#pragma once

#include <mc_control/fsm/Controller.h>
#include <mc_rtc/Configuration.h>

#include <Eigen/Core>

#include <string>
#include <vector>

/** Identity and resources resolved by RunNN for one policy before configure(). */
struct MCNNPolicyInfo
{
  /** Unique identifier inside a RunNN state: YAML prefix + ONNX file name without extension. */
  std::string id;
  /** Registered contract name (YAML `contract`). */
  std::string contract;
  /** Absolute path of the policy's .onnx file. */
  std::string modelPath;
  /** Directory containing the .onnx file (policy-specific side files live here). */
  std::string folder;
  /** ONNX Runtime execution provider requested in YAML: cpu, cuda or auto. */
  std::string device = "auto";
};

/**
 * Base class of every mc_nn contract.
 *
 * A contract is the C++ code that knows how to run one kind of policy on the
 * robot: which observations to build, how to interpret actions, which tasks or
 * constraints to create. RunNN owns everything common to all policies: model
 * discovery, scheduling, pause/play, exclusivity, completion, the common GUI and
 * the common logs. RunNN creates one contract instance per policy, so instances
 * never share state.
 *
 * Every hook has a do-nothing default. A contract overrides only what it needs.
 *
 * Lifecycle driven by RunNN:
 *
 * ```text
 * RunNN start    -> factory create -> configure            (YAML only, no controller)
 * preload/launch -> load                                   (heavy resources, e.g. ONNX session)
 * launch/play    -> start -> addGui -> addLog
 * every tick     -> update(dt) -> [step() at the policy rate] -> finished()
 * after QP solve -> afterSolve()                           (only if requiresAfterSolve())
 * pause/remove   -> teardown                               (GUI/log entries removed by RunNN)
 * reload         -> new instance -> configure -> load [-> start if it was running]
 * ```
 *
 * Pausing tears the contract down, so a paused policy leaves no task in the QP.
 * Playing starts it again from scratch.
 */
struct MCNNContract
{
  virtual ~MCNNContract() = default;

  // ---------------------------------------------------------------- configuration

  /** Read contract-specific YAML fields. No controller is available yet. */
  virtual void configure(const mc_rtc::Configuration &) {}

  /**
   * Load heavy resources such as the ONNX session. Called once before the first
   * start (or when RunNN is configured with `preload: true`).
   *
   * @return false if loading failed; RunNN then refuses to start the policy.
   */
  virtual bool load() { return true; }

  /** Whether load() succeeded. */
  virtual bool loaded() const { return true; }

  /** Inference rate used when YAML `policy_hz` is 0. A value <= 0 means every controller tick. */
  virtual double defaultRateHz() const { return 30.0; }

  /** Exclusivity used when YAML `exclusive` is absent. An exclusive policy pauses every other one when it starts. */
  virtual bool defaultExclusive() const { return false; }

  /**
   * Whether afterSolve() must be called after every QP solve. This requires the
   * host controller to call the "MCNN::AfterSolve" datastore entry after
   * fsm::Controller::run() (see mc_nn/MCNNHost.h). RunNN refuses to start the
   * policy on a host that does not declare this support.
   */
  virtual bool requiresAfterSolve() const { return false; }

  // ---------------------------------------------------------------- runtime

  /** Validate robots/frames and create tasks, constraints or datastore entries. */
  virtual void start(mc_control::fsm::Controller &) {}

  /** Called every controller tick while running, before step(). */
  virtual void update(mc_control::fsm::Controller &, double /* dt */) {}

  /** Called at the policy rate while running: one inference step. */
  virtual void step(mc_control::fsm::Controller &) {}

  /** Called after each QP solve while running, if requiresAfterSolve() is true. */
  virtual void afterSolve(mc_control::fsm::Controller &) {}

  /** Whether the policy currently considers itself complete. The default never completes. */
  virtual bool finished(mc_control::fsm::Controller &) { return false; }

  /**
   * Remove every task/constraint/resource created by start() and restore what
   * start() changed. Also called when start() throws, so it must only undo
   * what start() actually did.
   */
  virtual void teardown(mc_control::fsm::Controller &) {}

  // ---------------------------------------------------------------- presentation

  /**
   * Add contract-specific GUI elements under `category` (or its sub-categories).
   * RunNN removes the whole category when the policy is paused or removed, and
   * calls addGui() again whenever it rebuilds its GUI.
   */
  virtual void addGui(mc_control::fsm::Controller &, const std::vector<std::string> & /* category */) {}

  /**
   * Add contract-specific log entries, named `prefix + name`, using `this` as the
   * log source. RunNN calls ctl.logger().removeLogEntries(this) after teardown().
   */
  virtual void addLog(mc_control::fsm::Controller &, const std::string & /* prefix */) {}

  /** Sizes and latest vectors shown/logged by RunNN's common GUI. Empty when unknown. */
  virtual size_t observationSize() const { return 0; }
  virtual size_t actionSize() const { return 0; }
  virtual Eigen::VectorXd observation() const { return {}; }
  virtual Eigen::VectorXd action() const { return {}; }

  // ---------------------------------------------------------------- set by RunNN

  const MCNNPolicyInfo & info() const noexcept { return info_; }

  /** Effective inference rate in Hz (1 / controller timestep when running every tick). */
  double policyHz() const noexcept { return policy_hz_; }

  /**
   * RunNN's `gui` / `logs` settings. When false, RunNN never calls addGui() /
   * addLog(). A contract may check them to skip GUI- or log-only computations.
   * GUI elements or log entries a contract adds by itself elsewhere still work;
   * they are simply not managed (nor removed) by RunNN.
   */
  bool guiEnabled() const noexcept { return gui_enabled_; }
  bool logsEnabled() const noexcept { return logs_enabled_; }

private:
  friend struct RunNN;
  MCNNPolicyInfo info_;
  double policy_hz_ = 30.0;
  bool gui_enabled_ = true;
  bool logs_enabled_ = true;
};
