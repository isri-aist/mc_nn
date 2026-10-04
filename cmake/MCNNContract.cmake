# mc_nn_add_contract(<Name>
#   SOURCES <files...>
#   [INCLUDE_DIRS <dirs...>]        # private include directories of the contract
#   [LINK <targets...>]             # extra libraries, e.g. another contract: mcnn_contract_<Other>
#   [REQUIRES_HEADERS <headers...>] # skip the contract when a header is not found in mc_rtc's include paths
# )
#
# Builds one optional contract as its own shared library, mcnn_contract_<Name>,
# installed in <lib dir of the current project>/mc_nn_contracts. RunNN loads
# mc_nn's own contract directory and every directory listed in its
# `contracts_dirs`. The contract registers itself with REGISTER_MC_NN_CONTRACT,
# so nothing else in mc_nn has to change when a contract is added.
#
# Works inside mc_nn (contracts/) and in any other project after
# find_package(mc_nn) (see contracts/README.md, "External contracts").
#
# Each contract can be disabled with -DMC_NN_CONTRACT_<Name>=OFF. A contract
# whose REQUIRES_HEADERS or LINK contracts are unavailable is skipped with a
# message instead of breaking the whole build.
function(mc_nn_add_contract NAME)
  cmake_parse_arguments(CONTRACT "" "" "SOURCES;INCLUDE_DIRS;LINK;REQUIRES_HEADERS" ${ARGN})

  option(MC_NN_CONTRACT_${NAME} "Build the ${NAME} mc_nn contract" ON)
  if(NOT MC_NN_CONTRACT_${NAME})
    message(STATUS "mc_nn contract ${NAME}: disabled (MC_NN_CONTRACT_${NAME}=OFF)")
    return()
  endif()

  get_target_property(MC_RTC_INCLUDE_DIRS mc_rtc::mc_control INTERFACE_INCLUDE_DIRECTORIES)
  foreach(HEADER ${CONTRACT_REQUIRES_HEADERS})
    string(MAKE_C_IDENTIFIER "MC_NN_${NAME}_${HEADER}" HEADER_VAR)
    find_file(${HEADER_VAR} ${HEADER} PATHS ${MC_RTC_INCLUDE_DIRS} ${CMAKE_PREFIX_PATH} PATH_SUFFIXES include)
    if(NOT ${HEADER_VAR})
      message(STATUS "mc_nn contract ${NAME}: skipped, ${HEADER} not found (see the ${NAME} README)")
      return()
    endif()
  endforeach()

  foreach(LIB ${CONTRACT_LINK})
    if(LIB MATCHES "^mcnn_contract_" AND NOT TARGET ${LIB})
      message(STATUS "mc_nn contract ${NAME}: skipped, it needs ${LIB} which is not built")
      return()
    endif()
  endforeach()

  set(TARGET_NAME mcnn_contract_${NAME})
  add_library(${TARGET_NAME} SHARED ${CONTRACT_SOURCES})
  # Inside mc_nn the core target is MCNN, elsewhere the imported mc_nn::MCNN.
  if(TARGET MCNN)
    set(MC_NN_CORE_TARGET MCNN)
  else()
    set(MC_NN_CORE_TARGET mc_nn::MCNN)
  endif()
  set(CONTRACTS_DESTINATION "${MC_RTC_LIBDIR}/mc_nn_contracts")
  target_link_libraries(${TARGET_NAME} PUBLIC ${MC_NN_CORE_TARGET} ${CONTRACT_LINK})
  target_include_directories(${TARGET_NAME} PRIVATE ${CONTRACT_INCLUDE_DIRS})
  # Find libMCNN/libonnxruntime and contract libraries this one links to.
  set_target_properties(${TARGET_NAME} PROPERTIES INSTALL_RPATH "${MC_NN_LIBDIR};${MC_RTC_LIBDIR};${CONTRACTS_DESTINATION}")
  install(TARGETS ${TARGET_NAME} LIBRARY DESTINATION "${CONTRACTS_DESTINATION}")
  message(STATUS "mc_nn contract ${NAME}: enabled")
endfunction()
