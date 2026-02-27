# GACL Tests 
GACL components are tested using gtest - each project builds a binary that can be run to test a component of GACL. 

## GACL_LIB_TESTS
White box test that links and validates the core of GACL lib.

## GACL_DLL_TESTS
Simple tests that validates the DLL wrapper around the GACL lib by calling the produced dll.

## GACL_EXE_TESTS
Verify exe shell and proprocessing functionalities by calling the exe.

## ML_RDO_TESTS
(Experimental) Verifies ML RDO functionality.