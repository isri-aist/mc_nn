#pragma once

#include <mc_control/mc_controller.h>
#include <mc_control/fsm/Controller.h>

#include "api.h"

// Named MCNNController (controller name "MCNN") to avoid clashing with the MCNN policy base class.
struct MCNN_DLLAPI MCNNController : public mc_control::fsm::Controller
{
  MCNNController(mc_rbdyn::RobotModulePtr rm, double dt, const mc_rtc::Configuration & config);

  bool run() override;

  void reset(const mc_control::ControllerResetData & reset_data) override;

private:
  /** Solver feedback used by run(), from the `FeedbackType` configuration key. */
  mc_solver::FeedbackType feedback_type_ = mc_solver::FeedbackType::None;
};
