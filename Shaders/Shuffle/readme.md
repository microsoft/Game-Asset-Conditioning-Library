This folder contains HLSL implementations of the texture unshuffle transforms used with block-compressed texture formats. DirectStorage uses these shaders to provide runtime support for decoding shuffle transforms. The shaders are provided for transparency, reference, and community feedback. 



Shaders can also be used directly, outside of current or future DirectStorage support.  See the standalone

"Samples\\RawShaderUnshuffleDemo" project for example usage.



**Legacy shaders (as included in DirectStorage 1.4 Preview #1)**

UnshuffleBC1.hlsl

UnshuffleBC3.hlsl

UnshuffleBC4.hlsl

UnshuffleBC5.hlsl



**Preview #2 shaders (future DirectStorage support, supersedes\\replaces prior shaders)**

UnshuffleBCnEx.hlsl - unified BC1\\3\\4\\5 and BC7 Join unshuffle (transform ids 0x01-0x04, 0x06 and 0x08-0x0F).  This includes the extensible EX streams, one transform id per format (GACL\_SHUFFLE\_TRANSFORM\_ZSTD\_BC1\_EX 0x0B, \_BC3\_EX 0x0C, \_BC4\_EX 0x0D, \_BC5\_EX 0x0E and \_BC7\_EX 0x0F), whose in-stream header (Lib\\shuffle\\streamheader.h) carries duplicate-block removal, header-coded blocks, the optional space curve, the BC1\\3\\4\\5 index prediction mask (all prediction by default) and the BC7 Join parameters.  Curve-only EX streams (header parameter 2 BCNEX\_UNSHUFFLED with the curve parameter, any of BC1\\3\\4\\5\\7, BC7 blocks carried as BC5 blocks under the BC7\_EX id: space curve, duplicate removal and coded blocks without a shuffle) replace the former UnshuffleCurveOnly.hlsl and its transform id.  Uses BC7JoinDecode.hlsli, BC7JoinTables.hlsli, BCnExTables.hlsli and IndexPredictionHelpers.hlsli.  Same root signature and constants as the BC1\\3\\4\\5 shaders.  A stream of n blocks of B bytes, decoded G blocks per thread, needs ceil((n / G + n % G) / 256) thread groups, with (B, G) per id: BC1 (8, 2), BC3 (16, 4), BC4 (8, 4), BC5 (16, 4), BC7 Join and BC7\_EX (16, 1).  These match the deprecated UnshuffleBC1x\\3x\\4x\\5x.hlsl shaders; ceil(bufferSizeInBytes / 16 / 256) thread groups also cover every id (spare groups exit).  The per-format builds below are faster; the unified build is kept for reference and benchmarking.



UnshuffleBC1Headerless.hlsl, UnshuffleBC3Headerless.hlsl, UnshuffleBC4Headerless.hlsl, UnshuffleBC5Headerless.hlsl, UnshuffleBC1Ex.hlsl, UnshuffleBC3Ex.hlsl, UnshuffleBC4Ex.hlsl, UnshuffleBC5Ex.hlsl and UnshuffleBC7Ex.hlsl - per-format builds of UnshuffleBCnEx.hlsl (its build options), which the host picks by transform id instead of the unified build: UnshuffleBCnHeaderless for the format's headerless ids, UnshuffleBCnEx for its EX id, and UnshuffleBC7Ex for BC7 Join and BC7\_EX.  Same root signature and constants as UnshuffleBCnEx.hlsl.  Without the code of the other formats and paths, each decodes faster than the unified build (RTX 5080: BC4 EX streams of tiles in 0.57-0.86x the time, other EX streams 0.8-1.0x, headerless ids mostly 0.75-1.0x, BC7 Join 0.98x).  A stream of n blocks, decoded T blocks per thread by groups of TG threads, needs ceil((n / T + n % T) / TG) thread groups, with (T, TG) per build: UnshuffleBC1Headerless (0x01, 0x08) and UnshuffleBC1Ex (0x0B) (2, 128); UnshuffleBC3Headerless (0x02, 0x09) and UnshuffleBC3Ex (0x0C) (4, 256); UnshuffleBC4Headerless (0x03) (4, 256); UnshuffleBC4Ex (0x0D) (2, 256); UnshuffleBC5Headerless (0x04, 0x0A) and UnshuffleBC5Ex (0x0E) (4, 128); UnshuffleBC7Ex (0x06, 0x0F) (1, 256).



**BC7 Split:**

PrepassB.hlsl

BinningPassCS\_MinWave\*.hlsl

PrefixSumPassCS\_MinWave\*.hlsl

UnshuffleCSMode\*.hlsl



**Deprecated (superseded by UnshuffleBCnEx.hlsl)**

UnshuffleBC1x.hlsl

UnshuffleBC3x.hlsl

UnshuffleBC4x.hlsl

UnshuffleBC5x.hlsl

