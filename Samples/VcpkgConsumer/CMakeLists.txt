cmake_minimum_required(VERSION 3.21)

project(GACLVcpkgConsumer
    VERSION 1.0.0
    DESCRIPTION "Minimal consumer sample for GACL via vcpkg"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# GACL is installed via the overlay port in build/overlays/microsoft-gacl/.
# When using vcpkg manifest mode, CMake finds it automatically via
# the vcpkg toolchain file:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
find_package(GACL CONFIG REQUIRED)

add_executable(gacl_consumer main.cpp)

target_link_libraries(gacl_consumer PRIVATE GACL::gacl_lib)

# Smoke-test: build succeeds and version prints correctly at runtime.
enable_testing()
add_test(NAME gacl_consumer_runs COMMAND gacl_consumer)

# Optional CLER smoke test: only registered when ONNX models are present.
# Run setupCLER.ps1 first, then re-run cmake or just invoke the test manually:
#   build\Debug\gacl_consumer.exe --test-cler
if(DEFINED GACL_MODELS_DIR)
    set(_models_dir "${GACL_MODELS_DIR}")
else()
    # Default: repo root / ThirdParty / models  (works for in-repo builds)
    get_filename_component(_models_dir
        "${CMAKE_CURRENT_SOURCE_DIR}/../../ThirdParty/models" ABSOLUTE)
endif()

get_target_property(_gacl_defs GACL::gacl_lib INTERFACE_COMPILE_DEFINITIONS)
list(FIND _gacl_defs "GACL_INCLUDE_CLER=1" _gacl_cler_enabled_idx)
set(_gacl_cler_enabled FALSE)
if(_gacl_cler_enabled_idx GREATER -1)
    set(_gacl_cler_enabled TRUE)
endif()

if(_gacl_cler_enabled AND EXISTS "${_models_dir}/lpips_model.onnx" AND EXISTS "${_models_dir}/vgg_model.onnx")
    message(STATUS "CLER ONNX models found at ${_models_dir} — enabling CLER smoke test")
    add_test(NAME gacl_consumer_cler COMMAND gacl_consumer --test-cler)
else()
    message(STATUS "CLER smoke test disabled (requires GACL built with CLER and ONNX models present)")
endif()
