#include "RunNN.h"

#include "mc_nn/MCNNHost.h"
#include "mc_nn/MCNNRegistry.h"

#include <mc_control/fsm/Controller.h>
#include <mc_rtc/gui/Button.h>
#include <mc_rtc/gui/ComboInput.h>
#include <mc_rtc/gui/Label.h>
#include <mc_rtc/gui/NumberInput.h>
#include <mc_rtc/logging.h>

#include <fnmatch.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace
{

std::string trim(const std::string & value)
{
  const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
  const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
  if(begin >= end) { return {}; }
  return std::string(begin, end);
}

std::string joinNames(const std::vector<std::string> & names)
{
  std::ostringstream os;
  for(size_t i = 0; i < names.size(); ++i)
  {
    if(i > 0) { os << ", "; }
    os << "'" << names[i] << "'";
  }
  return os.str();
}

std::string formatHz(double hz)
{
  std::ostringstream os;
  os.setf(std::ios::fixed);
  os.precision(1);
  os << hz;
  return os.str();
}

/** A string or a list of strings. */
std::vector<std::string> stringList(const mc_rtc::Configuration & config, const std::string & key)
{
  std::vector<std::string> out;
  if(!config.has(key)) { return out; }
  const auto value = config(key);
  if(value.isArray()) { out = value.operator std::vector<std::string>(); }
  else { out.push_back(value.operator std::string()); }
  for(auto & item : out) { item = trim(item); }
  return out;
}

struct ModelFile
{
  std::string stem;     ///< File name without .onnx
  std::string relative; ///< Path relative to its models_dirs entry, without .onnx
  std::string path;     ///< Canonical absolute path
  std::string folder;   ///< Directory containing the file
};

/** Directories never searched: training backups and recorded videos. */
bool skippedDirectory(const fs::path & path)
{
  const auto name = path.filename().string();
  return name == "files_backup" || name == "videos";
}

std::vector<ModelFile> scanModels(const std::vector<std::string> & directories)
{
  std::vector<ModelFile> files;
  std::set<std::string> seen;
  for(const auto & directory : directories)
  {
    std::error_code error;
    if(!fs::is_directory(directory, error))
    {
      mc_rtc::log::warning("[RunNN] models_dirs entry '{}' is not a directory", directory);
      continue;
    }
    fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied), end;
    for(; it != end; it.increment(error))
    {
      if(error) { break; }
      if(it->is_directory() && skippedDirectory(it->path()))
      {
        it.disable_recursion_pending();
        continue;
      }
      if(!it->is_regular_file() || it->path().extension() != ".onnx") { continue; }
      const std::string path = fs::canonical(it->path()).string();
      if(!seen.insert(path).second) { continue; } // Overlapping models_dirs
      auto relative = fs::relative(it->path(), directory);
      relative.replace_extension();
      files.push_back({it->path().stem().string(), relative.generic_string(), path, it->path().parent_path().string()});
    }
  }
  return files;
}

/** Patterns containing '/' match the relative path, other patterns match the file name. */
bool matches(const std::string & pattern, const ModelFile & file)
{
  if(pattern.find('/') != std::string::npos) { return fnmatch(pattern.c_str(), file.relative.c_str(), FNM_PATHNAME) == 0; }
  return fnmatch(pattern.c_str(), file.stem.c_str(), 0) == 0;
}

} // namespace

void RunNN::configure(const mc_rtc::Configuration & config)
{
  // configure() is called once per level of state inheritance: keep merging and
  // build the policies in start().
  config_.load(config);
}

void RunNN::buildPolicies()
{
  verbose_ = config_("verbose", 1);
  preload_ = config_("preload", false);
  gui_enabled_ = config_("gui", true);
  logs_enabled_ = config_("logs", true);

  std::vector<std::string> modelsDirs = stringList(config_, "models_dirs");
  if(modelsDirs.empty()) { modelsDirs.push_back(MC_NN_POLICIES_DIR); }
  for(auto & directory : modelsDirs)
  {
    if(fs::path(directory).is_relative()) { directory = (fs::path(MC_NN_POLICIES_DIR) / directory).string(); }
  }
  MCNNRegistry::loadContractLibraries(stringList(config_, "contracts_dirs"));

  const auto files = scanModels(modelsDirs);
  std::vector<mc_rtc::Configuration> groups;
  if(config_.has("policies")) { groups = config_("policies").operator std::vector<mc_rtc::Configuration>(); }

  policies_.clear();
  std::map<std::string, size_t> groupOfId;
  for(size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
  {
    const auto & group = groups[groupIndex];
    const std::string contract = trim(group("contract", std::string{}));
    const std::string prefix = group("prefix", std::string{});
    const auto patterns = stringList(group, "onnx");
    if(contract.empty() || patterns.empty())
    {
      mc_rtc::log::error_and_throw<std::runtime_error>(
          "[RunNN] policies entry #{} requires a non-empty 'contract' and 'onnx' pattern list", groupIndex + 1);
    }

    // Collect the files selected by this entry, grouped by file name.
    std::map<std::string, std::vector<const ModelFile *>> selected;
    std::vector<std::string> order;
    for(const auto & pattern : patterns)
    {
      bool found = false;
      for(const auto & file : files)
      {
        if(!matches(pattern, file)) { continue; }
        found = true;
        auto & candidates = selected[file.stem];
        if(candidates.empty()) { order.push_back(file.stem); }
        if(std::find(candidates.begin(), candidates.end(), &file) == candidates.end()) { candidates.push_back(&file); }
      }
      if(!found)
      {
        mc_rtc::log::error_and_throw<std::runtime_error>("[RunNN] onnx pattern '{}' (entry #{}, contract '{}') matches no .onnx file in [{}]",
                                                         pattern, groupIndex + 1, contract, joinNames(modelsDirs));
      }
    }

    for(const auto & stem : order)
    {
      const auto & candidates = selected[stem];
      if(candidates.size() > 1)
      {
        std::vector<std::string> paths;
        for(const auto * file : candidates) { paths.push_back(file->path); }
        mc_rtc::log::error_and_throw<std::runtime_error>(
            "[RunNN] '{}.onnx' is ambiguous, found in: [{}]. Rename one of them or use a more specific path pattern",
            stem, joinNames(paths));
      }
      const std::string id = prefix + stem;
      const auto previous = groupOfId.find(id);
      if(previous != groupOfId.end())
      {
        mc_rtc::log::error_and_throw<std::runtime_error>(
            "[RunNN] Policy '{}' is selected by policies entries #{} and #{}. Use different prefixes to configure "
            "the same model twice",
            id, previous->second + 1, groupIndex + 1);
      }
      groupOfId[id] = groupIndex;

      std::unique_ptr<Policy> entry(new Policy());
      entry->info.id = id;
      entry->info.contract = contract;
      entry->info.modelPath = candidates.front()->path;
      entry->info.folder = candidates.front()->folder;
      entry->info.device = group("device", std::string("auto"));
      entry->config = group;
      entry->desiredHz = group("policy_hz", 0.0);
      entry->timeout = group("timeout", 0.0);
      entry->blocking = group("blocking", true);
      if(entry->timeout < 0.0)
      {
        mc_rtc::log::error_and_throw<std::runtime_error>("[RunNN] Policy '{}': timeout must be zero or greater", id);
      }
      entry->contract = MCNNRegistry::create(contract);
      bindContract(*entry);
      entry->exclusive = group("exclusive", entry->contract->defaultExclusive());
      entry->contract->configure(group);
      policies_.push_back(std::move(entry));
    }
  }

  if(preload_)
  {
    for(auto & entry : policies_) { ensureLoaded(*entry); }
  }
}

void RunNN::bindContract(Policy & entry)
{
  entry.contract->info_ = entry.info;
  entry.contract->gui_enabled_ = gui_enabled_;
  entry.contract->logs_enabled_ = logs_enabled_;
}

RunNN::Policy & RunNN::policy(const std::string & id)
{
  const auto found = std::find_if(policies_.begin(), policies_.end(), [&id](const std::unique_ptr<Policy> & entry) { return entry->info.id == id; });
  if(found == policies_.end()) { mc_rtc::log::error_and_throw<std::runtime_error>("[RunNN] Unknown policy '{}'", id); }
  return **found;
}

bool RunNN::ensureLoaded(Policy & entry)
{
  if(!entry.loadAttempted)
  {
    entry.loadAttempted = true;
    entry.contract->load();
  }
  return entry.contract->loaded();
}

double RunNN::effectiveHz(const Policy & entry) const
{
  if(entry.desiredHz > 0.0) { return entry.desiredHz; }
  if(entry.desiredHz == 0.0 && entry.contract->defaultRateHz() > 0.0) { return entry.contract->defaultRateHz(); }
  return 1.0 / dt_;
}

void RunNN::applyRate(Policy & entry) { entry.contract->policy_hz_ = effectiveHz(entry); }

void RunNN::start(mc_control::fsm::Controller & ctl)
{
  dt_ = ctl.timeStep;
  gui_category_ = {"MCNN", name()};
  buildPolicies();

  std::vector<std::string> active;
  for(const auto & pattern : stringList(config_, "active_policies"))
  {
    bool found = false;
    for(const auto & entry : policies_)
    {
      if(fnmatch(pattern.c_str(), entry->info.id.c_str(), 0) != 0) { continue; }
      found = true;
      if(std::find(active.begin(), active.end(), entry->info.id) == active.end()) { active.push_back(entry->info.id); }
    }
    if(!found)
    {
      std::vector<std::string> ids;
      for(const auto & entry : policies_) { ids.push_back(entry->info.id); }
      mc_rtc::log::error_and_throw<std::runtime_error>("[RunNN] active_policies entry '{}' matches no configured policy. Configured policies: [{}]",
                                                       pattern, joinNames(ids));
    }
  }
  if(active.size() > 1)
  {
    for(const auto & id : active)
    {
      if(policy(id).exclusive)
      {
        mc_rtc::log::error_and_throw<std::runtime_error>(
            "[RunNN] Policy '{}' is exclusive and cannot start together with the other active_policies [{}]", id,
            joinNames(active));
      }
    }
  }

  load_error_ = false;
  for(const auto & id : active)
  {
    auto & entry = policy(id);
    if(!startPolicy(ctl, entry))
    {
      load_error_ = true;
      continue;
    }
    running_.push_back(&entry);
  }

  std::vector<std::string> started;
  for(const auto * entry : running_) { started.push_back(entry->info.id); }
  mc_rtc::log::info("[RunNN] {} configured policies, running: [{}]", policies_.size(), joinNames(started));
  gui_dirty_ = true;
}

bool RunNN::startPolicy(mc_control::fsm::Controller & ctl, Policy & entry)
{
  if(!ensureLoaded(entry))
  {
    mc_rtc::log::error("[RunNN] Policy '{}' could not be loaded", entry.info.id);
    return false;
  }
  if(entry.contract->requiresAfterSolve() && !mc_nn::host::supportsAfterSolve(ctl))
  {
    mc_rtc::log::error("[RunNN] Policy '{}' (contract {}) needs a post-solve step, but the host controller does not "
                       "declare \"{}\". See mc_nn/MCNNHost.h",
                       entry.info.id, entry.info.contract, mc_nn::host::SupportsAfterSolveKey);
    return false;
  }

  applyRate(entry);
  try
  {
    entry.contract->start(ctl);
  }
  catch(...)
  {
    // Let the contract undo a partial start before reporting the original error.
    try
    {
      entry.contract->teardown(ctl);
    }
    catch(const std::exception & error)
    {
      mc_rtc::log::error("[RunNN] Cleaning up '{}' after a failed start also failed: {}", entry.info.id, error.what());
    }
    throw;
  }
  entry.status = Status::Running;
  entry.accumulator = 1.0 / effectiveHz(entry); // First inference on the first run()
  entry.elapsed = 0.0;
  entry.updates = 0;
  entry.measuredHz = 0.0;
  entry.hasLastStepWallTime = false;
  entry.finishedNow = false;
  entry.timedOut = false;

  if(entry.contract->requiresAfterSolve())
  {
    MCNNContract * contract = entry.contract.get();
    mc_nn::host::addAfterSolve(ctl, contract, [contract, &ctl]() { contract->afterSolve(ctl); });
  }
  if(logs_enabled_)
  {
    addCommonLog(ctl, entry);
    entry.contract->addLog(ctl, "MCNN_" + entry.info.id + "_");
  }
  gui_dirty_ = true;
  return true;
}

void RunNN::stopPolicy(mc_control::fsm::Controller & ctl, Policy & entry)
{
  if(entry.status != Status::Running) { return; }
  MCNNContract * contract = entry.contract.get();
  mc_nn::host::removeAfterSolve(ctl, contract);
  contract->teardown(ctl);
  if(logs_enabled_)
  {
    ctl.logger().removeLogEntries(contract);
    ctl.logger().removeLogEntries(&entry);
  }
  if(gui_enabled_) { ctl.gui()->removeCategory(contractCategory(entry)); }
  entry.status = Status::Paused;
  gui_dirty_ = true;
}

void RunNN::applyExclusivity(mc_control::fsm::Controller & ctl, const Policy & starting)
{
  // An exclusive policy pauses every other one; any policy pauses running
  // exclusive ones. What ran before is not remembered.
  for(auto * other : running_)
  {
    if(other == &starting || other->status != Status::Running) { continue; }
    if(starting.exclusive || other->exclusive)
    {
      mc_rtc::log::info("[RunNN] Pausing '{}': {} policy '{}' is starting", other->info.id,
                        starting.exclusive ? "exclusive" : "another", starting.info.id);
      stopPolicy(ctl, *other);
    }
  }
}

void RunNN::launch(mc_control::fsm::Controller & ctl, Policy & entry)
{
  if(entry.status != Status::Ready) { return; }
  if(!ensureLoaded(entry)) { return; }
  applyExclusivity(ctl, entry);
  if(startPolicy(ctl, entry))
  {
    running_.push_back(&entry);
    mc_rtc::log::info("[RunNN] Launched '{}'", entry.info.id);
  }
}

void RunNN::pauseOrPlay(mc_control::fsm::Controller & ctl, Policy & entry)
{
  if(entry.status == Status::Running)
  {
    stopPolicy(ctl, entry);
    mc_rtc::log::info("[RunNN] Paused '{}'", entry.info.id);
  }
  else if(entry.status == Status::Paused)
  {
    applyExclusivity(ctl, entry);
    if(startPolicy(ctl, entry)) { mc_rtc::log::info("[RunNN] Playing '{}'", entry.info.id); }
  }
}

void RunNN::remove(mc_control::fsm::Controller & ctl, Policy & entry)
{
  stopPolicy(ctl, entry);
  running_.erase(std::remove(running_.begin(), running_.end(), &entry), running_.end());
  entry.status = Status::Ready;
  gui_dirty_ = true;
  mc_rtc::log::info("[RunNN] Removed '{}' from the running policies", entry.info.id);
}

void RunNN::reload(mc_control::fsm::Controller & ctl, Policy & entry)
{
  const bool wasRunning = entry.status == Status::Running;
  stopPolicy(ctl, entry);

  // A new instance re-reads the model and every file the contract loads.
  entry.contract = MCNNRegistry::create(entry.info.contract);
  bindContract(entry);
  entry.contract->configure(entry.config);
  entry.loadAttempted = false;
  if(!ensureLoaded(entry))
  {
    mc_rtc::log::error("[RunNN] Reloading '{}' failed", entry.info.id);
    return;
  }
  mc_rtc::log::success("[RunNN] Reloaded '{}'", entry.info.id);
  if(wasRunning) { startPolicy(ctl, entry); }
  gui_dirty_ = true;
}

void RunNN::processActions(mc_control::fsm::Controller & ctl)
{
  const auto actions = std::move(actions_);
  actions_.clear();
  for(const auto & action : actions)
  {
    try
    {
      auto & entry = policy(action.second);
      switch(action.first)
      {
        case Action::Launch: launch(ctl, entry); break;
        case Action::PauseOrPlay: pauseOrPlay(ctl, entry); break;
        case Action::Remove: remove(ctl, entry); break;
        case Action::Reload: reload(ctl, entry); break;
      }
    }
    catch(const std::exception & error)
    {
      // A GUI request must not bring the controller down: report and leave the policy stopped.
      mc_rtc::log::error("[RunNN] Request on '{}' failed: {}", action.second, error.what());
      for(auto & entry : policies_)
      {
        if(entry->info.id == action.second && entry->status == Status::Running) { stopPolicy(ctl, *entry); }
      }
    }
  }
}

void RunNN::stepPolicy(mc_control::fsm::Controller & ctl, Policy & entry)
{
  const auto & name = entry.info.id;
  entry.contract->update(ctl, ctl.timeStep);

  entry.accumulator += ctl.timeStep;
  const double policyHz = effectiveHz(entry);
  const double period = 1.0 / policyHz;
  if(entry.accumulator + 1e-12 >= period)
  {
    // More than one tick late means the previous step could not be scheduled in time.
    if(verbose_ == 1 && entry.accumulator > period + ctl.timeStep + 1e-9)
    {
      mc_rtc::log::warning(
          "[RunNN] Policy '{}' could not run at its expected rate in mc_rtc time ({:.1f} Hz): last step took {:.1f} ms instead of {:.1f} ms ({:.1f} Hz)",
          name,
          policyHz,
          entry.accumulator * 1000.0,
          period * 1000.0,
          1.0 / entry.accumulator);
    }

    const auto now = std::chrono::steady_clock::now();
    if(entry.hasLastStepWallTime)
    {
      // Simulated time can stay on schedule even if the real control loop is
      // running slower/faster than real time, so wall-clock time is checked too.
      const double realDt = std::chrono::duration<double>(now - entry.lastStepWallTime).count();
      if(realDt > 0.0) { entry.measuredHz = entry.measuredHz > 0.0 ? 0.8 * entry.measuredHz + 0.2 / realDt : 1.0 / realDt; }
      if(verbose_ == 1 && realDt > 1.5 * period)
      {
        mc_rtc::log::warning(
            "[RunNN] Policy '{}' could not run at its expected rate in real time ({:.1f} Hz): last step took {:.1f} ms instead of {:.1f} ms ({:.1f} Hz)",
            name,
            policyHz,
            realDt * 1000.0,
            period * 1000.0,
            1.0 / realDt);
      }
    }
    entry.lastStepWallTime = now;
    entry.hasLastStepWallTime = true;

    // Consume exactly one period. fmod alone keeps an accumulator that passed
    // the check only thanks to the tolerance, which stepped twice in a row.
    entry.accumulator = std::fmod(std::max(0.0, entry.accumulator - period), period);
    entry.contract->step(ctl);
    ++entry.updates;
  }

  entry.elapsed += ctl.timeStep;
  // timeout == 0 is the mc_rtc-style convention for no time limit.
  entry.timedOut = entry.timeout > 0.0 && entry.elapsed >= entry.timeout;
  entry.finishedNow = entry.contract->finished(ctl);
}

bool RunNN::evaluateCompletion(std::string & output)
{
  if(running_.empty())
  {
    completion_ = "Not complete: no policy running";
    return false;
  }

  size_t pausedBlocking = 0;
  std::vector<std::string> waiting;
  std::vector<std::string> completed;
  for(const auto * entry : running_)
  {
    if(entry->status == Status::Paused)
    {
      if(entry->blocking) { ++pausedBlocking; }
      continue;
    }
    if(entry->finishedNow) { completed.push_back(entry->info.id); }
    if(entry->blocking && !entry->finishedNow && !entry->timedOut) { waiting.push_back(entry->info.id); }
  }

  if(pausedBlocking > 0 || !waiting.empty())
  {
    std::ostringstream os;
    os << "Not complete:";
    if(pausedBlocking > 0) { os << " " << pausedBlocking << " paused blocking polic" << (pausedBlocking == 1 ? "y" : "ies") << ";"; }
    if(!waiting.empty()) { os << " waiting for " << joinNames(waiting); }
    completion_ = os.str();
    return false;
  }

  // mc_rtc exposes one output string, so join only policies whose custom
  // completion hook currently returns true. Timed-out/unfinished policies
  // are intentionally absent, including non-blocking policies.
  output.clear();
  for(const auto & name : completed)
  {
    if(!output.empty()) { output += ", "; }
    output += name + "(OK)";
  }
  if(output.empty()) { output = "OK"; }
  completion_ = "Complete: " + output;
  return true;
}

std::string RunNN::completionText(const Policy & entry) const
{
  if(entry.status == Status::Paused) { return entry.blocking ? "paused (blocks completion)" : "paused"; }
  if(entry.status == Status::Ready) { return "not running"; }
  if(entry.finishedNow) { return "finished (OK)"; }
  if(entry.timedOut) { return "timed out"; }
  return entry.blocking ? "running, not finished" : "running (non-blocking)";
}

bool RunNN::run(mc_control::fsm::Controller & ctl)
{
  processActions(ctl);

  if(load_error_)
  {
    output("SKIP");
    return true;
  }

  for(auto * entry : running_)
  {
    if(entry->status == Status::Running) { stepPolicy(ctl, *entry); }
  }

  std::string combinedOutput;
  const bool complete = evaluateCompletion(combinedOutput);
  if(gui_dirty_ && gui_enabled_) { rebuildGui(ctl); }
  if(complete)
  {
    output(combinedOutput);
    return true;
  }
  return false;
}

void RunNN::teardown(mc_control::fsm::Controller & ctl)
{
  // Reverse order mirrors resource acquisition and is friendly to policies with
  // dependencies, while remaining irrelevant for independent solver tasks.
  for(auto it = running_.rbegin(); it != running_.rend(); ++it) { stopPolicy(ctl, **it); }
  running_.clear();
  if(gui_enabled_) { ctl.gui()->removeCategory(gui_category_); }
}

std::vector<std::string> RunNN::policyCategory(const Policy & entry) const
{
  auto category = gui_category_;
  category.push_back(entry.info.id);
  return category;
}

std::vector<std::string> RunNN::contractCategory(const Policy & entry) const
{
  auto category = policyCategory(entry);
  category.push_back(entry.info.contract);
  return category;
}

void RunNN::addCommonLog(mc_control::fsm::Controller & ctl, Policy & entry)
{
  const std::string prefix = "MCNN_" + entry.info.id + "_";
  const MCNNContract * contract = entry.contract.get();
  const Policy * source = &entry;
  ctl.logger().addLogEntry(prefix + "observation", source, [contract]() { return contract->observation(); });
  ctl.logger().addLogEntry(prefix + "action", source, [contract]() { return contract->action(); });
  ctl.logger().addLogEntry(prefix + "policy_hz", source, [contract]() { return contract->policyHz(); });
  ctl.logger().addLogEntry(prefix + "measured_hz", source, [source]() { return source->measuredHz; });
  ctl.logger().addLogEntry(prefix + "updates", source, [source]() { return static_cast<double>(source->updates); });
}

void RunNN::rebuildGui(mc_control::fsm::Controller & ctl)
{
  gui_dirty_ = false;
  auto & gui = *ctl.gui();
  gui.removeCategory(gui_category_);

  // ---- Global information and status list
  gui.addElement(
      gui_category_,
      mc_rtc::gui::Label("Configured policies", [this]() { return std::to_string(policies_.size()); }),
      mc_rtc::gui::Label("Loaded models",
                         [this]()
                         {
                           return std::to_string(std::count_if(policies_.begin(), policies_.end(),
                                                               [](const std::unique_ptr<Policy> & entry)
                                                               { return entry->loadAttempted && entry->contract->loaded(); }));
                         }),
      mc_rtc::gui::Label("Running policies",
                         [this]()
                         {
                           return std::to_string(std::count_if(running_.begin(), running_.end(),
                                                               [](const Policy * entry) { return entry->status == Status::Running; }));
                         }),
      mc_rtc::gui::Label("Paused policies",
                         [this]()
                         {
                           return std::to_string(std::count_if(running_.begin(), running_.end(),
                                                               [](const Policy * entry) { return entry->status == Status::Paused; }));
                         }),
      mc_rtc::gui::Label("Completion", [this]() { return completion_; }));

  // ---- Launch a ready policy. Always present, so that the layout above the
  // status lines never changes: GUI clients keep existing widgets in place and
  // append new ones, so only the end of this category may grow or shrink.
  std::vector<std::string> ready;
  for(const auto & entry : policies_)
  {
    if(entry->status == Status::Ready) { ready.push_back(entry->info.id); }
  }
  if(std::find(ready.begin(), ready.end(), selected_) == ready.end()) { selected_ = ready.empty() ? "" : ready.front(); }
  gui.addElement(
      gui_category_,
      mc_rtc::gui::ComboInput("Ready policies", ready, [this]() { return selected_; },
                              [this](const std::string & id) { selected_ = id; }),
      mc_rtc::gui::Label("Exclusive",
                         [this]()
                         {
                           if(selected_.empty()) { return std::string("No ready policy"); }
                           return policy(selected_).exclusive
                                      ? std::string("This policy is set as exclusive and will pause all others if launched")
                                      : std::string("No");
                         }),
      mc_rtc::gui::Button("Launch",
                          [this]()
                          {
                            if(!selected_.empty()) { actions_.emplace_back(Action::Launch, selected_); }
                          }));

  // ---- Status of every running/paused policy, in launch order (end of the category)
  for(const auto * entry : running_)
  {
    gui.addElement(gui_category_,
                   mc_rtc::gui::Label("- " + entry->info.id,
                                      [this, entry]()
                                      {
                                        return std::string(entry->status == Status::Running ? "Running" : "Paused") + " | "
                                               + formatHz(entry->measuredHz) + "/" + formatHz(effectiveHz(*entry)) + " Hz | "
                                               + completionText(*entry);
                                      }));
  }

  // ---- One section per running/paused policy
  for(auto * entry : running_)
  {
    const auto category = policyCategory(*entry);
    const std::string id = entry->info.id;
    gui.addElement(
        category,
        mc_rtc::gui::Label("Policy", [entry]() { return entry->info.id; }),
        mc_rtc::gui::Label("Status", [entry]() { return std::string(entry->status == Status::Running ? "Running" : "Paused"); }),
        mc_rtc::gui::Label("Run speed [Hz]",
                           [this, entry]() { return formatHz(entry->measuredHz) + " (desired: " + formatHz(effectiveHz(*entry)) + ")"; }),
        mc_rtc::gui::Label("Completion", [this, entry]() { return completionText(*entry); }),
        mc_rtc::gui::Label("Elapsed [s]", [entry]() { return entry->elapsed; }),
        mc_rtc::gui::Label("Timeout [s]", [entry]() { return entry->timeout > 0.0 ? std::to_string(entry->timeout) : std::string("none"); }),
        mc_rtc::gui::Label("Blocking", [entry]() { return std::string(entry->blocking ? "yes" : "no"); }),
        mc_rtc::gui::Label("Exclusive", [entry]() { return std::string(entry->exclusive ? "yes" : "no"); }),
        mc_rtc::gui::Label("Inference updates", [entry]() { return std::to_string(entry->updates); }),
        mc_rtc::gui::Label("Observation / action size",
                           [entry]()
                           {
                             return std::to_string(entry->contract->observationSize()) + " / "
                                    + std::to_string(entry->contract->actionSize());
                           }),
        mc_rtc::gui::Label("Model", [entry]() { return entry->info.modelPath; }),
        mc_rtc::gui::Button(entry->status == Status::Running ? "Pause" : "Play",
                            [this, id]() { actions_.emplace_back(Action::PauseOrPlay, id); }),
        mc_rtc::gui::Button("Remove", [this, id]() { actions_.emplace_back(Action::Remove, id); }),
        mc_rtc::gui::Button("Reload", [this, id]() { actions_.emplace_back(Action::Reload, id); }),
        mc_rtc::gui::NumberInput("Rate [Hz]", [entry]() { return entry->desiredHz; },
                                 [this, entry](double hz)
                                 {
                                   entry->desiredHz = hz;
                                   applyRate(*entry);
                                 }),
        mc_rtc::gui::Label("Rate values", []() { return std::string("0 = contract default, -1 = every tick"); }),
        mc_rtc::gui::Label("Contract", [entry]() { return entry->info.contract; }));
    if(entry->status == Status::Running) { entry->contract->addGui(ctl, contractCategory(*entry)); }
  }
}

EXPORT_SINGLE_STATE("RunNN", RunNN)
