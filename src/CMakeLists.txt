set(controller_SRC
  MCNNController.cpp
  mc_nn/MCNN.cpp
  mc_nn/MCNNHost.cpp
  mc_nn/MCNNModel.cpp
  mc_nn/MCNNRegistry.cpp
)

set(controller_HDR
  MCNNController.h
  mc_nn/MCNN.h
  mc_nn/MCNNContract.h
  mc_nn/MCNNHost.h
  mc_nn/MCNNModel.h
  mc_nn/MCNNRegistry.h
)

# libMCNN.so is the mc_nn core: contract base class, ONNX model, registry and
# host-controller hooks. Contracts are separate libraries (contracts/*) linked
# against it and loaded at runtime by the registry. The RunNN state links
# against it, so any FSM controller loading RunNN from this package gets the core.
add_library(${PROJECT_NAME} SHARED ${controller_SRC} ${controller_HDR})
set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "-DMCNN_EXPORTS")
target_compile_definitions(${PROJECT_NAME} PRIVATE MC_NN_CONTRACTS_DIR="${MC_NN_CONTRACTS_DIR}")
target_link_libraries(${PROJECT_NAME} PUBLIC mc_rtc::mc_control_fsm ${CMAKE_DL_LIBS})
# Find the vendored libonnxruntime next to libMCNN without LD_LIBRARY_PATH
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY INSTALL_RPATH "${MC_RTC_LIBDIR}")
# Exported as mc_nn::MCNN for contracts built in other projects (find_package(mc_nn))
install(TARGETS ${PROJECT_NAME}
  EXPORT mc_nnTargets
  ARCHIVE DESTINATION "${MC_RTC_LIBDIR}"
  LIBRARY DESTINATION "${MC_RTC_LIBDIR}"
  RUNTIME DESTINATION bin)
install(FILES
  mc_nn/MCNN.h
  mc_nn/MCNNContract.h
  mc_nn/MCNNHost.h
  mc_nn/MCNNModel.h
  mc_nn/MCNNRegistry.h
  DESTINATION "${MC_NN_PREFIX}/include/mc_nn")

# ---- ONNX Runtime (local vendored copy) ----
set(ONNXRUNTIME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/onnxruntime)
set(ONNXRUNTIME_INCLUDE_DIR ${ONNXRUNTIME_ROOT}/include)
set(ONNXRUNTIME_LIB_DIR ${ONNXRUNTIME_ROOT}/lib)

find_library(ONNXRUNTIME_LIB
  NAMES onnxruntime
  PATHS ${ONNXRUNTIME_LIB_DIR}
  NO_DEFAULT_PATH
  REQUIRED
)

if(ONNXRUNTIME_LIB)
  # Installed: mc_nnConfig.cmake defines mc_nn::onnxruntime from the installed library.
  target_link_libraries(${PROJECT_NAME}
    PUBLIC
      $<BUILD_INTERFACE:${ONNXRUNTIME_LIB}>
      $<INSTALL_INTERFACE:mc_nn::onnxruntime>
  )

  # Installed layout: <prefix>/include/mc_nn/*.h and <prefix>/include/mc_nn/onnxruntime/*.h
  target_include_directories(${PROJECT_NAME}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
      $<BUILD_INTERFACE:${ONNXRUNTIME_INCLUDE_DIR}>
      $<INSTALL_INTERFACE:include>
      $<INSTALL_INTERFACE:include/mc_nn/onnxruntime>
  )
  install(DIRECTORY ${ONNXRUNTIME_INCLUDE_DIR}/ DESTINATION "${MC_NN_PREFIX}/include/mc_nn/onnxruntime")

  # Install the runtime shared libraries alongside the project libs
  install(
    DIRECTORY ${ONNXRUNTIME_LIB_DIR}/
    DESTINATION ${MC_RTC_LIBDIR}
    FILES_MATCHING
      PATTERN "libonnxruntime*.so*"
  )
else()
  message(WARNING "ONNX Runtime not found in ${ONNXRUNTIME_LIB_DIR}. mc_nn will fail to build if headers or libraries are missing.")
endif()

add_controller(${PROJECT_NAME}_controller lib.cpp "")
target_link_libraries(${PROJECT_NAME}_controller PUBLIC ${PROJECT_NAME})
set_property(TARGET ${PROJECT_NAME}_controller APPEND PROPERTY INSTALL_RPATH "${MC_RTC_LIBDIR}")

add_subdirectory(states)
