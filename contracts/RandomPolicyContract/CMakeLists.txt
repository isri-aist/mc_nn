# One call builds the contract as libmcnn_contract_RandomPolicyContract.so, installed
# where RunNN loads contract libraries. See ../../cmake/MCNNContract.cmake for
# the options (INCLUDE_DIRS, LINK, REQUIRES_HEADERS).
mc_nn_add_contract(RandomPolicyContract
  SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/RandomPolicyContract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RandomPolicyContract.h
)
