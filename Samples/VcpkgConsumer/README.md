# GACLVcpkgConsumer — Minimal vcpkg Consumer Sample

This directory contains a minimal CMake project that acts as an **acceptance
test** for the GACL vcpkg overlay port (`build/overlays/microsoft-gacl/`).  It verifies
that:

1. The overlay port installs GACL headers and the static library correctly.
2. A downstream `CMakeLists.txt` can find GACL via `find_package(GACL CONFIG REQUIRED)`.
3. The public API links and runs — the version macros, `GACL_Logging_SetCallback()`,
   `GACL_ShuffleCompress_GetFileExtensionForTransform()`, and optionally
   `GACL_RDO_ComponentLevelEntropyReduce()` via `--test-cler`.

---

## Prerequisites

- CMake ≥ 3.21 for Visual Studio 2022; ≥ 4.1 for Visual Studio 2026, which
  introduced the `Visual Studio 18 2026` generator
- Visual Studio 2022 (v143 toolset) or Visual Studio 2026 (v145 toolset)
- vcpkg - a recent standalone checkout is recommended. VS-bundled vcpkg
  installations can be several years old and may fail while building a
  transitive dependency, with a 404 that looks unrelated to GACL.

---

## Build & Run

```powershell
# From this directory
# A standalone vcpkg checkout, as recommended in Prerequisites above.
# If you use the VS-bundled copy instead, the edition segment of the path
# varies with your install (Community / Professional / Enterprise).
$vcpkgRoot = "C:\vcpkg"
$overlays  = "..\..\build\overlays"
$triplets  = "..\..\build\triplets"
$installed = "$PWD\vcpkg_installed"   # parent directory (do not append triplet)

# Optional: use an approved NuGet v3 mirror when direct nuget.org access is restricted.
# $env:GACL_NUGET_SOURCE = "https://<approved-nuget-v3-service-index>"

# Pick the toolset you are building with. The generator and the triplet must
# agree: the triplet is what pins vcpkg's dependencies to the same toolset.
$toolset   = "v143"                       # or "v145"
$generator = "Visual Studio 17 2022"      # or "Visual Studio 18 2026"

cmake -B build -G "$generator" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$vcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_OVERLAY_PORTS="$overlays" `
  -DVCPKG_OVERLAY_TRIPLETS="$triplets" `
  -DVCPKG_INSTALLED_DIR="$installed" `
  -DVCPKG_TARGET_TRIPLET="x64-windows-$toolset-static-md" `
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release

ctest --test-dir build -C Release --output-on-failure
```

Expected output:
```
GACL version: 1.1.0 (1.1.0-preview)
BC1 shuffle extension: gacl.bc11
CLER test skipped (pass --test-cler to run).
GACLVcpkgConsumer: all checks passed.
```

The `CLER test skipped` line is absent when the port is built with
`"default-features": false`.

`ctest` reports **1 test**. A second test, `gacl_consumer_cler`, is registered only
when GACL was built with CLER *and* the ONNX models are present. Without them it does
not appear in `ctest -N` at all - so `1/1 passed` reports only `gacl_consumer_runs`;
the CLER path was not exercised. Generating the models is described under the `cler`
feature in the port's `usage` file.

---

## Notes

- `vcpkg.json` declares `microsoft-gacl` as a dependency.  The overlay port at
  `build/overlays/microsoft-gacl/portfile.cmake` resolves it from the local repository
  in the current state.
- The in-repository overlay triplets (`build/triplets/x64-windows-v143-static-md.cmake`
  and `x64-windows-v145-static-md.cmake`) pin dependencies to a specific MSVC toolset.
  Without them, vcpkg selects the newest installed toolset regardless of the generator,
  which on a machine with both Visual Studio versions installed produces static
  libraries incompatible with the consumer build — typically an unresolved-symbol link
  error such as `__std_rotate`.
- Match the triplet to the generator. There is no unsuffixed `x64-windows-static-md`
  triplet in this repository; selecting one would leave the toolset unpinned.
- After the public GitHub repository is live, remove `-DVCPKG_OVERLAY_PORTS` and
  let vcpkg resolve `microsoft-gacl` from the official `microsoft/vcpkg` registry.
  Keep `-DVCPKG_OVERLAY_TRIPLETS` — the toolset pin is still required.

## Compressing non-texture data

GACL's hardware decompression path requires a zstd window of at most 256 KB
(`windowLog` 18). Exceeding it does not corrupt anything — it drops off the
hardware path and decodes roughly **100x slower**, silently.

Texture data conditioned through `GACL_ShuffleCompress_BCn()` is clamped
automatically. For **non-texture** data — audio, geometry, anything else that must
stay on the same hardware path — prefer GACL's own tooling, which applies the
window for you:

```
gacl.exe <input> --compressraw          # writes <input>.zst
```

Raw mode exits non-zero with a warning and writes no file when the data does not
compress smaller.

```c
#include <gacl/shuffle.h>

std::vector<uint8_t> dst(srcBytes);   // srcBytes is always a sufficient capacity
size_t written = 0;

HRESULT hr = GACL_Compression_CompressBuffer(dst.data(), srcBytes, &written, src, nullptr);
// S_OK    - `written` bytes of compressed data in dst
// S_FALSE - did not compress smaller; nothing written, store the original bytes
```

Pass a `GACL_COMPRESS_BUFFER_PARAMETERS*` instead of `nullptr` to override the
compression level or target block size.

If you compress with zstd yourself instead:

- **stock `zstd.exe`:** `zstd --zstd=wlog=18 <input>` — undocumented upstream but
  functional. `--long=` is **not** a substitute: it exists to make windows larger
  and changes other strategy parameters on the way down.
- **linking `libzstd`:** a CLI flag cannot configure your in-process context. Call
  `GACL_Compression_CompressBuffer()`, or `GACL_ShuffleCompress_BCn()` for texture
  data. If you need the `ZSTD_CCtx` itself — streaming, dictionaries, a reused
  context — `GACL_Compression_DefaultInitRoutine()` returns one already configured,
  and `GACL_Compression_DefaultCleanupRoutine()` frees it.

The emitted frame declares a window of *at most* 256 KB — zstd reduces it further
for small inputs, and a decoder provisioned for 256 KB accepts any frame declaring
256 KB or less.
