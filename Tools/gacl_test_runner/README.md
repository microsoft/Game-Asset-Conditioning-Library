# GACL test runner

A thin native Windows x64 launcher for the four existing GoogleTest executables: `gacl_lib_tests`, `gacl_exe_tests`, `ml_rdo_tests`, and `shader_tests`. Like the Zstd tests, results are printed to stdout as tests run. Fuzzing remains separate. No Python, PowerShell, C#, report parser, or separate wrapper-test framework is required.

Build the runner and suites from `gacl.sln` in Debug or Release x64. With v143, the executables are in `bin/v143/x64/<configuration>/`. Preserve the suites' existing runtime DLLs, shader `.cso` files, and applicable fixtures/models. The launcher does not build or download dependencies or change graphics settings.

## Run a content folder

From the binary directory:

```powershell
.\gacl_test_runner.exe --binary-directory . --input-directory C:\Textures
$LASTEXITCODE
```

Each suite recursively selects files and uses normal GoogleTest `[ RUN ]`, `[ OK ]`, `[ FAILED ]`, and `[ SKIPPED ]` formatting. Case names come from relative paths, with punctuation replaced by underscores and a stable suffix to distinguish collisions. They do not change just because another earlier-sorting file is added. Original relative paths remain available as GoogleTest value parameters and optional XML properties.

For example, this actual case name comes from `BC1mip0/Ladybug_Body_albedo.DDS`:

```text
[ RUN      ] ContentInput.BC1mip0_Ladybug_Body_albedo_DDS_e2c20ad2cc2c3fa2
[       OK ] ContentInput.BC1mip0_Ladybug_Body_albedo_DDS_e2c20ad2cc2c3fa2 (1887 ms)
```

Before each suite, `[ TEST CWD ]` and `[ TEST CMD ]` print copyable PowerShell working-directory and executable commands, including the exact input/filter settings. Quoting preserves spaces, Unicode, apostrophes and `$`. Automatic working directories are deleted after the run, so use `--work-directory` if you want those printed paths to remain available for replay.

Assertion messages and unsupported reasons appear through the normal GoogleTest printer. Unsupported inputs are **SKIPPED** with an explicit `[UNSUPPORTED]` reason, not reported as passes. The enclosing suite heading identifies the executable. There is no automatic discovery phase, mandatory results directory, JSON summary, automatic XML, or wrapper log file. Suites own coverage checks; the launcher owns process lifetime and aggregate exit status.

A normal shell redirect is enough to save output when needed:

```powershell
.\gacl_test_runner.exe --binary-directory . --input-directory C:\Textures *> run-output.txt
```

## Choose suites and cases

```powershell
.\gacl_test_runner.exe --binary-directory . --input-directory C:\Textures --suite gacl_lib_tests
.\gacl_test_runner.exe --binary-directory . --input-directory C:\Textures --suite shader_tests
.\gacl_test_runner.exe --binary-directory . --suite gacl_lib_tests --gtest_filter="CompressBuffer.*"
.\gacl_test_runner.exe --binary-directory . --input-directory C:\Textures --suite gacl_lib_tests --gtest_list_tests
.\gacl_test_runner.exe --binary-directory . --input-directory C:\Textures --suite gacl_lib_tests --gtest_filter="ContentInput.*Ladybug*"
```

| Argument | Meaning |
|---|---|
| `--binary-directory <path>` | Required: directory containing test EXEs and runtime dependencies. |
| `--input-directory <path>` | Optional: recursive file-validation mode. |
| `--suite <name>` | Repeat to select suites; default all four listed above. |
| `--gtest_filter=<pattern>` | GoogleTest selection; a separate value is also accepted. Default `ContentInput.*` with an input folder; otherwise `*`. |
| `--filter <pattern>` | Backward-compatible alias for `--gtest_filter`. Specify only one filter spelling. |
| `--gtest_list_tests` | List selected cases without executing them; a successful list is labeled `SUITE LISTED`, not a passing test run. |
| `--timeout-seconds <n>` | Per-suite limit, default 1500 seconds (25 minutes). |
| `--work-directory <path>` | Optional new folder to retain generated test files. Not a report directory. |
| `--help` | Show usage. |

Without `--work-directory`, the runner creates isolated temporary suite directories and removes them on completion, including ordinary failures/timeouts. Temporary-lock cleanup is retried briefly; persistent cleanup errors are printed. To inspect generated exports, explicitly retain a new work folder:

```powershell
.\gacl_test_runner.exe --binary-directory . --input-directory C:\Textures --work-directory C:\GaclWork\run-001
```

The explicit work folder must not exist already and must be outside the input tree. Inputs are read-only, links/junctions inside the input tree are refused, and the input tree must remain stable during execution. Direct suite runs must also use a working directory outside the inputs. Some validators hold a whole file/texture in memory; this is not a streaming large-file harness.

## What each suite validates in folder mode

| Suite | Validation and limits |
|---|---|
| Library | Nonempty arbitrary-file compression/Zstd roundtrip or documented store-original fallback. Supported BC1/3/4/5/7 DDS also receive CPU shuffle/inverse checks across subresources. Other formats get buffer-only validation. |
| CLI | Typed BC1/3/4/5/7 DDS through `gacl.exe`, export presence/header checks and saved-DDS validation. Not a full export decoder or visual-quality test. |
| ML/RDO | PNG MSE/RMSE checks; basic CLER on applicable BC1 top mips without requiring a reference. A same-stem PNG additionally exercises reference-based MSE RDO and rejects fallback to basic clustering. No folder-mode perceptual-model or quality-threshold claim. |
| Shader | Actual supported single-2D BC1/3/4/5/7 DDS data through GPU unshuffle comparison. Current folder-mode input is mip 0 only, not the complete synthetic parameter matrix. Unsupported shapes/formats are explicit. |

An optional CLER reference must decode and match the DDS dimensions; malformed/mismatched references fail. CLER format/dimension limits still apply. PNG files also have independent loss-metric cases. Unsupported inputs use GoogleTest `SKIPPED` with an `[UNSUPPORTED]` reason, never a pass. A suite with no enabled tests or no applicable passing content fails. Unexpected skipped tests, assertions and nonzero test exits also fail.

## Fixed regressions and machine requirements

Omit the input folder to run original fixed regressions with normal GoogleTest output:

```powershell
.\gacl_test_runner.exe --binary-directory .
```

Preserve fixed fixtures and dependencies. The ML suite stages its own `test_assets/` and checks its perceptual-model coverage. Its model copies must match beside EXEs and three EXE-directory parents above at `ThirdParty/models`; for `bin/v143/x64/Release`, the latter is `bin/ThirdParty/models`. Folder MSE cases do not require those perceptual models. Shader tests require a suitable D3D12 environment.

Exit **0** means every selected suite passed its coverage policy. Exit **1** means at least one suite failed, timed out, had leftover children, or could not start; the other suites are still attempted. Exit **2** means an overall argument or working-directory setup error. Existing product/validation failures are not waived.

Processes start suspended and enter a kill-on-close Windows Job Object before execution. Only standard streams are inherited; child stdout/stderr flow directly to the caller, including shell redirects and pipes. Inherited `GTEST_*` variables are cleared to avoid unintended sharding/repetition/output changes. Disabled tests stay disabled.

For optional GoogleTest XML, invoke a suite directly with its normal `--gtest_output=xml:<path>` option. The launcher neither requires nor parses that output. Use `--gtest_list_tests` on the launcher or directly on a suite to inspect names. Listing initializes the suite (including D3D12 for shader tests), but does not execute cases. A normal run still launches each suite only once. The launcher accepts the documented filter/list options, not arbitrary GoogleTest settings that could change coverage or overwrite files.

## CI artifacts and model staging

The GitHub build overrides the output folder to `out/<configuration>/x64/bin/`. After building, it runs this build-time helper:

```powershell
.\Tools\scripts\stageTestModels.ps1 -BinaryDirectory .\out\Release\x64\bin
```

Run `Tools\scripts\setupCLER.ps1` first if exported models are not available. Staging fails on missing/empty exports; it copies both models beside the EXEs and to the existing CLER lookup location. For the CI layout, that second location is `out/ThirdParty/models/`. This PowerShell step is build tooling, not a lab runtime requirement.

When copying or downloading the `out` artifact, preserve the complete layout, including its shared `ThirdParty/models` tree. Keep the suite EXEs, runtime DLLs, compiled shaders and fixture directories together. The ML suite can run from its own EXE directory without copying fixtures onto themselves, or from the launcher's isolated working directory.

CI also runs the CPU launcher selection `--suite gacl_lib_tests --gtest_filter="CompressBuffer.*"`. This checks launcher integration without requiring a hardware GPU. It does not replace shader or clean lab-machine qualification.

The launcher links its own runtime statically, but the existing test programs and `gacl.exe` still import MSVC/OpenMP runtime DLLs. Debug builds additionally import Debug runtime DLLs such as `MSVCP140D.dll`, `VCRUNTIME140D.dll` and `ucrtbased.dll`; these are not staged by the model helper. A successful local run does not establish that a lab machine has those prerequisites. Verify them with the machine owner; this build integration does not install or redistribute runtimes automatically.

Keep source/configuration, content provenance and console output with any failure report. Local execution from a relocated artifact does not prove clean-machine portability or lab readiness; qualify those separately.
