//-------------------------------------------------------------------------------------
// main.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include  "gacl.h"
#include "../helpers/Utility.h"
#include "Processing.h"
#define ZSTD_STATIC_LINKING_ONLY
#include "../ThirdParty/zstd/lib/zstd.h"

#include <iostream>
#include <algorithm>


using namespace std;
using namespace gacl;

static void PrintVersion(void)
{
    printf("\n");
    printf("Game Asset Conditioning Library Tool v" GACL_VERSION_STRING "\n");
#if GACL_INCLUDE_CLER
    printf("Copyright (c) Microsoft 2026, and others.\n");
#else
    printf("Copyright (c) Microsoft 2026\n");
#endif
    printf("Build Date:  %s\n", __DATE__);
    printf("\n");
    printf("Run \"gacl.exe help\" for usage instructions\n");
#if GACL_INCLUDE_CLER
    printf("Run \"gacl.exe license\" for full license information\n");
#endif
}

#if GACL_INCLUDE_CLER
static void PrintThirdPartyLicenses(void)
{
    printf("*** Third party component notice: torchvision ***\n");
    printf("https://docs.pytorch.org/vision/stable/index.html");
    printf("\n");
    printf("BSD 3 - Clause License\n");
    printf("\n");
    printf("Copyright(c) Soumith Chintala 2016,\n");
    printf("All rights reserved.\n");
    printf("\n");
    printf("Redistribution and use in source and binary forms, with or without\n");
    printf("modification, are permitted provided that the following conditions are met : \n");
    printf("\n");
    printf(" * Redistributions of source code must retain the above copyright notice, this\n");
    printf("   list of conditions and the following disclaimer.\n");
    printf("\n");
    printf(" * Redistributions in binary form must reproduce the above copyright notice, \n");
    printf("   this list of conditions and the following disclaimer in the documentation\n");
    printf("   and /or other materials provided with the distribution.\n");
    printf("\n");
    printf(" * Neither the name of the copyright holder nor the names of its\n");
    printf("   contributors may be used to endorse or promote products derived from\n");
    printf("   this software without specific prior written permission.\n");
    printf("\n");
    printf("THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\"\n");
    printf("AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE\n");
    printf("IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE\n");
    printf("DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE\n");
    printf("FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL\n");
    printf("DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR\n");
    printf("SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER\n");
    printf("CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, \n");
    printf("OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE\n");
    printf("OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n");
    printf("\n");
    printf("\n");
    
    printf("*** Third party component notice: VGG ***\n");
    printf("https://www.robots.ox.ac.uk/~vgg/research/very_deep/");
    printf("\n");
    printf("Copyright(c) Karen Simonyan and Andrew Zisserman\n");
    printf("\n");
    printf("Used in unmodified form under Creative Commons Attribution License:\n");
    printf("https://creativecommons.org/licenses/by/4.0/\n");
    printf("\n");
    printf("\n");

    printf("*** Third party component notice: ZStandard (zstd) ***\n");
    printf("https://github.com/facebook/zstd\n");
    printf("\n");
    printf("BSD License\n");
    printf("\n");
    printf("For Zstandard software\n");
    printf("\n");
    printf("Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.\n");
    printf("\n");
    printf("Redistribution and use in source and binary forms, with or without modification,\n");
    printf("are permitted provided that the following conditions are met:\n");
    printf("\n");
    printf(" * Redistributions of source code must retain the above copyright notice, this\n");
    printf("   list of conditions and the following disclaimer.\n");
    printf("\n");
    printf(" * Redistributions in binary form must reproduce the above copyright notice,\n");
    printf("   this list of conditions and the following disclaimer in the documentation\n");
    printf("   and/or other materials provided with the distribution.\n");
    printf("\n");
    printf(" * Neither the name Facebook, nor Meta, nor the names of its contributors may\n");
    printf("   be used to endorse or promote products derived from this software without\n");
    printf("   specific prior written permission.\n");
    printf("\n");
    printf("THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\" AND\n");
    printf("ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED\n");
    printf("WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE\n");
    printf("DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR\n");
    printf("ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES\n");
    printf("(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;\n");
    printf("LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON\n");
    printf("ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n");
    printf("(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS\n");
    printf("SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n");
}
#endif

static void PrintHelp(void)
{
    printf("\n");
    printf("Game Asset Conditioning Library frontend tool applies RDO and shuffle transforms to texture data.\n");
    printf("\n");
    printf("Usage:  gacl.exe <sourceFile> [options]+\n");
    printf("\n");
    printf("        <sourceFile>        Path to a source DDS (Direct Draw Surface file) containing block\n");
    printf("                            compressed texture data of one of the supported modes BC1-7\n");
    printf("\n");
    printf("Options, general:\n");
    printf("\n");
    printf("  -f, --format <format>     Format of the data, if sourceFile is a raw data stream.\n");
    printf("                            i.e. BC1_UNORM\\BC1_UNORM_SRGB, etc...\n");
    printf("                            Not required if source file is a dds.\n");
    printf("\n");
    printf("  -w, --width <width>       Pixel width of the data, if sourceFile is a raw data stream.\n");
    printf("                            Required for computing the row pitch for tiling\n");
    printf("                            Not required if source file is a dds.\n");
    printf("\n");
    printf("  -o, --output [filename]   Output dds file name.\n");
    printf("                            Should only be used in conjuction with Shuffle+Compress, so that acl\n");
    printf("                            can correctly identify mips\\slices that do not benefit from shuffle,\n");
    printf("                            allowing for the correct (linear vs screen space) application of\n");
    printf("                            entropy reduction.\n");
    printf("\n");
    printf("  -z, --zstdlevel <level>   zstd' compression level.\n");
    printf("                            Default is the minimum setting to enable 3-byte matching:\n");
    printf("                               <= 16KB  : level 12\n");
    printf("                               <= 128KB : level 14\n");
    printf("                                > 256KB : level 18\n");
    printf("\n");
    printf("  -ztbs, --zstdtargetblocksize <size>   \n");
    printf("                            Target compressed block size for zstd compression. DefaultL 8192:\n");
    printf("\n");
    printf("  -v, --verbose             Display more information and statistics.\n");
    printf("  -q, --quiet               Suppress unnecessary messages.\n");
    printf("\n");
    printf("  **  Space curves move screen-adjacent data to memory-adjacent.                              ** \n");
    printf("  **  Shuffle+compress includes a forward space curve to improve compression.                 ** \n");
    printf("  **  If a texture is having entropy reduction applied, the forward space curve is applied    ** \n");
    printf("  **  before bler\\cler, allowing those algorithms to work on screen-adjacent data.  If a     ** \n");
    printf("  **  texture is having Entrory Reduction applied, without Shuffle+Compress, entropy reduction** \n");
    printf("  **  still occurs in the space curved context, as below:  ** \n");
    printf("  **     Input -> Forward Space Curve -> Entropy Reduction -> Reverse Space Curve -> Export   ** \n");
    printf("\n");
    printf("  -fsc, -forwardspacecurve  Forward space curve, no other transforms will be applied.\n");
    printf("  -rsc, -reversespacecurve  Reverse space curve, no other transforms will be applied.\n");
    printf("                            These options can be used to allow the application of external\n");
    printf("                            tooling or transforms to the curved (screen adjacent) data stream.\n");
    printf("\n");
    printf("  -dsc, -disablespacecurve  Disables space the automatic space curve prior to entropy reduction.\n");
    printf("                            Use this option when applying entropy reduction to a texture that\n");
    printf("                            will not use shuffle+compress. Textures that will not have a load-\n");
    printf("                            time unshuffle applied to them cannot have a space curve applied.\n");
    printf("\n");
    printf("\n");
    printf("Options, data shuffle related (supported for BC1\\3\\4\\5):\n");
    printf("\n");
    printf("  -sc, --shufflecompress    Apply Shuffle + zstd compress after any other transform\n");
    printf("                            Note: output will no longer be a valid DDS, and if the input file contains\n");
    printf("                            multiple mips or slices, each will be processed individually.\n");
    printf("                            If an output file name is specififed, it will be used as a base name.\n");
    printf("\n");
    printf("  -sce, --shufflecompressexperimental \n");
    printf("                            As \"shufflecompress\", but enabling experimental shuffle patterns, for\n");
    printf("                            which there is not current DirectStorage transform support, including BC7.\n");
    printf("\n");
    printf("  -sco, --compressonly \n");
    printf("                            As \"shufflecompress\", but disabling all shuffle patterns, effectively\n");
    printf("                            resulting in export streams that are only zstd compressed.\n");
    printf("\n");
    printf("  -e, --export <basename>   Output base name.  Each mip\\slice will be exported as a\n");
    printf("                            Shuffle+Compressed opaque data stream.\n");
    printf("\n");
    printf("  -sno7s,                   Disable BC7 mode Split shuffle (Higher reverse time cost).\n");
    printf("\n");
    printf("  -sno7j,                   Disable BC7 mode Join shuffle (Not Shipping 2025).\n");
    printf("\n");
#if GACL_INCLUDE_CLER
    printf("Options, Component-Level Entropy Reduction related (supported for BC1) [Experimental]:\n");
    printf("\n");
    printf("  -cler                     Enable Component-Level Entropy Reduction.\n");
    printf("\n");
    printf("  -cref <filename>          Reference original art file. (optional)\n");
    printf("                            When provided, RDO errors are computed with reference to the original\n");
    printf("                            art asset, before BC encoding, producing higher fidelity to that orignal.\n");
    printf("                            If not specified, perceptual loss is measured against block compressed\n");
    printf("                            input image\n");
    printf("\n");
    printf("  -cmc, --cmaxclusters <c>  Maximum clusters (optional, default: 0.125 * max(height, width))\n");
    printf("\n");
    printf("  -ci, --citerations <n>    Number of iterations (optional,default: 10)\n");
    printf("\n");
    printf("  -cm, --cmetric <type>     Loss metric type (optional,default: LPIPS)\n");
    printf("                            Options: MSE, LPIPS, VGG, RMSE\n");
    printf("\n");
    printf("  -closs_min <val>          Minimum loss value (optional, default: 0.01)\n");
    printf("\n");
    printf("  -closs_max <val>          Maximum loss value (optional,default: 0.02)\n");
    printf("\n");
    printf("  -cmips <val>              Maximum Mip levels to evaluate (optional,default: 3)\n");
    printf("\n");
#endif
#if GACL_INCLUDE_BLER
    printf("Options, Block-Level Entropy Reduction related (supported for BC1-5 & BC7):\n");
    printf("\n");
    printf("  -bler                     Enable Block-Level Entropy Reduction.\n");
    printf("\n");
    printf("  -breduce <val>            Target unique block element reduction, expressed as percentage. \n");
    printf("                            (optional, default = 25)\n");
    printf("                            i.e.  \"-breduce 40\" implies 40%% reduction in unique blocks.\n");
    printf("                            example: 10000 unique blocks, reduced 40%% down to 6000 unique blocks. \n");
#endif
}

Verbosity verbosity = Verbosity::eDefault;

void LogCallback(GACL_Logging_Priority msgPri, const wchar_t* msg)
{
    if ((msgPri >= GACL_Logging_Priority_Medium && verbosity >= Verbosity::eDefault) ||
        msgPri >= GACL_Logging_Priority_High ||
        verbosity >= Verbosity::eVerbose)
    {
        wprintf(L"%ws", msg);
    }
}

int wmain(size_t argc, const wchar_t* argv[])
{
    GACL_Logging_SetCallback(&LogCallback);

    if (argc == 1)
    {
        PrintVersion();

        return 0;
    }

    wstring command = Utility::ToLower(argv[1]);
    if (command == L"help" || command == L"/?" || command == L"/h" || command == L"-h" || command == L"--help")
    {
        PrintHelp();
        return 0;
    }

#if GACL_INCLUDE_CLER
    if (command == L"license")
    {
        PrintThirdPartyLicenses();
        return 0;
    }
#endif

    if (argv[1][0] == L'-')
    {
        printf("Error: <sourceFile> not specified.\n");
        return -1;
    }

    wstring inputFileName = argv[1];
    wstring referenceFileName;

    size_t pixelWidth = 0;

    wstring outputFileName;
    wstring outputParamFileName;
    wstring exportBaseName;

    DXGI_FORMAT bcInputFormat = DXGI_FORMAT_UNKNOWN;

#if GACL_INCLUDE_CLER
    ComponentLevelEntopyReductionOptions clerOptions = { 
        false,              // CLER enabled
        -1,                 // Max clusters (uses default)
        0,                  // Min clusters
        7,                  // Number of iterations per clustering pass
		LossMetrics::LPIPS, // Loss metric
        0.1f,               // Upper loss bound
        0.05f,              // Lower loss bound
        true,               // Use ++ clustering initialization
        3                   // Number of mips to process
    };
#endif

    BlockLevelEntopyReductionOptions blerOptions = { false, 0.25f };
    ShuffleTransformOptions shuffleOptions = { false, GACL_SHUFFLE_TRANSFORM::GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED };
    SpaceCurveOptions curveOptions = { false, false, false };
    ZstdCompressOptions compressOptions = { 0xff, GACL_ZSTD_TARGET_COMPRESSED_BLOCK_SIZE };
    bool blockBC7Join = false;
    bool blockBC7Split = false;

    for (size_t i = 2; i < argc; )
    {
        wstring option = Utility::ToLower(argv[i++]);

        if (option == L"-o" || option == L"--output")
        {
            // Look for a valid output filename, otherwise just change the extension of the input file to .acl
            if (i < argc && argv[i][0] != L'-' && wstring(argv[i]).find(L".") != wstring::npos)
                outputFileName = argv[i++];
            else
                outputFileName = Utility::RemoveExtension(inputFileName) + L".acl.dds";
        }

        else if (option == L"-f" || option == L"--format")
        {
            bcInputFormat = IdentifyBCEncodeFormat(Utility::WideStringToUTF8(argv[i++]));
        }
        else if (option == L"-w" || option == L"--width" && i < argc)
        {
            pixelWidth = std::stoul(wstring(argv[i++]), 0, 0);
        }

        else if ((option == L"-z" || option == L"--zstdlevel") && i < argc)
        {
            compressOptions.Level = std::min<uint8_t>(22, (uint8_t)std::stoul(wstring(argv[i++]), 0, 0));
        }
        else if ((option == L"-ztbs" || option == L"--zstdtargetblocksize") && i < argc)
        {
            compressOptions.TargetBlockSize = std::max<int>(ZSTD_TARGETCBLOCKSIZE_MIN, std::min<int>(ZSTD_TARGETCBLOCKSIZE_MAX, std::stol(wstring(argv[i++]), 0, 0)));
        }
        else if ((option == L"-v" || option == L"--verbose") && verbosity < Verbosity::eMaximum)
        {
            verbosity = Verbosity(((int)verbosity)+1);
        }
        else if (option == L"-q" || option == L"--quiet" )
        {
            verbosity = Verbosity::eSilent;
        }
        // Shuffle Options
        else if (option == L"-sc" || option == L"--shufflecompress")
        {
            shuffleOptions.Enabled = true;
        }
        else if (option == L"-sce" || option == L"--shufflecompressexperimental")
        {
            shuffleOptions.Enabled = true;
            shuffleOptions.Transform = GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;
        }
        else if (option == L"-sco" || option == L"--compressonly")
        {
            shuffleOptions.Enabled = true;
            shuffleOptions.Transform = GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY;
        }
        else if (option == L"-sno7s")
        {
            blockBC7Split = true;
        }
        else if (option == L"-sno7j")
        {
            blockBC7Join = true;
        }
        else if ((option == L"-e" || option == L"--export"))
        {
            if (i < argc && argv[i][0] != L'-')
                exportBaseName = argv[i++];
            else
            {
                exportBaseName = Utility::RemoveExtension(inputFileName);
            }
        }

#if GACL_INCLUDE_CLER
        // Component Level Entopy Reduction Options
        else if (option == L"-cler")
        {
            clerOptions.Enabled = true;
        }
        else if (option == L"-cref" && i < argc)
        {
            if (i < argc && argv[i][0] != L'-' && wstring(argv[i]).find(L".") != wstring::npos)
                referenceFileName = argv[i++];
            else
            {
                printf("ERROR: cref specified without file name\n");
                return -1;
            }
        }
        else if ((option == L"-cmc" || option == L"--cmaxclusters") && i < argc)
        {
            clerOptions.MaxK = std::stoul(wstring(argv[i++]), 0, 0);
        }
        else if ((option == L"-ci" || option == L"--citerations") && i < argc)
        {
            clerOptions.InitialIterations = std::stoul(wstring(argv[i++]), 0, 0);
        }

        else if (option == L"-closs_min" && i < argc)
        {
            clerOptions.LowerLossBound = std::max<float>(0.0001f, std::stof(wstring(argv[i++]), 0));
        }
        else if (option == L"-closs_max" && i < argc)
        {
            clerOptions.UpperLossBound = std::min<float>(0.5f, std::stof(wstring(argv[i++]), 0));
        }

        else if ((option == L"-cm" || option == L"--cmetric") && i < argc)
        {
            std::wstring metricStr = argv[i++];
            std::transform(metricStr.begin(), metricStr.end(), metricStr.begin(), ::towupper);

            if (metricStr == L"MSE")
            {
                clerOptions.lossMetric = LossMetrics::MSE;
            }
            else if (metricStr == L"LPIPS")
            {
                clerOptions.lossMetric = LossMetrics::LPIPS;
            }
            else if (metricStr == L"VGG")
            {
                clerOptions.lossMetric = LossMetrics::VGG;
            }
            else if (metricStr == L"RMSE")
            {
                clerOptions.lossMetric = LossMetrics::RMSE;
            }
            else
            {
                printf("ERROR: Unknown metric type '%S'. Valid options are MSE, LPIPS, VGG, RMSE\n", metricStr.c_str());
                return -1;
            }
        }
        else if ((option == L"-cmips" && i < argc))
        {
			clerOptions.Mips = std::stoul(wstring(argv[i++]), 0, 0);
        }
#endif
#if GACL_INCLUDE_BLER
        else if (option == L"-bler")
        {
            blerOptions.Enabled = true;
        }
        else if (option == L"-breduce" && i < argc)
        {
            blerOptions.TargetUniqueBlockReduction = std::max<float>(0.0001f, std::min<float>(0.9f, std::stof(wstring(argv[i++]), 0)/100.0f));
        }
#endif
        else if (option == L"-rsc" || option == L"-reversespacecurve")
        {
            curveOptions.ReverseSpaceCurve = true;
            curveOptions.ForwardSpaceCurve = false;
        }
        else if (option == L"-fsc" || option == L"-forwardspacecurve")
        {
            curveOptions.ForwardSpaceCurve = true;
            curveOptions.ReverseSpaceCurve = false;
        }
        else if (option == L"-dsc" || option == L"-disablespacecurve")
        {
            curveOptions.DisableSpaceCurve = true;
        }
    }

    if (verbosity >= Verbosity::eVerbose)
    {
        printf("DEBUG: shuffleOptions.Enabled = %d\n", shuffleOptions.Enabled);
        printf("DEBUG: blerOptions.Enabled = %d\n", blerOptions.Enabled);
#if GACL_INCLUDE_CLER
        printf("DEBUG: clerOptions.Enabled = %d\n", clerOptions.Enabled);
#endif
        printf("DEBUG: curveOptions.ForwardSpaceCurve = %d\n", curveOptions.ForwardSpaceCurve);
        printf("DEBUG: curveOptions.ReverseSpaceCurve = %d\n", curveOptions.ReverseSpaceCurve);
    }

#if GACL_INCLUDE_CLER
    if (!shuffleOptions.Enabled && !blerOptions.Enabled && !clerOptions.Enabled && !curveOptions.ForwardSpaceCurve && !curveOptions.ReverseSpaceCurve)
    {
        printf("Error: no transform specified.\n");
        return -1;
    }
#else
    if (!shuffleOptions.Enabled && !blerOptions.Enabled && !curveOptions.ForwardSpaceCurve && !curveOptions.ReverseSpaceCurve)
    {
        printf("Error: no transform specified.\n");
        return -1;
    }
#endif

    if (blockBC7Join && blockBC7Split)
    {
        shuffleOptions.Transform = GACL_SHUFFLE_TRANSFORM_NONE;
    }
    else if (blockBC7Join)
    {
        shuffleOptions.Transform = GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT;
    }
    else if (blockBC7Split)
    {
        shuffleOptions.Transform = GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN;
    }

#if GACL_INCLUDE_CLER
    ProcessingOptions processingOptions = { shuffleOptions, compressOptions, clerOptions, blerOptions, curveOptions};
#else
    ProcessingOptions processingOptions = { shuffleOptions, compressOptions, blerOptions, curveOptions };
#endif

    if (!ProcessTexture(inputFileName, referenceFileName, outputFileName, exportBaseName, bcInputFormat, pixelWidth, processingOptions, verbosity))
    {
        printf("ERROR: Texture processing failed\n");
        return -1;
    }
    else
    {
        return 0;
    }
}