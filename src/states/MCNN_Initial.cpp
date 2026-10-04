#include "MCNN_Initial.h"

void MCNN_Initial::configure(const mc_rtc::Configuration &) {}

void MCNN_Initial::start(mc_control::fsm::Controller &) {}

bool MCNN_Initial::run(mc_control::fsm::Controller &)
{
  output("OK");
  return true;
}

void MCNN_Initial::teardown(mc_control::fsm::Controller &) {}

EXPORT_SINGLE_STATE("MCNN_Initial", MCNN_Initial)
