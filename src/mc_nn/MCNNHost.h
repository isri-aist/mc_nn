#pragma once

#include <mc_control/fsm/Controller.h>
#include <mc_solver/QPSolver.h>

#include <functional>
#include <string>

/**
 * Contract between mc_nn and the FSM controller hosting RunNN.
 *
 * Some contracts need the host controller's cooperation, which a state cannot
 * provide by itself. Everything goes through the datastore, so a host
 * controller does not need to link against mc_nn. A host opts in with:
 *
 * ```cpp
 * // constructor
 * datastore().make<bool>("MCNN::HostSupportsAfterSolve", true);
 * datastore().make<std::string>("MCNN::FeedbackType", "<FeedbackType used in run()>");
 *
 * // run()
 * bool ok = mc_control::fsm::Controller::run(feedbackType);
 * if(datastore().has("MCNN::AfterSolve")) { datastore().call("MCNN::AfterSolve"); }
 * return ok;
 * ```
 *
 * - "MCNN::AfterSolve": created by mc_nn while at least one running policy
 *   needs a post-solve step (e.g. writing torques after the QP).
 * - "MCNN::HostSupportsAfterSolve": declares that the host calls it. RunNN
 *   refuses to start such policies on hosts that do not.
 * - "MCNN::FeedbackType": solver feedback used by the host, so contracts can
 *   check that it matches what they were designed for.
 */
namespace mc_nn
{
namespace host
{

constexpr const char * AfterSolveKey = "MCNN::AfterSolve";
constexpr const char * SupportsAfterSolveKey = "MCNN::HostSupportsAfterSolve";
constexpr const char * FeedbackTypeKey = "MCNN::FeedbackType";

/** Whether the host controller declared that it calls "MCNN::AfterSolve". */
bool supportsAfterSolve(const mc_control::fsm::Controller & ctl);

/** Feedback type published by the host, or an empty string if it did not publish one. */
std::string feedbackType(const mc_control::fsm::Controller & ctl);

/** Add a post-solve callback owned by `owner`; creates the datastore entry when needed. */
void addAfterSolve(mc_control::fsm::Controller & ctl, const void * owner, std::function<void()> callback);

/** Remove the callback of `owner`; removes the datastore entry when no callback is left. */
void removeAfterSolve(mc_control::fsm::Controller & ctl, const void * owner);

/**
 * Parse a FeedbackType name: None/OpenLoop, Joints, JointsWVelocity,
 * ObservedRobots/ClosedLoop or ClosedLoopIntegrateReal. Throws on unknown names.
 */
mc_solver::FeedbackType parseFeedbackType(const std::string & name);

} // namespace host
} // namespace mc_nn
