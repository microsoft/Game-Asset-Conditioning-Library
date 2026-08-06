[!\[Build](https://github.com/microsoft/Game-Asset-Conditioning-Library/actions/workflows/build.yml/badge.svg)](https://github.com/microsoft/Game-Asset-Conditioning-Library/actions/workflows/build.yml)
[!\[Test](https://github.com/microsoft/Game-Asset-Conditioning-Library/actions/workflows/test.yml/badge.svg)](https://github.com/microsoft/Game-Asset-Conditioning-Library/actions/workflows/test.yml)

# Introduction

\---

The Game Asset Conditioning Library (GACL) contains several components that help improve compression of game assets.
Texture assets are the primary focus, since those make up the largest portion of most games package size.

In the near future, it is anticipated that we will see optimized CPU offload implementations of ZStandard (Zstd) being developed by hardware vendors. Zstd (https://github.com/facebook/zstd) support is included in DirectStorage 1.4 as part of this effort.

The GACL starts with zstd compression as a baseline, and then provides several tools for improving compression ratios while ensuring
high throughput from the anticipated decompression implementations.  Two approaches of lossy texture data rate
distortion optimization (RDO) are included, block-level and component-level entropy reduction.  Shuffle transforms can be applied prior
to compression to losslessly further improve compression ratios of [block compressed BCn](https://learn.microsoft.com/en-us/windows/win32/direct3d11/texture-block-compression-in-direct3d-11) data streams, with DirectStorage supporting and
implementing the reverse transform at data retrieval time.

# Preview Release #2 (What's New)

As a step in working towards a general release, this preview #2 primarily gives visibility into forward looking features, **including BC7**.  The core functionality of the Game Asset Conditioning Library remains largely unchanged.  Texture shuffle transforms that are not yet supported within the DirectStorage runtime are still marked experimental.  
However, as transforms near completion for inclusion into the supported list, there is a desire to increase visibility into future plans and invite feedback on the transforms and shader implementations.  Additionally, experimental work on CLER continues, as avenues of stacking technologies are investigated.

**Included in preview #2:**

**CLER functional updates**
Component Level Entropy Reduction RDO has been extended with the ability to operate on smaller regions within a texture, both within linear memory layout and the 16KB space curve layout.  For BC1 textures, this means that Block-Level Entropy Reduction and Component-Level Entropy Reduction can be applied in series when experimental features are enabled.  


**Gacl.exe updates**
The front-end sample integration has been extended to allow for BLER+CLER stacking for BC1, allowing for experiments that combine the two techniques.

**Unshuffle shader sources (including BC7)**
Shader sources have been included for visibility and public feedback.  This includes the BC1\3\4\5 unshuffle shaders integrated into the DirectStorage 1.4 preview #1, as well as the BC1\3\4\5\7 shaders planned for pickup in future DirectStorage releases.
Location: \Shaders\Shuffle

**Shuffle shader validation suite** 
Gtest based set of tests for validating any changes to shaders or patterns.  This new test project is included in the root solution file or can be found at \Tests\shader_tests.

**Shuffle shader sample**
D3D UI sample that demonstrates the raw usage of all shaders, implementing the reversal of most GACL transforms in GPU compute.  Given a launch parameter “-path <path_to_dds>”, the texture will be loaded, shuffled though the GACL, and then unshuffled on GPU using the appropriate shader for the transformID:
   
The project is standalone and can be found at \Samples\RawShaderUnshuffleDemo.




\---

GACL is currently **in preview**. We are actively seeking feedback from the community and welcome contributions to help guide the future development of this library.

As a preview:

* **APIs are subject to change.** Interfaces documented in this release, including those in `gacl.h`, `shuffle.h`, `blockentropy.h`, and `ml\_RDO.h`, may be revised based on feedback from the community.
* **Experimental features** (enabled via `GACL\_EXPERIMENTAL`) are particularly likely to evolve or be replaced.

We would love to hear from you - please use [GitHub Issues](https://github.com/microsoft/Game-Asset-Conditioning-Library/issues) to report bugs, share results, or suggest improvements. Pull requests are also welcome; see [CONTRIBUTING.md](CONTRIBUTING.md) for details.

# Getting Started

\---

The GACL preview is buildable via Visual Studio, version 2022 or newer.

1. Installation process
Install latest Visual Studio 2022 from here:  https://aka.ms/vs/17/release/vs\_enterprise.exe
Enable "Desktop Development with C++" in Workloads
2. Software dependencies

   * For lossy ML based implementation:

     * For model download (highly suggested), run the CLER set up script Tools\\scripts\\setupCLER.ps1
     * We currently support CPU-based model inference. Check out other ORT nuget packages and execution providers for GPU support: https://onnxruntime.ai/docs/install/
   * Submodules

     * zstd is included as a submodule within this repository.  From within your cloned GACL repo, zstd can be pulled down via the folowwing commands:

       * `git submodule init`
       * `git submodule update`
3. The solution file "gacl.sln" can be found in the root folder

# Description of Components

\---

Apart from external dependencies, the Game Asset Conditiontioning Library is split into a core functional library designed for
integration into content pipelines which import textures, and a front end tool intended for simplified scenarios.

## Projects within this solution include:

* lbzstd\_p - Project that imports and builds the zstd submodule, but with modified compression settings intended for broader compatibility with future CPU offload implementations of zstd, with a 256KB window size. Builds to static lib form.
* libzstd\_p-dll - As above, but builds to dll form.
* zstd\_p - As above, builds a modified version of the zstd.exe command line tool that limits the window size, which cannot be specified at command
line.  Note that for optimized performance with CPU offload decompression implementations, it is also advised that developers include the following setting when compressing content:

  * \--target-compressed-block-size=8192
* gacl\_lib - Core library that contains APIs for RDO and Shuffle transforms, builds into static lib.
* gacl\_exe - Builds the gacl.exe front end tool that loads textures and applies selected transforms.
* Tests/... - gtest based projects used for validation.

# Build and Test

\---

Before building, or viewing sources in Visual Studio, the zstd submodule must be initialized by the following git commands:

```
git submodule init
git submodule update
```

Primary build solution can be found at:

`<root>\\gacl.sln`

Gtest-based validation projects can be found in the "tests" folder or solution area, and can be directly launched with F5 within Visual Studio.

# Primary API

\---

Top level include (**gacl.h**) will pull in additional headers for the primary functional areas (**shuffle.h**, **blockentropy.h**, **ml\_RDO.h**).
These correspond to three general functional areas:

* Shuffle+Compress
* Block-Level Entropy Reduction (BLER)
* Component-Level Entropy Reduction (CLER)

> Note on experimental\\\\future features: These are turned off by default using the `GACL\_EXPERIMENTAL` C++ define.  See `gacl.h` for further information on experimental features.

## Block-level Entropy Reduction, aka BLER (blockentropy.h):

BLER is a lossy RDO algorithm that reduces entropy within a texture by unifying whole BCn blocks that are visually close, but not identical.
It currently supports BC1-5 \& BC7.  To guide the replacement of encoded BCn data, it examines the decoded R8G8B8A8 representation of
each 4x4 block of pixels.  A second API is included to quickly convert linear data into the required format.

BLER is generally able to reduce the compressed stream size by up to 50% while maintaining PSNR values of 40dB+.

```
void GACL\_RDO\_BlockLevelEntropyReduce(  
    uint32\_t numBlocks,
    void\* encodedData,  
    uint32\_t bcElementSizeBytes,  
    void\* decodedR8G8B8A8,  
    float uniqueBlockReduce,
    float maxDistSq = 64.0f \* 4.0f,  
    float avgDistSq = 64.0f \* 0.5f
);
```



```
void GACL\_RDO\_R8G8B8A8LinearToBlockGrouped(
    uint8\_t\* blockGroupedData,  
    const uint8\_t\* linearR8G8B8A8Data,  
    size\_t rowPitch,
    size\_t width,  
    size\_t height  
);
```

BLER will only unify blocks within the same 256KB window, consistent with the zstd compression settings applied at the shuffle+compress stage. For
tiles or small textures, maximum distance\\reference limit does not have any compression ratio implications.

For large textures, 256KB may only represent a narrow strip of the texture in linear layout.  Experimental curved shuffle transforms (discussed below)
move data within a stream into z-ordered 16KB micro-tiles, ensuring each 16KB\\64KB\\256KB blocks of memory is screen-adjacent.  This maximizes the
ability of RDO to reduce texture sizes, especially for large textures.  Once curved transforms are supported in DirectStorage requests, applying RDO
in 'curved' space will produce higher quality and compression ratios for the same texture and RDO settings.  The gacl.exe front end tool contains an
example of this flow, applying BLER twice (once to linear data, once to curved data) if experimental shuffle+compress is enabled.  Then both streams
of entropy reduced data are included in the Shuffle+Compress request.

## Shuffle and Compression (shuffle.h):

Shuffle+Compress are two lossless processing stages that are handled as a single request for a given texture data stream.  The API will attempt
multiple different shuffle transforms based upon `destTransformId` and `params`, returning the shuffled+compressed data stream for the
shuffle transform that compressed the smallest.  Streams may be individual tiles, groups of tiles, mips, or any other scope of texture data.  At
runtime, the reverse stages (decompression + unshuffle) are required to restore the original data stream.

Currently shuffles for BC1\\3\\4\\5 are supported, with unshuffle operations supported within DirectStorage 1.4.  Experimental BC7 support is
included in the GACL, but runtime unshuffle support is not currently available.

Shuffle+Compress will generally yield up to a relative 10% reduction in compressed stream size, as compared to the same stream compressed with zstd without
shuffling.  Savings can vary widely by texture.

```
HRESULT GACL\_ShuffleCompress\_BCn(
    uint8\_t\* dest,
    GACL\_SHUFFLE\_TRANSFORM\& destTransformId,  
    size\_t\& destBytesWritten,  
    SHUFFLE\_COMPRESS\_PARAMETERS\& params   
);
```

Returns `S\_OK` if shuffle+compress reduced the size of the data, in which case the compressed stream is written to `dest`. The transform ID
in `destTransformID` will be used in Direct Storage read requests for queueing the matching unshuffle transform at runtime.

Returns `S\_FALSE` if shuffle+compress did not produce any reduction in size.  In this case a title should package the asset in uncompressed form.

Shuffle+Compress requests use zstd compression by default, but can be customized by replacing the three global compression function pointers below:

```
typedef HRESULT(\*PGACL\_COMPRESSION\_INITROUTINE)
(void\*\* ccContext, size\_t\* destBytesRequired, const SHUFFLE\_COMPRESS\_PARAMETERS\* params);
typedef HRESULT(\*PGACL\_COMPRESSION\_COMPRESSROUTINE) ( void\* context, void\* dest, size\_t\* destBytes, const void\* src, size\_t srcBytes);
typedef HRESULT(\*PGACL\_COMPRESSION\_CLEANUPROUTINE) ( void\* pContext);

extern PGACL\_COMPRESSION\_INITROUTINE GACL\_Compression\_InitRoutine;
extern PGACL\_COMPRESSION\_COMPRESSROUTINE GACL\_Compression\_CompressRoutine;
extern PGACL\_COMPRESSION\_CLEANUPROUTINE GACL\_Compression\_CleanupRoutine;
```

## Space Curves and transforms

Texture data typically exists in linear (row-major) form until it is uploaded to GPU memory.  This means locations within a 2D texture that
may be near\\adjacent in screen space may be a far distance in memory layout.  For example, a 4K\*4K BC7 mip would require 16MB of memory, and a
single strip of 1024 4x4 elements would represent 16KB of memory.  i.e. two pixels that are adjacent vertically might be 16KB away in memory.

Future CPU offload implementations for zstd decompression may not support references across such a large texture, and the GACL limits zstd compression
to window and distances of <= 256KB.  For the above 4K BC7 example, 256KB would represent of strip of 64 pixels.

When compressing a texture into discrete chunks\\blocks, screen adjacent regions typically produce higher compression ratios than strips of a
texture.  This is partially from higher chances of repeat byte sequences, and partially from reduced distance and encoding cost to those matches.

When applying RDO, there is higher chance to finding "near similar" blocks when searching in a screen-adjacent pattern.

To enable these screen adjacency gains, the GACL introduces the concept of curved transforms, *which are currently experimental*.

Any texture with a height and width that are both a power of 2 BCn elements in each direction, and with a total size that is a multiple of 16KB is
eligible for curved space transforms.  Curved textures are arranged as a z-order series of 16KB micro tiles.  For example, a BC7 mip 1024 pixels
(256 elements) wide, and 512 pixels (128 elements) high would be broken into 32 micro tiles as below:

|||||||||
|-|-|-|-|-|-|-|-|
|0|1|4|5|16|17|20|21|
|2|3|6|7|18|19|22|23|
|8|9|12|13|24|25|28|29|
|10|11|14|15|26|27|30|31|

Each screen-adjacent set of 4 (2x2) micro tiles represent a screen-adjacent 64KB block.  Likewise sets of 16 (4x4) micro tiles represent screen
adjacent 256KB blocks.  One API allows for converting textures to (forward) or from (reverse) curved space, so that RDO or other
compression-improvement operations can be completed in curved space.  Future releases of DirectStorage will include unshuffle shaders that include
support for reversing curved data.

```
bool GACL\_Shuffle\_ApplySpaceCurve(
    uint8\_t\* dest,
    const uint8\_t\* src,
    size\_t size,
    size\_t elementSizeBytes,
    size\_t widthInPixels,
    bool forward
);
```

## Component-level Entropy Reduction (Experimental/RDO\_ML/ML\_RDO.h):

Component-Level Entropy Reduction (CLER) is a lossy RDO algorithm that will attempt to reduce the number of unique values for certain fields within
the block compressed (BCn) data, while using perceptual quality measurement models to minimize loss of detail.  Currently CLER supports BC1 textures.

This feature is experimental and under active development to improve performance.
Execution of this function on a single 4k texture may take many seconds depending on hardware.

Requires `GACL\_EXPERIMENTAL` or `GACL\_INCLUDE\_CLER` to be defined. See `gacl.h`.

```
void GACL\_RDO\_ComponentLevelEntropyReduce(
    void\* encodedData,
    uint32\_t imageWidth,  
    uint32\_t imageHeight, 
    void\* referenceR8G8B8A8,  
    DXGI\_FORMAT format
    RDOOptions\& options
);
```

This experimental feature supports BC1, performing advanced entropy reduction using color endpoint clustering and ML-based perceptual loss metrics on a modifiable buffer of BC texture data, and also an optional R8B8G8A8 reference image. Below are the supporting types:

```
struct RDOOptions {
    int maxClusters = -1;                        - Maximum number of clusters (default: selects based on image size)
    int minClusters = 1;                         - Minimum number of clusters
    int iterations = 7;                          - Number of clustering iterations
    RDOLossMetric metric = RDOLossMetric::LPIPS; - Loss metric to use, see RDOLossMetric
    float lossMin = 0.05f;                       - Minimum loss bound
    float lossMax = 0.1f;                        - Maximum loss bound
    int numThreads = 0;                          - Number of threads (0 = auto)
    bool usePlusPlus = true;                     - Use k-means++ initialization
    bool useClusterRDO = true;                   - Enable advanced RDO and use of loss metric
    bool isGammaFormat = false;                  - Indicates sRGB/gamma format
    void\* onnxModelPtr = nullptr;                - Internal pointer for ONNX model (managed by library)
);

enum class RDOLossMetric{
    MSE,      - Mean Squared Error
    RMSE,     - Root Mean Squared Error
    VGG,      - VGG ml-based perceptual loss
    LPIPS,    - LPIPS ml-based perceptual loss
);

enum class RDO\_ErrorCode : int
```

|Error code|Value|Meaning|
|-|-|-|
|0|OK|Success|
|1|OK\_NoAdvancedRDO|Success, but advanced RDO not used|
|10|NullEncodedData|Input data pointer is null|
|11|UnsupportedEncodedFormat|Encoded format not supported|
|12|InvalidImageDimensions|Image width or height is zero or invalid|
|13|InvalidBCElementSize|Block-compressed element size is invalid|
|14|UnsupportedBaseFormat|Base format not supported|
|20|InvalidClusterRange|Cluster count parameters are out of range|
|21|InvalidLossRange|Loss bounds are invalid (min > max, or out of \[0,1])|
|30|ClusteredSizeMismatch|Internal error: clustered data size mismatch|
|31|ModesSizeMismatch|Internal error: modes data size mismatch|
|32|UnsupportedFormatNotImplemented|Format is not implemented yet|
|40|InternalException|Internal exception occurred during processing|
|50|UnknownError|Unknown error|

# Credits

\---

The GACL library is the work of Richard Meyer, Meredith Green, Paul Edelstein and Zuoming Shi, with further contributions from Di Tang, Simon Craddick, Danny Chen, and Adeline Braun.

