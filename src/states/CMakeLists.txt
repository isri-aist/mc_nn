add_fsm_state_simple(MCNN_Initial)

add_fsm_state_simple(RunNN)
# Link explicitly so loading RunNN from another FSM controller
# also loads libMCNN.so, which loads the contract libraries.
target_link_libraries(RunNN PUBLIC ${PROJECT_NAME})
# Default models_dirs entry, and base of relative models_dirs entries
target_compile_definitions(RunNN PRIVATE MC_NN_POLICIES_DIR="${POLICIES_DIR}")
# Find libMCNN.so even when mc_nn is not in LD_LIBRARY_PATH (e.g. shell opened before the first build)
set_property(TARGET RunNN APPEND PROPERTY INSTALL_RPATH "${MC_RTC_LIBDIR}")

# RunNNBase needs the installed policies path, so it is configured, not copied.
configure_file(data/RunNNBase.in.yaml "${CMAKE_CURRENT_BINARY_DIR}/data/RunNNBase.yaml")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/data/RunNNBase.yaml" DESTINATION "${MC_STATES_RUNTIME_INSTALL_PREFIX}/data")
