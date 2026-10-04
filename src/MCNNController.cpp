#include "MCNNController.h"

#include "mc_nn/MCNNHost.h"

MCNNController::MCNNController(mc_rbdyn::RobotModulePtr rm, double dt, const mc_rtc::Configuration & config)
: mc_control::fsm::Controller(
    rm,
    dt,
    config,
    mc_control::ControllerParameters{}
        .backend(mc_control::MCController::Backend::TVM)
        .overwrite_config(true)
        .load_robot_config_into(std::vector<std::string>{}))
{
  // Host side of mc_nn/MCNNHost.h: publish the solver feedback and declare the post-solve call.
  const std::string feedbackType = config("FeedbackType", std::string("None"));
  feedback_type_ = mc_nn::host::parseFeedbackType(feedbackType);
  datastore().make<std::string>(mc_nn::host::FeedbackTypeKey, feedbackType);
  datastore().make<bool>(mc_nn::host::SupportsAfterSolveKey, true);
  mc_rtc::log::success("MCNN init done ");
}

bool MCNNController::run()
{
  const bool ok = mc_control::fsm::Controller::run(feedback_type_);
  // Post-solve steps of running mc_nn policies (e.g. torques written after the QP)
  if(datastore().has(mc_nn::host::AfterSolveKey)) { datastore().call(mc_nn::host::AfterSolveKey); }
  return ok;
}

void MCNNController::reset(const mc_control::ControllerResetData & reset_data)
{
  mc_control::fsm::Controller::reset(reset_data);
}
