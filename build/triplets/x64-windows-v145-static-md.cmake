# GACL triplet: x64, dynamic CRT, static libs, built with the VS 2026 (v145) toolset.
# See x64-windows-v143-static-md.cmake for why the toolset is pinned.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PLATFORM_TOOLSET v145)
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED GACL_NUGET_SOURCE)
