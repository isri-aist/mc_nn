# Defines the ONNX Runtime target used by mc_nn. During the mc_nn build it
# discovers ONNX Runtime and installs the bundled runtime when requested. When
# loaded by mc_nnConfig.cmake, the configured installed paths are used instead.
if(NOT TARGET mc_nn::onnxruntime)
    if(
        NOT DEFINED MC_NN_ONNXRUNTIME_LOCATION
        OR NOT DEFINED MC_NN_ONNXRUNTIME_INCLUDE_DIR
    )
        unset(ONNXRUNTIME_INCLUDE_DIR CACHE)
        unset(ONNXRUNTIME_INCLUDE_DIR)
        if(USE_BUNDLED_ONNXRUNTIME)
            set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/include")
            if(NOT EXISTS "${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_cxx_api.h")
                message(
                    FATAL_ERROR
                    "Bundled ONNX Runtime header not found: "
                    "${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_cxx_api.h"
                )
            endif()
        else()
            # Nix splits headers into onnxruntime.dev, so they are found
            # through its dependency paths rather than ONNXRUNTIME_ROOT.
            find_path(
                ONNXRUNTIME_INCLUDE_DIR
                NAMES onnxruntime_cxx_api.h
                HINTS "${ONNXRUNTIME_ROOT}/include"
                REQUIRED
            )
        endif()

        unset(ONNXRUNTIME_LIB CACHE)
        unset(ONNXRUNTIME_LIB)
        find_library(
            ONNXRUNTIME_LIB
            NAMES onnxruntime
            PATHS "${ONNXRUNTIME_ROOT}/lib"
            NO_DEFAULT_PATH
            REQUIRED
        )

        set(MC_NN_ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_INCLUDE_DIR}")
        set(MC_NN_ONNXRUNTIME_LOCATION "${ONNXRUNTIME_LIB}")
        set(MC_NN_ONNXRUNTIME_TARGET_INCLUDE_DIR "${ONNXRUNTIME_INCLUDE_DIR}")
        set(MC_NN_ONNXRUNTIME_TARGET_LOCATION "${ONNXRUNTIME_LIB}")

        if(USE_BUNDLED_ONNXRUNTIME)
            set(MC_NN_ONNXRUNTIME_LOCATION "\${MC_NN_LIBDIR}/libonnxruntime.so")
            set(MC_NN_ONNXRUNTIME_INCLUDE_DIR
                "\${MC_NN_PREFIX}/include/mc_nn/onnxruntime"
            )

            install(
                DIRECTORY "${ONNXRUNTIME_INCLUDE_DIR}/"
                DESTINATION "${MC_NN_PREFIX}/include/mc_nn/onnxruntime"
            )
            install(
                DIRECTORY "${ONNXRUNTIME_ROOT}/lib/"
                DESTINATION "${MC_RTC_LIBDIR}"
                FILES_MATCHING
                PATTERN "libonnxruntime*.so*"
            )
            set(MC_NN_ONNXRUNTIME_BUNDLED ON)
        else()
            message(
                STATUS
                "Using external ONNX Runtime from ${ONNXRUNTIME_ROOT}"
            )
            set(MC_NN_ONNXRUNTIME_BUNDLED OFF)
        endif()
    endif()

    if(NOT DEFINED MC_NN_ONNXRUNTIME_TARGET_LOCATION)
        set(MC_NN_ONNXRUNTIME_TARGET_LOCATION "${MC_NN_ONNXRUNTIME_LOCATION}")
    endif()
    if(NOT DEFINED MC_NN_ONNXRUNTIME_TARGET_INCLUDE_DIR)
        set(MC_NN_ONNXRUNTIME_TARGET_INCLUDE_DIR
            "${MC_NN_ONNXRUNTIME_INCLUDE_DIR}"
        )
    endif()

    add_library(mc_nn::onnxruntime SHARED IMPORTED GLOBAL)
    set_target_properties(
        mc_nn::onnxruntime
        PROPERTIES
            IMPORTED_LOCATION "${MC_NN_ONNXRUNTIME_TARGET_LOCATION}"
            INTERFACE_INCLUDE_DIRECTORIES
                "${MC_NN_ONNXRUNTIME_TARGET_INCLUDE_DIR}"
    )
endif()
