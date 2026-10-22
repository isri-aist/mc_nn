cmake_minimum_required(VERSION 3.10)

set(CXX_DISABLE_WERROR 1)
set(CMAKE_CXX_STANDARD 17)

set(PROJECT_NAME MCNN)
set(PROJECT_DESCRIPTION
    "MCNN: core for running neural-network policies (contracts) in mc_rtc"
)
set(PROJECT_URL "")

project(${PROJECT_NAME} CXX)

###########
# OPTIONS #
###########
set(ONNXRUNTIME_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/src/onnxruntime"
    CACHE PATH
    "Path to ONNX Runtime root directory"
)
option(USE_BUNDLED_ONNXRUNTIME "Install the ONNX Runtime bundled with mc_nn" ON)

# Check if the project is built inside mc_rtc
if(NOT TARGET mc_rtc::mc_control)
    find_package(mc_rtc REQUIRED)
endif()

set(AROBASE "@")
# Default models_dirs entry, and base of relative models_dirs entries
set(POLICIES_DIR "${MC_CONTROLLER_RUNTIME_INSTALL_PREFIX}/policies")
# Where contract libraries are installed and loaded from at runtime
set(MC_NN_CONTRACTS_DIR "${MC_RTC_LIBDIR}/mc_nn_contracts")
# Installation prefix of libMCNN's public headers. Do not infer this from
# MC_RTC_LIBDIR: on multiarch systems that directory is /lib/<triplet>.
set(MC_NN_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(MC_NN_LIBDIR "${MC_RTC_LIBDIR}")

include(cmake/MCNNContract.cmake)

add_subdirectory(src)
add_subdirectory(contracts)

# CMake package for contracts built outside mc_nn: find_package(mc_nn) provides
# mc_nn::MCNN and mc_nn_add_contract() (see contracts/README.md, "External contracts").
set(MC_NN_CMAKE_DIR "${MC_RTC_LIBDIR}/cmake/mc_nn")
install(EXPORT mc_nnTargets NAMESPACE mc_nn:: DESTINATION "${MC_NN_CMAKE_DIR}")
configure_file(
    cmake/mc_nnConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/mc_nnConfig.cmake"
    @ONLY
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/mc_nnConfig.cmake"
        cmake/MCNNContract.cmake
        cmake/onnxruntimeConfig.cmake
    DESTINATION "${MC_NN_CMAKE_DIR}"
)

configure_file(etc/MCNN.in.yaml "${CMAKE_CURRENT_BINARY_DIR}/etc/MCNN.yaml")
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/etc/MCNN.yaml"
    DESTINATION "${MC_CONTROLLER_RUNTIME_INSTALL_PREFIX}/etc"
)

# Install the bundled policies: models plus the YAML side files some contracts
# read next to them. Training backups and videos stay in the source tree.
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/policies/"
    DESTINATION "${POLICIES_DIR}"
    FILES_MATCHING
    PATTERN "*.onnx"
    PATTERN "*.yaml"
    PATTERN "files_backup" EXCLUDE
    PATTERN "videos" EXCLUDE
)
