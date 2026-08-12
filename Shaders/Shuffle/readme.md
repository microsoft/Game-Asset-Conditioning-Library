This folder contains the shaders used by DirectStorage to provide unshuffling support.  They are included here for

transparency and to allow public feedback.  



Shaders can also be used directly, outside of current or future DirectStorage support.  See the standalone

"Samples\\RawShaderUnshuffleDemo" project for example usage.



**Legacy shaders (as included in DirectStorage 1.4 Preview #1)**

UnshuffleBC1.hlsl

UnshuffleBC3.hlsl

UnshuffleBC4.hlsl

UnshuffleBC5.hlsl



**Preview #2 shaders (future DirectStorage support, supersedes\\replaces prior shaders)**

UnshuffleBC1x.hlsl

UnshuffleBC2x.hlsl

UnshuffleBC4x.hlsl

UnshuffleBC5x.hlsl

UnshuffleCurveOnly.hlsl



**BC7:**

PrepassB.hlsl

BinningPassCS\_MinWave\*.hlsl

PrefixSumPassCS\_MinWave\*.hlsl

UnshuffleCSMode\*.hlsl

