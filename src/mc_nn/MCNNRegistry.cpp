#include "MCNNRegistry.h"

#include <mc_rtc/logging.h>

#include <dlfcn.h>

#include <algorithm>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;

std::map<std::string, MCNNRegistry::Factory> & MCNNRegistry::factories()
{
  static std::map<std::string, Factory> registry;
  return registry;
}

bool MCNNRegistry::add(const std::string & contract, Factory factory)
{
  const bool added = factories().emplace(contract, std::move(factory)).second;
  if(!added) { mc_rtc::log::warning("[MCNNRegistry] Contract '{}' is registered more than once", contract); }
  return added;
}

std::unique_ptr<MCNNContract> MCNNRegistry::create(const std::string & contract)
{
  const auto factory = factories().find(contract);
  if(factory == factories().end())
  {
    std::string known;
    for(const auto & name : contracts()) { known += (known.empty() ? "" : ", ") + name; }
    mc_rtc::log::error_and_throw<std::runtime_error>(
        "[MCNNRegistry] Unknown contract '{}'. Registered contracts: [{}]. Check that its library was built "
        "(optional contracts are skipped when their dependencies are missing) and that its directory is listed.",
        contract, known);
  }
  return factory->second();
}

void MCNNRegistry::loadContractLibraries(const std::vector<std::string> & directories)
{
  // Handles are intentionally never closed: contract code must outlive every instance.
  static std::set<std::string> loaded;

  std::vector<std::string> all = directories;
  all.insert(all.begin(), MC_NN_CONTRACTS_DIR);
  for(const auto & directory : all)
  {
    std::error_code error;
    if(!fs::is_directory(directory, error))
    {
      if(directory != MC_NN_CONTRACTS_DIR)
      {
        mc_rtc::log::warning("[MCNNRegistry] Contract directory '{}' does not exist", directory);
      }
      continue;
    }
    std::vector<fs::path> libraries;
    for(const auto & entry : fs::directory_iterator(directory))
    {
      if(entry.is_regular_file() && entry.path().extension() == ".so") { libraries.push_back(entry.path()); }
    }
    std::sort(libraries.begin(), libraries.end());
    for(const auto & library : libraries)
    {
      const std::string path = fs::canonical(library).string();
      if(!loaded.insert(path).second) { continue; }
      if(dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL) == nullptr)
      {
        mc_rtc::log::error("[MCNNRegistry] Failed to load contract library '{}': {}", path, dlerror());
      }
    }
  }
}

std::vector<std::string> MCNNRegistry::contracts()
{
  std::vector<std::string> names;
  for(const auto & factory : factories()) { names.push_back(factory.first); }
  return names;
}
