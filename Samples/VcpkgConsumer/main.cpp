// Minimal GACL consumer sample — verifies that the vcpkg overlay port
// installs headers and the library correctly, and that the public API
// is accessible from a downstream CMake project.
//
// Build instructions:
//
//   # From this directory, with the vcpkg overlay port active:
//   # Use -v145- with the "Visual Studio 18 2026" generator instead.
//   cmake -B build -G "Visual Studio 17 2022" -A x64 ^
//     -DCMAKE_TOOLCHAIN_FILE="<vcpkg_root>/scripts/buildsystems/vcpkg.cmake" ^
//     -DVCPKG_OVERLAY_PORTS="../../build/overlays" ^
//     -DVCPKG_OVERLAY_TRIPLETS="../../build/triplets" ^
//     -DVCPKG_TARGET_TRIPLET=x64-windows-v143-static-md
//   cmake --build build
//   ctest --test-dir build --output-on-failure
//
// CLER smoke test (optional — requires ONNX models):
//   Run setupCLER.ps1 first to generate ThirdParty/models/*.onnx, then:
//   build\Debug\gacl_consumer.exe --test-cler

#include <gacl/gacl.h>
#include <gacl/shuffle.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

// Simple logging sink that collects messages for validation.
static bool s_logCalled = false;
static void LogCallback(GACL_Logging_Priority /*priority*/, const wchar_t* msg)
{
    s_logCalled = true;
    std::wprintf(L"[GACL log] %s\n", msg);
}

#if GACL_INCLUDE_CLER
// ---------------------------------------------------------------------------
// Optional CLER smoke test
//
// Builds a synthetic 4x4 BC1 texture (1 block, 8 bytes) and a matching
// R8G8B8A8 reference, then calls GACL_RDO_ComponentLevelEntropyReduce.
// Uses MSE metric (no ONNX model required) but still exercises the full
// code path through initORT() and LoadPerceptualModel().
//
// Returns true on success (OK or OK_NoAdvancedRDO), false on hard error.
// ---------------------------------------------------------------------------
static bool RunCLERSmokeTest()
{
    std::puts("\n--- CLER smoke test ---");

    // Minimal valid BC1 block: two identical 565 endpoints + all-zero indices.
    // This is a degenerate but structurally valid 4x4 block.
    uint8_t bc1Block[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00 };

    // Reference: 4x4 RGBA image, solid white.
    std::vector<uint8_t> ref(4 * 4 * 4, 0xFF);

    RDOOptions opts;
    opts.metric      = RDOLossMetric::MSE;  // no model needed for MSE
    opts.maxClusters = 1;
    opts.minClusters = 1;
    opts.iterations  = 1;

    RDO_ErrorCode result = GACL_RDO_ComponentLevelEntropyReduce(
        8,                          // BC1 element size
        bc1Block,
        ref.data(),
        4, 4,                       // 4x4 image
        DXGI_FORMAT_BC1_UNORM,
        opts);

    if (result == RDO_ErrorCode::OK || result == RDO_ErrorCode::OK_NoAdvancedRDO)
    {
        std::printf("CLER result: %s (code %d) — PASS\n",
            result == RDO_ErrorCode::OK ? "OK" : "OK_NoAdvancedRDO",
            static_cast<int>(result));
        return true;
    }
    else
    {
        std::fprintf(stderr, "CLER result: error code %d — FAIL\n",
            static_cast<int>(result));
        return false;
    }
}
#endif // GACL_INCLUDE_CLER

int main(int argc, char* argv[])
{
#if !GACL_INCLUDE_CLER
    (void)argc;
    (void)argv;
#endif

    // Disable stdout buffering so output is visible even if the process crashes.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    // --- 1. Version check ---
    std::printf("GACL version: %d.%d.%d (%s)\n",
        GACL_VERSION_MAJOR,
        GACL_VERSION_MINOR,
        GACL_VERSION_PATCH,
        GACL_VERSION_STRING);

    // --- 2. Logging callback registration ---
    GACL_Logging_SetCallback(LogCallback);
    // Note: keep LogCallback active for the remainder of the test so that
    // internal GACL messages (e.g. from CLER) are visible and don't crash
    // due to a null function pointer.

    // --- 3. Shuffle API availability ---
    // Simply calling the function confirms the library linked correctly.
    const wchar_t* ext = GACL_ShuffleCompress_GetFileExtensionForTransform(
        GACL_SHUFFLE_TRANSFORM_ZSTD_BC1_224);
    if (ext == nullptr)
    {
        std::fputs("GACL_ShuffleCompress_GetFileExtensionForTransform returned null\n",
                   stderr);
        return 1;
    }
    std::wprintf(L"BC1 shuffle extension: %s\n", ext);

#if GACL_INCLUDE_CLER
    // --- 4. Optional CLER smoke test (pass --test-cler to enable) ---
    bool runCler = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--test-cler")
        {
            runCler = true;
            break;
        }
    }

    if (runCler)
    {
        if (!RunCLERSmokeTest())
            return 1;
    }
    else
    {
        std::puts("CLER test skipped (pass --test-cler to run).");
    }
#endif

    std::puts("GACLVcpkgConsumer: all checks passed.");
    return 0;
}
