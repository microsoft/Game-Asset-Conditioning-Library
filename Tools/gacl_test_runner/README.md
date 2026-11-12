# GACL test runner

A thin native Windows x64 launcher for the four existing GoogleTest executables: `gacl_lib_tests`, `gacl_exe_tests`, `ml_rdo_tests`, and `shader_tests`. Like the Zstd tests, results are printed to stdout as tests run. Fuzzing remains separate. No Python, PowerShell, C#, report parser, or separate wrapper-test framework is required.

Build the runner and suites from `gacl.sln` in Debug or Release x64. With v143, the executables are in `bin/v143/x64/<configuration>/`. Use x64 runtime DLLs, compiled GPU shader `.cso` files, and applicable fixtures/models. On Windows ARM64, run the complete x64 package under Windows x64 emulation, including x64 runtime dependencies; do not mix native ARM64 DLLs with x64 executables. The launcher does not build or download dependencies or change graphics settings.

## Build

From a Visual Studio developer shell at the repository root:

```powershell
.\Tools\scripts\restorePackages.ps1 -Configuration Release
msbuild gacl.sln /t:Build /m /p:Configuration=Release /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw "GACL build failed" }
```

This uses the normal root vcpkg manifest, including its SDK dependencies, and the configured NuGet source. Native ARM64 encoding is not built by this integration. Export the ML models with `Tools\scripts\setupCLER.ps1` before building; the ML project's post-build target stages them automatically.

## Run a content folder

From the binary directory:

```powershell
.\gacl_test_runner.exe --binary-directory . --input-directory C:\Textures
$LASTEXITCODE
```

Each suite discovers files recursively in sorted order and uses normal GoogleTest `[ RUN ]`, `[ OK ]`, `[ FAILED ]`, and `[ SKIPPED ]` formatting. Case names use a file index and the relative path with punctuation replaced by underscores; no hashing is involved. Adding files can change the indices. Original relative paths remain available as GoogleTest value parameters and optional XML properties.

For example, if `BC1mip0/Ladybug_Body_albedo.DDS` is the first discovered file:

```text
[ RUN      ] ContentInput.File_0_BC1mip0_Ladybug_Body_albedo_DDS
[       OK ] ContentInput.File_0_BC1mip0_Ladybug_Body_albedo_DDS (1887 ms)
```

Assertion messages and unsupported reasons appear through the normal GoogleTest printer. Unsupported inputs are **SKIPPED** with an explicit `[UNSUPPORTED]` reason, not reported as passes. The enclosing suite heading identifies the executable. There is no replay command, retained-work-directory option, mandatory results directory, JSON summary, automatic XML, or wrapper log file. Suites own validation; the launcher owns process lifetime and aggregate exit status.

The final `[ RUN SUMMARY ]` repeats each suite's outcome, elapsed milliseconds, and exit code or launch/timeout error. Nonzero exits include hexadecimal status; access violations and fail-fast exits are labeled. Suite failures and timeout messages use stdout with the suite markers so they remain in order. Individual test counts remain in GoogleTest's summaries; the launcher does not parse them.

A normal shell redirect is enough to save output when needed:

```powershell
.\gacl_test_runner.exe --binary-directory . --input-directory C:\Textures *> run-output.txt
```

## Choose suites and cases

```powershell
.\gacl_test_runner.exe --binary-directory . --input-directory C:\Textures --suite gacl_lib_tests
.\gacl_test_runner.exe --binary-directory . --input-directory C:\Textures --suite shader_tests
.\gacl_test_runner.exe --binary-directory . --suite gacl_lib_tests --gtest_filter="CompressBuffer.*"
.\gacl_test_runner.exe --binary-directory . --input-directory C:\Textures --suite gacl_lib_tests --gtest_filter="ContentInput.*Ladybug*"
```

| Argument | Meaning |
|---|---|
| `--binary-directory <path>` | Directory containing test EXEs and runtime dependencies; defaults to the runner's directory. |
| `--input-directory <path>` | Optional: recursive file-validation mode. |
| `--suite <name>` | Repeat to select suites; default all four listed above. |
| `--gtest_filter=<pattern>` | GoogleTest selection; a separate value is also accepted. Default `ContentInput.*` with an input folder; otherwise `*`. |
| `--timeout-seconds <n>` | Overall limit shared by all selected suites, default 3600 seconds (one hour). A suite only receives the time left in that budget. |
| `--help` | Show usage. |

The runner creates isolated temporary suite directories and removes them on completion, including ordinary failures/timeouts. CLI cases keep their input copies and exports beneath the suite working directory so forced suite termination also leaves them under launcher cleanup. Temporary-lock cleanup is retried briefly; persistent cleanup errors are printed. Inputs are read, not modified, and must remain stable during execution. Direct suite runs must also use a working directory outside the inputs. Some validators hold a whole file/texture in memory; this is not a streaming large-file harness.

## What each suite validates in folder mode

| Suite | Validation and limits |
|---|---|
| Library | Nonempty arbitrary-file compression/Zstd roundtrip or documented store-original fallback. Supported BC1/3/4/5/7 DDS also receive CPU shuffle/inverse checks across subresources. Other formats get buffer-only validation. |
| CLI | Typed BC1/3/4/5/7 DDS through `gacl.exe`, export presence/header checks and saved-DDS validation. Not a full export decoder or visual-quality test. |
| ML/RDO | PNG MSE/RMSE checks; basic CLER on applicable BC1 top mips without requiring a reference. A same-stem PNG additionally exercises reference-based MSE RDO and rejects fallback to basic clustering. No folder-mode perceptual-model or quality-threshold claim. |
| Shader | Actual supported single-2D BC1/3/4/5/7 DDS data through GPU unshuffle comparison. Current folder-mode input is mip 0 only, not the complete synthetic parameter matrix. Shapes exceeding the selected format's shader dispatch limit are unsupported even within the D3D12 texture-size limit. Unsupported shapes/formats are explicit. |

An optional CLER reference must decode and match the DDS dimensions; malformed/mismatched references fail. CLER format/dimension limits still apply. PNG files also have independent loss-metric cases. Skips follow normal GoogleTest behavior; a suite with no passing tests, or a content run with no passing content cases, fails. Assertions and nonzero test exits also fail.

## Fixed regressions and machine requirements

Omit the input folder to run original fixed regressions with normal GoogleTest output:

```powershell
.\gacl_test_runner.exe --binary-directory .
```

Preserve fixed fixtures and dependencies. The ML suite stages its own `test_assets/` and checks its perceptual-model coverage. Its model copies must match beside EXEs and three EXE-directory parents above at `ThirdParty/models`; for `bin/v143/x64/Release`, the latter is `bin/ThirdParty/models`. Folder MSE cases do not require those perceptual models. Shader tests require a suitable D3D12 environment.

Exit **0** means every selected suite passed. Exit **1** means at least one suite failed, timed out, or could not start; the other suites are attempted only while time remains. On timeout, the active process tree is stopped and remaining suites are reported as not run, not passes. Exit **2** means an overall argument or working-directory setup error. Existing product/validation failures are not waived.

Processes start suspended and enter a kill-on-close Windows Job Object before execution. Remaining children are stopped when a suite finishes or times out. Child stdout/stderr flow directly to the caller, including shell redirects and pipes. The normal environment is inherited; the launcher explicitly sets the filter, disables colored output, and runs each suite once.

For listing or optional GoogleTest XML, invoke a suite directly with `--gtest_list_tests` or `--gtest_output=xml:<path>`. The launcher does not expose these options or parse reports.

## CI artifacts and model staging

The ML project's post-build target copies both exported models beside the EXEs and to the existing CLER lookup location, including custom output directories. It fails on missing/empty exports; run `Tools\scripts\setupCLER.ps1` first if needed. The GitHub build overrides the output folder to `out/<configuration>/x64/bin/`, so the second model location is `out/ThirdParty/models/`. No separate model-staging script is required.

When copying or downloading the `out` artifact, preserve the complete layout, including its shared `ThirdParty/models` tree. Keep the suite EXEs, runtime DLLs, compiled shaders and fixture directories together. The ML suite can run from its own EXE directory without copying fixtures onto themselves, or from the launcher's isolated working directory.

CI also runs the CPU launcher selection `--suite gacl_lib_tests --gtest_filter="CompressBuffer.*"`. This checks launcher integration without requiring a hardware GPU. It does not replace shader or clean lab-machine qualification.

The launcher links its own runtime statically, but the existing test programs and `gacl.exe` still import MSVC/OpenMP runtime DLLs. Debug builds additionally import Debug runtime DLLs such as `MSVCP140D.dll`, `VCRUNTIME140D.dll` and `ucrtbased.dll`; these are not staged by the model-copy target. A successful local run does not establish that a lab machine has those prerequisites. Verify them with the machine owner; this build integration does not install or redistribute runtimes automatically.

Keep source/configuration, content provenance and console output with any failure report. Local execution from a relocated artifact does not prove clean-machine portability or lab readiness; qualify those separately.
