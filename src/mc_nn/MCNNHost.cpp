#include "MCNNHost.h"

#include <mc_rtc/logging.h>

#include <map>
#include <stdexcept>
#include <vector>

namespace mc_nn
{
namespace host
{

namespace
{

// Owners are kept in insertion order so post-solve steps run in launch order.
std::vector<std::pair<const void *, std::function<void()>>> & callbacks()
{
  static std::vector<std::pair<const void *, std::function<void()>>> registry;
  return registry;
}

} // namespace

bool supportsAfterSolve(const mc_control::fsm::Controller & ctl)
{
  return ctl.datastore().has(SupportsAfterSolveKey) && ctl.datastore().get<bool>(SupportsAfterSolveKey);
}

std::string feedbackType(const mc_control::fsm::Controller & ctl)
{
  return ctl.datastore().has(FeedbackTypeKey) ? ctl.datastore().get<std::string>(FeedbackTypeKey) : std::string{};
}

void addAfterSolve(mc_control::fsm::Controller & ctl, const void * owner, std::function<void()> callback)
{
  removeAfterSolve(ctl, owner);
  callbacks().emplace_back(owner, std::move(callback));
  if(!ctl.datastore().has(AfterSolveKey))
  {
    ctl.datastore().make_call(AfterSolveKey,
                              []()
                              {
                                // Copy: a callback may remove itself or others.
                                const auto current = callbacks();
                                for(const auto & entry : current) { entry.second(); }
                              });
  }
}

void removeAfterSolve(mc_control::fsm::Controller & ctl, const void * owner)
{
  auto & registry = callbacks();
  for(auto it = registry.begin(); it != registry.end();)
  {
    it = it->first == owner ? registry.erase(it) : it + 1;
  }
  if(registry.empty() && ctl.datastore().has(AfterSolveKey)) { ctl.datastore().remove(AfterSolveKey); }
}

mc_solver::FeedbackType parseFeedbackType(const std::string & name)
{
  static const std::map<std::string, mc_solver::FeedbackType> types = {
      {"None", mc_solver::FeedbackType::None},
      {"OpenLoop", mc_solver::FeedbackType::OpenLoop},
      {"Joints", mc_solver::FeedbackType::Joints},
      {"JointsWVelocity", mc_solver::FeedbackType::JointsWVelocity},
      {"ObservedRobots", mc_solver::FeedbackType::ObservedRobots},
      {"ClosedLoop", mc_solver::FeedbackType::ClosedLoop},
      {"ClosedLoopIntegrateReal", mc_solver::FeedbackType::ClosedLoopIntegrateReal}};
  const auto type = types.find(name);
  if(type == types.end())
  {
    mc_rtc::log::error_and_throw<std::runtime_error>(
        "[MCNN] Unknown FeedbackType '{}'. Expected None, OpenLoop, Joints, JointsWVelocity, ObservedRobots, "
        "ClosedLoop or ClosedLoopIntegrateReal",
        name);
  }
  return type->second;
}

} // namespace host
} // namespace mc_nn
