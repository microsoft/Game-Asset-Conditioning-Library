This is a LibFuzzer project that exercises the public facing APIs looking for any functional or security issues.

More information on LibFuzzer can be found here: https://llvm.org/docs/LibFuzzer.html

Full command line options can be seen using the command:
gacl_fuzz -help=1

Because the GACL APIs are heavy weight, including large allocations, threads, and even AI models on a per-call basis, LibFuzzer runs out of memory within just a few iterations.  There are built in options to account for this though, with the "fork" command telling the fuzzer to maintain a pool of child processes where actual fuzzing occurs.  Those instances can be told to then self-terminate and cycle as necessary to allow for long fuzzing runs.

Arguments with double dashes are passed to child processes, and ignored by the primary.  Additionally this fuzzer looks for one the presence of one of these options to force the targeting of specific APIs within the GACL:
--BC1
--BC3
--BC4
--BC5
--BC7
--BLER
--CLER

The fuzzer can be run without an initial dds file corpus, and LibFuzzer will start with small random streams, adjusting based on feedback from code coverage measurements.  The below example would Launch the fuzzer with no overall time limit fuzzing the BC7 Shuffle+Compress API, cycling child processes every 5 seconds:

gacl_fuzz.exe -fork=5 --max_total_time=5 --BC7

The fuzzer can also be run with a folder as the first parameter, in which case it will use those example files as the starting point for fuzzed streams.  In this case Shuffle+Compress is the assumed target, and the format target override is optional.

gacl_fuzz.exe TestAssetRoot\Mip0\BC7 -fork=5 --max_total_time=5

Currently release teams manually run 24h of fuzzing before release, pending advice from OSS\corp security stake holders.  BC1\3\4\5 are trivial and provably safe.  BC7\CLER\BLER are in scope, with BC7 being primary for fuzzing: 

24h - BC7 corpus guided (any pool of small BC7 textures, <=64KB):
	  gacl_fuzz.exe <small_BC7_corpus> -fork=5 -max_total_time=86400 --max_total_time=5

1h - BC7 unguided
	  gacl_fuzz.exe -fork=5 -max_total_time=3600 --max_total_time=5 --BC7

2h+ - BLER corpus guided (any pool of small BC1 textures, <=64KB):
	  gacl_fuzz.exe <small_BC1_corpus> -fork=5 -max_total_time=7200 --max_total_time=5 --BLER

2h+ - CLER corpus guided (any pool of small BC1\7 textures, <=64KB):
	  gacl_fuzz.exe <small_BC1\7_corpus> -fork=5 -max_total_time=7200 --max_total_time=5 --CLER

Run results to be logged int ADO for tracking.