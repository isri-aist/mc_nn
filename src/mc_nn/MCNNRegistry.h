#pragma once

#include "MCNNContract.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

/**
 * Runtime factory for compiled MCNN contracts.
 *
 * A YAML `contract` names a registered C++ class. Each contract is built as its
 * own shared library (mc_nn_add_contract in CMake) and registers itself when the
 * library is loaded, so adding a contract never requires editing RunNN.
 */
struct MCNNRegistry
{
  using Factory = std::function<std::unique_ptr<MCNNContract>()>;

  static bool add(const std::string & contract, Factory factory);

  /** Create a contract instance, throwing if no loaded library registered `contract`. */
  static std::unique_ptr<MCNNContract> create(const std::string & contract);

  /**
   * Load every contract library (`*.so`) found in `directories`, plus the
   * directory where mc_nn installs its own contracts. A library is loaded once
   * per process; failures are reported and skipped.
   */
  static void loadContractLibraries(const std::vector<std::string> & directories);

  /** Names of every registered contract. */
  static std::vector<std::string> contracts();

private:
  static std::map<std::string, Factory> & factories();
};

/** Register one concrete contract class under a YAML-visible name. */
#define REGISTER_MC_NN_CONTRACT(CONTRACT_NAME, CLASS_NAME)                                                  \
  namespace                                                                                                \
  {                                                                                                        \
  const bool CLASS_NAME##_registered =                                                                     \
      MCNNRegistry::add(CONTRACT_NAME, []() { return std::unique_ptr<MCNNContract>(new CLASS_NAME()); });  \
  }
