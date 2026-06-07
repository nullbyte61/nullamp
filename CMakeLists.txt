cmake_minimum_required(VERSION 3.15)

project(Nullamp VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)          # NeuralAmpModelerCore requires C++20
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# GNU extensions ON: DPF's DGL (WebView, pulled in by USE_FILE_BROWSER) uses `typeof`.
# gnu++20 is a superset of c++20 and harmless for our own sources.
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Link one target at a time. Every target whole-archives the Eigen-heavy nam_core, and
# linking several at once can exhaust memory on small CI runners (the build stalls).
# Compiles still run in parallel. Ninja-only; other generators ignore this.
set_property(GLOBAL PROPERTY JOB_POOLS link_pool=1)
set(CMAKE_JOB_POOL_LINK link_pool)

set(NAM_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/NeuralAmpModelerCore")
set(ADSP_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/deps/AudioDSPTools")

if(NOT EXISTS "${NAM_CORE_DIR}/NAM/get_dsp.cpp")
    message(FATAL_ERROR "deps/ submodules missing. Run: git submodule update --init --recursive")
endif()

# --- Inference engine: NeuralAmpModelerCore + AudioDSPTools + our NamModel wrapper ----------
file(GLOB NAM_CORE_SOURCES "${NAM_CORE_DIR}/NAM/*.cpp" "${NAM_CORE_DIR}/NAM/*/*.cpp")

# AudioDSPTools blocks used by the signal chain (IR, DC blocker, wav loading).
set(ADSP_SOURCES
    "${ADSP_DIR}/dsp/dsp.cpp"
    "${ADSP_DIR}/dsp/ImpulseResponse.cpp"
    "${ADSP_DIR}/dsp/NoiseGate.cpp"
    "${ADSP_DIR}/dsp/RecursiveLinearFilter.cpp"
    "${ADSP_DIR}/dsp/wav.cpp")
set_source_files_properties(${ADSP_SOURCES} PROPERTIES COMPILE_FLAGS "-w") # 3rd-party warnings

add_library(nam_core STATIC
    ${NAM_CORE_SOURCES}
    ${ADSP_SOURCES}
    plugin/NamModel.cpp
    plugin/ToneStack.cpp)

target_include_directories(nam_core PUBLIC
    "${NAM_CORE_DIR}"                       # NAM/dsp.h, NAM/get_dsp.h
    "${NAM_CORE_DIR}/Dependencies/eigen"    # <Eigen/Dense>
    "${NAM_CORE_DIR}/Dependencies/nlohmann" # json.hpp
    "${ADSP_DIR}/dsp"                       # ResamplingContainer/ResamplingContainer.h
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin")   # NamModel.hpp

# The A2 fast-path WaveNet is a big translation unit that is slow to compile at -O3 and
# bloats every whole-archive link. On by default; CI turns it off to keep builds quick.
option(NAM_ENABLE_A2_FAST "Build the A2 fast-path WaveNet" ON)
if(NAM_ENABLE_A2_FAST)
    target_compile_definitions(nam_core PUBLIC NAM_ENABLE_A2_FAST)
endif()
target_compile_features(nam_core PUBLIC cxx_std_20)
set_target_properties(nam_core PROPERTIES POSITION_INDEPENDENT_CODE ON) # linked into shared plugin modules
target_compile_options(nam_core PRIVATE $<$<CONFIG:Release>:-O3>)

# --- DPF framework ----------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dpf/CMakeLists.txt")
    message(FATAL_ERROR "dpf/ submodule missing. Run: git submodule update --init --recursive")
endif()
add_subdirectory(dpf)

# --- The plugin -------------------------------------------------------------
dpf_add_plugin(Nullamp
    TARGETS jack lv2 clap vst3
    USE_FILE_BROWSER            # enables UI::requestStateFile() native dialogs
    FILES_DSP
        plugin/NamPlugin.cpp
    FILES_UI
        ui/NamUI.cpp)

target_include_directories(Nullamp PUBLIC plugin)
# WHOLE_ARCHIVE: the core registers architectures (WaveNet/ConvNet/LSTM) via file-scope
# static initializers; without this they're stripped at the final shared-module link.
target_link_libraries(Nullamp PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
target_compile_options(Nullamp PRIVATE $<$<CONFIG:Release>:-O3>)

# --- Tools / tests ----------------------------------------------------------
option(NAM_DPF_BUILD_TESTS "Build the render_cli A/B tool and upstream render reference" ON)
if(NAM_DPF_BUILD_TESTS)
    # wav.cpp now lives in nam_core, so the tools just link it (no duplicate symbols).

    # Our renderer (routes through NamModel). This is the thing we validate.
    add_executable(render_cli test/render_cli.cpp)
    target_link_libraries(render_cli PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
    target_include_directories(render_cli PRIVATE "${ADSP_DIR}/dsp")

    # Upstream reference renderer, built from the core submodule's own tool.
    add_executable(nam_render "${NAM_CORE_DIR}/tools/render.cpp")
    target_link_libraries(nam_render PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
    target_include_directories(nam_render PRIVATE
        "${NAM_CORE_DIR}" "${ADSP_DIR}/dsp")

    # CPU headroom benchmark for the inference path.
    add_executable(bench_chain test/bench_chain.cpp)
    target_link_libraries(bench_chain PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
    target_include_directories(bench_chain PRIVATE "${ADSP_DIR}/dsp")

    # Functional checks for the tone stack and noise gate.
    add_executable(chain_test test/chain_test.cpp)
    target_link_libraries(chain_test PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
    target_include_directories(chain_test PRIVATE "${ADSP_DIR}/dsp" "${CMAKE_CURRENT_SOURCE_DIR}/plugin")
endif()

# --- Install ----------------------------------------------------------------
# Installs the built bundles into the standard per-user or system plugin dirs.
# Default prefix ~/.local is fine; packagers override CMAKE_INSTALL_PREFIX.
set(NAM_DPF_BIN "${CMAKE_BINARY_DIR}/bin")
include(GNUInstallDirs)
install(DIRECTORY "${NAM_DPF_BIN}/Nullamp.lv2"  DESTINATION "${CMAKE_INSTALL_LIBDIR}/lv2"  USE_SOURCE_PERMISSIONS OPTIONAL)
install(DIRECTORY "${NAM_DPF_BIN}/Nullamp.vst3" DESTINATION "${CMAKE_INSTALL_LIBDIR}/vst3" USE_SOURCE_PERMISSIONS OPTIONAL)
install(PROGRAMS  "${NAM_DPF_BIN}/Nullamp.clap" DESTINATION "${CMAKE_INSTALL_LIBDIR}/clap" OPTIONAL)
install(PROGRAMS  "${NAM_DPF_BIN}/Nullamp"      DESTINATION "${CMAKE_INSTALL_BINDIR}"      OPTIONAL)

message(STATUS "Nullamp ${PROJECT_VERSION}. Targets: jack lv2 clap vst3; tests=${NAM_DPF_BUILD_TESTS}")
