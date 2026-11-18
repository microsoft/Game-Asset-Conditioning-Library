# GACL Tests 
GACL components are tested using gtest - each project builds a binary that can be run to test a component of GACL. 

## GitHub Actions reporting
The `Test GACL` workflow builds and runs native tests with read-only repository
permissions, including for pull requests from forks. Each configuration uploads
its XML results as a separate `test-results-<configuration>-<platform>` artifact,
even when tests fail. Missing XML results fail the upload step.

The `Publish GACL test results` workflow runs after testing completes and publishes
the `Test Results (<configuration>-<platform>)` checks on the tested source commit.
It downloads artifacts only from the triggering run, with `actions: read`,
`contents: read`, and `checks: write` permissions. It must not check out or execute
PR code or artifact contents. Cancelled and skipped test runs are not reported;
missing or failing reports fail the reporting workflow.

The reporting workflow must be merged into the default branch before it can run.
Keep the native `Test (<configuration>-<platform>)` jobs as required checks: they
fail directly on test errors, while report publication happens asynchronously and
cannot change the completed test job's conclusion.

## GACL_LIB_TESTS
White box test that links and validates the core of GACL lib.

## GACL_DLL_TESTS
Simple tests that validates the DLL wrapper around the GACL lib by calling the produced dll.

## GACL_EXE_TESTS
Verify exe shell and proprocessing functionalities by calling the exe.

## ML_RDO_TESTS
(Experimental) Verifies ML RDO functionality.