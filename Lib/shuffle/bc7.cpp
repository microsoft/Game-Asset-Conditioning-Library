//-------------------------------------------------------------------------------------
// bc7.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include "gacl.h"
#include "../BCnBlockDefs.h"
#include "bc7.h"
#include "../helpers/Utility.h"
#include "../helpers/FileUtility.h"
#include "../helpers/FormatHelper.h"

#include "../ThirdParty/zstd/lib/zstd.h"

#include <thread>
#include <fstream>
#include <vector>
#include <cassert>
#include <algorithm>

using namespace std;


void BC7_CollectTextureMetrics(const uint8_t* src, size_t srcSize, BC7TextureMetrics& m)
{
    memset(&m, 0, sizeof(BC7TextureMetrics));
    size_t block_count = srcSize / 16;

    std::vector<uint8_t> localImg(srcSize);
    memcpy(localImg.data(), src, localImg.size());

    uint8_t correlationPairs[][2] = {
        {0, 1},     // Red - Green
        {0, 2},     // Red - Blue
        {1, 2},     // Green - Blue
        {0, 3},     // Red - Alpha
        {1, 3},     // Green - Alpha
        {2, 3},     // Blue - Alpha
    };

    union {
        struct {
            uint64_t Raw[2];
        } X[9];     // represents mode blocks for M0 - M8  (Textures that have been pre-swizzled for a given GPU layout use mode 8 to represent void space between mip data)
    }f = {}, d = {};

    struct EndpointOrdering
    {
        size_t EvenLess[4];
        size_t EvenGreater[4];

        size_t BitsLess[4];
        size_t BitsGreater[4];

        size_t HighValue[4][3][7];
        size_t Delta[4][128];
        size_t Correlation[6];
    };

    struct EndpointCounts {
        size_t EC[8][128] = {};
        size_t OC[8][128] = {};
    };

    struct ModeEndpointData
    {
        EndpointCounts Totals = {};
        EndpointOrdering Order = {};
    };

    std::vector< ModeEndpointData> M(8);

    // pass #1 collect basic stats 
    uint8_t* be = localImg.data();

    for (size_t i = 0; i < block_count; ++i, be += 16)
    {
        // Look for first set bit in [7:0].  If none set, return 8 (invalid).
        unsigned long mode;
        _BitScanForward(&mode, *be | 0x100u);


        auto Collect = [&](uint64_t colorFieldPairs[][2], size_t endpointPairCount, size_t colorFieldCount, size_t colorFieldDepth)
            {
                for (size_t ep = 0; ep < endpointPairCount; ep++)
                {
                    bool evenLessThanOdd[4] = {};
                    bool evenGreaterThanOdd[4] = {};

                    for (size_t c = 0; c < colorFieldCount; c++)
                    {
                        const size_t cfp = ep + endpointPairCount * c;
                        const uint64_t& even = colorFieldPairs[cfp][0];
                        const uint64_t& odd = colorFieldPairs[cfp][1];

                        M[mode].Totals.EC[2 * c][even]++;
                        M[mode].Totals.EC[2 * c + 1][odd]++;

                        if (even < odd)
                        {
                            M[mode].Totals.OC[2 * c][even]++;
                            M[mode].Totals.OC[2 * c + 1][odd]++;

                            uint64_t delta = odd - even;
                            M[mode].Order.EvenLess[c]++;
                            M[mode].Order.Delta[c][delta]++;
                            unsigned long deltaBits;
                            _BitScanReverse64(&deltaBits, delta << 1u);
                            M[mode].Order.BitsLess[c] += deltaBits;
                            evenLessThanOdd[c] = true;
                        }
                        else if (even > odd)
                        {
                            M[mode].Totals.OC[2 * c][odd]++;
                            M[mode].Totals.OC[2 * c + 1][even]++;

                            uint64_t delta = even - odd;
                            M[mode].Order.EvenGreater[c]++;
                            M[mode].Order.Delta[c][delta]++;
                            unsigned long deltaBits;
                            _BitScanReverse64(&deltaBits, delta << 1u);
                            M[mode].Order.BitsGreater[c] += deltaBits;
                            evenGreaterThanOdd[c] = true;
                        }
                        else
                        {
                            M[mode].Totals.OC[2 * c][even]++;
                            M[mode].Totals.OC[2 * c + 1][odd]++;

                            M[mode].Order.Delta[c][0]++;
                        }

                        for (uint32_t b = 0; b < colorFieldDepth; b++)
                        {
                            if ((even >> b) > (odd >> b))
                            {
                                M[mode].Order.HighValue[c][0][b]++;
                            }
                            else if ((even >> b) < (odd >> b))
                            {
                                M[mode].Order.HighValue[c][1][b]++;
                            }
                            else break;
                        }

                    }


                    for (uint32_t cpid = 0; cpid < (colorFieldCount == 3 ? 3u : 6u); cpid++)
                    {
                        bool leftEqual = (false == evenGreaterThanOdd[correlationPairs[cpid][0]] && false == evenLessThanOdd[correlationPairs[cpid][0]]);
                        bool rightEqual = (false == evenGreaterThanOdd[correlationPairs[cpid][1]] && false == evenLessThanOdd[correlationPairs[cpid][1]]);

                        if ((evenGreaterThanOdd[correlationPairs[cpid][0]] == true && evenLessThanOdd[correlationPairs[cpid][1]] != true) ||
                            (evenLessThanOdd[correlationPairs[cpid][0]] == true && evenGreaterThanOdd[correlationPairs[cpid][1]] != true) ||
                            leftEqual || rightEqual)
                        {
                            M[mode].Order.Correlation[cpid]++;
                        }
                    }
                }
            };

        if (mode == 0)
        {
            BC7m0* be0 = reinterpret_cast<BC7m0*>(be);

            uint64_t endpoints[][2] =
            {
                {be0->R0,  be0->R1},
                {be0->R2,  be0->R3},
                {be0->R4,  be0->R5},
                {be0->G0,  be0->G1},
                {be0->G2,  be0->G3},
                {be0->G4,  be0->G5},
                {be0->B0,  be0->B1},
                {be0->B2(),  be0->B3},
                {be0->B4,  be0->B5},
            };

            Collect(endpoints, 3, 3, 4);
        }
        else if (mode == 1)
        {
            BC7m1* be1 = reinterpret_cast<BC7m1*>(be);


            uint64_t endpoints[][2] =
            {
                {be1->R0,  be1->R1},
                {be1->R2,  be1->R3},
                {be1->G0,  be1->G1},
                {be1->G2,  be1->G3},
                {be1->B0,  be1->B1()},
                {be1->B2,  be1->B3},
            };

            Collect(endpoints, 2, 3, 6);
        }
        else if (mode == 2)
        {
            BC7m2* be2 = reinterpret_cast<BC7m2*>(be);

            uint64_t endpoints[][2] =
            {
                {be2->R0,  be2->R1},
                {be2->R2,  be2->R3},
                {be2->R4,  be2->R5},
                {be2->G0,  be2->G1},
                {be2->G2,  be2->G3},
                {be2->G4,  be2->G5},
                {be2->B0,  be2->B1},
                {be2->B2,  be2->B3},
                {be2->B4,  be2->B5},
            };

            Collect(endpoints, 3, 3, 5);
        }
        else if (mode == 3)
        {
            BC7m3* be3 = reinterpret_cast<BC7m3*>(be);

            uint64_t endpoints[][2] =
            {
                {be3->R0,  be3->R1},
                {be3->R2,  be3->R3},
                {be3->G0,  be3->G1},
                {be3->G2,  be3->G3()},
                {be3->B0,  be3->B1},
                {be3->B2,  be3->B3},
            };

            Collect(endpoints, 2, 3, 7);
        }
        else if (mode == 4)
        {
            BC7m4_Derotated* be4 = reinterpret_cast<BC7m4_Derotated*>(be);

            uint8_t swap0, swap1;
            if (be4->Rotation)
            {
                switch (be4->Rotation)
                {
                case 1:
                    swap0 = be4->R0;
                    swap1 = be4->R1;
                    be4->R0 = be4->A0;
                    be4->R1 = be4->A1;
                    break;
                case 2:
                    swap0 = be4->G0;
                    swap1 = be4->G1;
                    be4->G0 = be4->A0;
                    be4->G1 = be4->A1;
                    break;
                default:
                case 3:
                    swap0 = be4->B0;
                    swap1 = be4->B1;
                    be4->B0 = be4->A0;
                    be4->B1 = be4->A1;
                    break;
                }
                be4->A0 = swap0;
                be4->A1 = swap1;
            }

            uint64_t endpoints[4][2] =
            {
                {be4->R0,  be4->R1},
                {be4->G0,  be4->G1},
                {be4->B0,  be4->B1},
                {be4->A0,  be4->A1}
            };

            Collect(endpoints, 1, 4, 5);
        }
        else if (mode == 5)
        {
            BC7m5_Derotated* be5 = reinterpret_cast<BC7m5_Derotated*>(be);

            uint8_t swap0, swap1;
            if (be5->Rotation)
            {
                switch (be5->Rotation)
                {
                case 1:
                    swap0 = be5->R0;
                    swap1 = be5->R1;
                    be5->R0 = be5->A0;
                    be5->R1 = be5->A1();
                    break;
                case 2:
                    swap0 = be5->G0;
                    swap1 = be5->G1;
                    be5->G0 = be5->A0;
                    be5->G1 = be5->A1();
                    break;
                default:
                case 3:
                    swap0 = be5->B0;
                    swap1 = be5->B1;
                    be5->B0 = be5->A0;
                    be5->B1 = be5->A1();
                    break;
                }
                be5->A0 = swap0;
                be5->SetA1(swap1);
            }

            uint64_t endpoints[4][2] =
            {
                {be5->R0, be5->R1},
                {be5->G0, be5->G1},
                {be5->B0, be5->B1},
                {be5->A0, be5->A1()}
            };

            Collect(endpoints, 1, 4, 7);
        }
        else if (mode == 6)
        {
            BC7m6* be6 = reinterpret_cast<BC7m6*>(be);

            uint64_t endpoints[4][2] =
            {
                {be6->R0, be6->R1},
                {be6->G0, be6->G1},
                {be6->B0, be6->B1},
                {be6->A0, be6->A1}
            };


            Collect(endpoints, 1, 4, 7);
        }
        else if (mode == 7)
        {
            BC7m7* be7 = reinterpret_cast<BC7m7*>(be);

            uint64_t endpoints[][2] =
            {
                {be7->R0, be7->R1},
                {be7->R2, be7->R3},

                {be7->G0, be7->G1},
                {be7->G2, be7->G3},

                {be7->B0, be7->B1},
                {be7->B2, be7->B3},

                {be7->A0, be7->A1},
                {be7->A2, be7->A3}
            };

            Collect(endpoints, 2, 4, 5);
        }

        // raw measure of bit entropy across each mode's stream of blocks
        // f = first block data for a given mode
        // d = accumulated bit delta
        if (1 == ++m.ModeCounts[mode])
        {
            f.X[mode].Raw[0] = reinterpret_cast<uint64_t*>(be)[0];
            f.X[mode].Raw[1] = reinterpret_cast<uint64_t*>(be)[1];
        }
        else
        {
            d.X[mode].Raw[0] |= f.X[mode].Raw[0] ^ reinterpret_cast<uint64_t*>(be)[0];
            d.X[mode].Raw[1] |= f.X[mode].Raw[1] ^ reinterpret_cast<uint64_t*>(be)[1];
        }
    }

    // compute statics
    m.Statics = 0xff;
    if (m.ModeCounts[0] > 1)
    {
        BC7m0 M0 = *reinterpret_cast<BC7m0*>(&d.X[0]);
        if (M0.R0 == 0 && M0.R2 == 0 && M0.R4 == 0) m.M[0].Statics |= 0x01;
        if (M0.R1 == 0 && M0.R3 == 0 && M0.R5 == 0) m.M[0].Statics |= 0x02;
        if (M0.G0 == 0 && M0.G2 == 0 && M0.G4 == 0) m.M[0].Statics |= 0x04;
        if (M0.G1 == 0 && M0.G3 == 0 && M0.G5 == 0) m.M[0].Statics |= 0x08;
        if (M0.B0 == 0 && M0.B2() == 0 && M0.B4 == 0) m.M[0].Statics |= 0x10;
        if (M0.B1 == 0 && M0.B3 == 0 && M0.B5 == 0) m.M[0].Statics |= 0x20;

        m.Statics &= (m.M[0].Statics | 0xC0);
    }

    if (m.ModeCounts[1] > 1)
    {
        BC7m1 M1 = *reinterpret_cast<BC7m1*>(&d.X[1]);
        if (M1.R0 == 0 && M1.R2 == 0) m.M[1].Statics |= 0x01;
        if (M1.R1 == 0 && M1.R3 == 0) m.M[1].Statics |= 0x02;
        if (M1.G0 == 0 && M1.G2 == 0) m.M[1].Statics |= 0x04;
        if (M1.G1 == 0 && M1.G3 == 0) m.M[1].Statics |= 0x08;
        if (M1.B0 == 0 && M1.B2 == 0) m.M[1].Statics |= 0x10;
        if (M1.B1() == 0 && M1.B3 == 0) m.M[1].Statics |= 0x20;

        m.Statics &= (m.M[1].Statics | 0xC0);
    }

    if (m.ModeCounts[2] > 1)
    {
        BC7m2 M2 = *reinterpret_cast<BC7m2*>(&d.X[2]);
        if (M2.R0 == 0 && M2.R2 == 0 && M2.R4 == 0) m.M[2].Statics |= 0x01;
        if (M2.R1 == 0 && M2.R3 == 0 && M2.R5 == 0) m.M[2].Statics |= 0x02;
        if (M2.G0 == 0 && M2.G2 == 0 && M2.G4 == 0) m.M[2].Statics |= 0x04;
        if (M2.G1 == 0 && M2.G3 == 0 && M2.G5 == 0) m.M[2].Statics |= 0x08;
        if (M2.B0 == 0 && M2.B2 == 0 && M2.B4 == 0) m.M[2].Statics |= 0x10;
        if (M2.B1 == 0 && M2.B3 == 0 && M2.B5 == 0) m.M[2].Statics |= 0x20;

        m.Statics &= (m.M[2].Statics | 0xC0);

    }

    if (m.ModeCounts[3] > 1)
    {
        BC7m3 M3 = *reinterpret_cast<BC7m3*>(&d.X[3]);
        if (M3.R0 == 0 && M3.R2 == 0) m.M[3].Statics |= 0x01;
        if (M3.R1 == 0 && M3.R3 == 0) m.M[3].Statics |= 0x02;
        if (M3.G0 == 0 && M3.G2 == 0) m.M[3].Statics |= 0x04;
        if (M3.G1 == 0 && M3.G3() == 0) m.M[3].Statics |= 0x08;
        if (M3.B0 == 0 && M3.B2 == 0) m.M[3].Statics |= 0x10;
        if (M3.B1 == 0 && M3.B3 == 0) m.M[3].Statics |= 0x20;

        m.Statics &= (m.M[3].Statics | 0xC0);
    }

    if (m.ModeCounts[4] > 1)
    {
        BC7m4_Derotated M4 = *reinterpret_cast<BC7m4_Derotated*>(&d.X[4]);
        if (M4.R0 == 0) m.M[4].Statics |= 0x01;
        if (M4.R1 == 0) m.M[4].Statics |= 0x02;
        if (M4.G0 == 0) m.M[4].Statics |= 0x04;
        if (M4.G1 == 0) m.M[4].Statics |= 0x08;
        if (M4.B0 == 0) m.M[4].Statics |= 0x10;
        if (M4.B1 == 0) m.M[4].Statics |= 0x20;
        if (M4.A0 == 0) m.M[4].Statics |= 0x40;
        if (M4.A1 == 0) m.M[4].Statics |= 0x80;

        m.Statics &= m.M[4].Statics;
    }
    if (m.ModeCounts[5] > 1)
    {
        BC7m5_Derotated M5 = *reinterpret_cast<BC7m5_Derotated*>(&d.X[5]);
        if (M5.R0 == 0) m.M[5].Statics |= 0x01;
        if (M5.R1 == 0) m.M[5].Statics |= 0x02;
        if (M5.G0 == 0) m.M[5].Statics |= 0x04;
        if (M5.G1 == 0) m.M[5].Statics |= 0x08;
        if (M5.B0 == 0) m.M[5].Statics |= 0x10;
        if (M5.B1 == 0) m.M[5].Statics |= 0x20;
        if (M5.A0 == 0) m.M[5].Statics |= 0x40;
        if (M5.A1() == 0) m.M[5].Statics |= 0x80;

        m.Statics &= m.M[5].Statics;
    }
    if (m.ModeCounts[6] > 1)
    {
        BC7m6 M6 = *reinterpret_cast<BC7m6*>(&d.X[6]);
        if (M6.R0 == 0) m.M[6].Statics |= 0x01;
        if (M6.R1 == 0) m.M[6].Statics |= 0x02;
        if (M6.G0 == 0) m.M[6].Statics |= 0x04;
        if (M6.G1 == 0) m.M[6].Statics |= 0x08;
        if (M6.B0 == 0) m.M[6].Statics |= 0x10;
        if (M6.B1 == 0) m.M[6].Statics |= 0x20;
        if (M6.A0 == 0) m.M[6].Statics |= 0x40;
        if (M6.A1 == 0) m.M[6].Statics |= 0x80;

        m.Statics &= m.M[6].Statics;
    }

    if (m.ModeCounts[7] > 1)
    {
        BC7m7 M7 = *reinterpret_cast<BC7m7*>(&d.X[7]);
        if (M7.R0 == 0 && M7.R2 == 0) m.M[7].Statics |= 0x01;
        if (M7.R1 == 0 && M7.R3 == 0) m.M[7].Statics |= 0x02;
        if (M7.G0 == 0 && M7.G2 == 0) m.M[7].Statics |= 0x04;
        if (M7.G1 == 0 && M7.G3 == 0) m.M[7].Statics |= 0x08;
        if (M7.B0 == 0 && M7.B2 == 0) m.M[7].Statics |= 0x10;
        if (M7.B1 == 0 && M7.B3 == 0) m.M[7].Statics |= 0x20;
        if (M7.A0 == 0 && M7.A2 == 0) m.M[7].Statics |= 0x40;
        if (M7.A1 == 0 && M7.A3 == 0) m.M[7].Statics |= 0x80;

        m.Statics &= m.M[7].Statics;
    }

    // compute low-entropy fields
    uint8_t fieldMaxPerMode[] = { 16, 64, 32, 128, 32, 128, 128, 32 };
    size_t endpointPairsPerMode[] = { 3, 2, 3, 2, 1, 1, 1, 2 };
    m.LowEntropy = uint8_t(~m.Statics);


    uint8_t LowEntropyOrdered[8] = {};

    for (size_t mode = 0; mode < 8; mode++)
    {
        if (m.ModeCounts[mode] > 16)
        {
            const size_t endpointFieldsInMode = mode < 6 ? 6 : 8;
            const size_t endpointPairsInMode = endpointPairsPerMode[mode];
            const uint8_t endpointMaxInMode = fieldMaxPerMode[mode];

            for (size_t e = 0; e < endpointFieldsInMode; e++)
            {
                if (m.M[mode].Statics & ((1 << e)))
                    continue;

                size_t sortedValues[128] = {};
                memcpy(sortedValues, M[mode].Totals.EC[e], sizeof(size_t) * endpointMaxInMode);
                std::sort(sortedValues, sortedValues + endpointMaxInMode, std::greater<size_t>());

                if (sortedValues[0] + sortedValues[1] >= m.ModeCounts[mode] * endpointPairsInMode * 8 / 10)  //  <--  80% threshold in 2 values means low entropy
                {
                    m.M[mode].LowEntropy |= (1 << e);
                }

                size_t sortedOrderedValues[128] = {};
                memcpy(sortedOrderedValues, M[mode].Totals.OC[e], sizeof(size_t) * endpointMaxInMode);
                std::sort(sortedOrderedValues, sortedOrderedValues + endpointMaxInMode, std::greater<size_t>());

                if (sortedOrderedValues[0] + sortedOrderedValues[1] >= m.ModeCounts[mode] * endpointPairsInMode * 8 / 10)  //  <--  80% threshold in 2 values means low entropy
                {
                    LowEntropyOrdered[mode] |= (1 << e);
                }
            }
        }

        m.LowEntropy &= (m.M[mode].LowEntropy | m.M[mode].Statics);
    }

    // note the best bit-spend strategies for endpoint re-ordering.
    for (size_t mode = 0; mode < 8; mode++)
    {
        // clear any correlation fields involving statics or low entropy fields
        for (uint8_t c = 0; c < 8; c++)
        {
            if ((m.M[mode].Statics | m.M[mode].LowEntropy) & (1 << c))
            {
                uint8_t color = c / 2;

                for (uint8_t cpid = 0; cpid < _countof(correlationPairs); cpid++)
                {
                    if (correlationPairs[cpid][0] == color || correlationPairs[cpid][1] == color)
                    {
                        M[mode].Order.Correlation[cpid] = 0;
                    }
                }
            }
        }

        if (m.ModeCounts[mode] > 16)
        {
            // Strategy #1 single bit single highest value channel + any other channel with high correlation
            size_t highestBitDelta = 0;
            uint8_t highestBitDeltaChannel = 0;

            for (uint8_t c = 0; c < 4; c++)
            {
                if (M[mode].Order.BitsGreater[c] > highestBitDelta)
                {
                    highestBitDelta = M[mode].Order.BitsGreater[c];
                    highestBitDeltaChannel = c;
                }
                if (M[mode].Order.BitsLess[c] > highestBitDelta)
                {
                    highestBitDelta = M[mode].Order.BitsGreater[c];
                    highestBitDeltaChannel = c;
                }
            }

            if (highestBitDelta)
            {
                m.M[mode].OrderingStrategies[0].Bits.B0 = (1u << highestBitDeltaChannel);

                // single bit single highest value channel + highest correlating channel
                size_t nearestCorrelationValue = 0;
                uint8_t nearestCorrelationChannel = 0;

                for (uint8_t cpid = 0; cpid < _countof(correlationPairs); cpid++)
                {
                    if (correlationPairs[cpid][0] == highestBitDeltaChannel && M[mode].Order.Correlation[cpid] > nearestCorrelationValue)
                    {
                        nearestCorrelationValue = M[mode].Order.Correlation[cpid];
                        nearestCorrelationChannel = correlationPairs[cpid][1];
                    }
                    if (correlationPairs[cpid][1] == highestBitDeltaChannel && M[mode].Order.Correlation[cpid] > nearestCorrelationValue)
                    {
                        nearestCorrelationValue = M[mode].Order.Correlation[cpid];
                        nearestCorrelationChannel = correlationPairs[cpid][0];
                    }
                }
                if (nearestCorrelationValue > 2 * m.ModeCounts[mode] / 3)
                {
                    m.M[mode].OrderingStrategies[0].Bits.B0 = (1u << highestBitDeltaChannel) | (1u << nearestCorrelationChannel);

                    // single bit single highest value channel + highest 2 correlating channels
                    size_t nextCorrelationValue = 0;
                    uint8_t nextCorrelationChannel = 0;

                    for (uint8_t cpid = 0; cpid < _countof(correlationPairs); cpid++)
                    {
                        if (correlationPairs[cpid][0] == highestBitDeltaChannel && correlationPairs[cpid][1] != nearestCorrelationChannel && M[mode].Order.Correlation[cpid] > nextCorrelationValue)
                        {
                            nextCorrelationValue = M[mode].Order.Correlation[cpid];
                            nextCorrelationChannel = correlationPairs[cpid][1];
                        }
                        if (correlationPairs[cpid][1] == highestBitDeltaChannel && correlationPairs[cpid][0] != nearestCorrelationChannel && M[mode].Order.Correlation[cpid] > nextCorrelationValue)
                        {
                            nextCorrelationValue = M[mode].Order.Correlation[cpid];
                            nextCorrelationChannel = correlationPairs[cpid][0];
                        }
                    }
                    if (nearestCorrelationValue > 2 * m.ModeCounts[mode] / 3)
                    {
                        m.M[mode].OrderingStrategies[0].Bits.B0 = (1u << highestBitDeltaChannel) | (1u << nearestCorrelationChannel) | (1u << nextCorrelationChannel);
                    }
                }
                else
                {
                    // else there is no channel that highly correlates with the one that would benefit most from ordering
                    // try a two bit strategy, with one bit for each of the two channels that have the highest endpoint-swap bit flip entropy

                    size_t secondHighestBitDelta = 0;
                    uint8_t secondHighestBitDeltaChannel = 0;

                    for (uint8_t c = 0; c < 4; c++)
                    {
                        if (M[mode].Order.BitsGreater[c] > secondHighestBitDelta && c != highestBitDeltaChannel)
                        {
                            secondHighestBitDelta = M[mode].Order.BitsGreater[c];
                            secondHighestBitDeltaChannel = c;
                        }
                        if (M[mode].Order.BitsLess[c] > secondHighestBitDelta && c != highestBitDeltaChannel)
                        {
                            secondHighestBitDelta = M[mode].Order.BitsGreater[c];
                            secondHighestBitDeltaChannel = c;
                        }
                    }

                    m.M[mode].OrderingStrategies[1].Bits.B0 = (1u << highestBitDeltaChannel);
                    m.M[mode].OrderingStrategies[1].Bits.B1 = (1u << secondHighestBitDeltaChannel);
                }
            }
        }
    }
}

void BC7_Mode45_ReorderRGBA(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest)
{
    const size_t block_count = srcSize / 16;
    dest.resize(srcSize);
    uint8_t* pDest = dest.data();

    for (size_t i = 0; i < block_count; ++i, src += 16, pDest += 16)
    {
        // Look for first set bit in [7:0].  If none set, return 8 (invalid).
        unsigned long mode;
        _BitScanForward(&mode, *src | 0x100u);




#define REVERSE_ONE_ENDPOINT_PAIR_OF_137 0

#if REVERSE_ONE_ENDPOINT_PAIR_OF_137
        if (mode == 1)
        {
            BC7m1 b = *reinterpret_cast<const BC7m1*>(src);

            uint8_t swap0;

            swap0 = b.R0;
            b.R0 = b.R1;
            b.R1 = swap0;

            swap0 = b.G0;
            b.G0 = b.G1;
            b.G1 = swap0;

            swap0 = b.B0;
            b.B0 = b.B1();
            b.B1_low = swap0;
            b.B1_high = swap0 >> 2;

            *reinterpret_cast<BC7m1*>(pDest) = b;
        }
        else if (mode == 3)
        {
            BC7m3 b = *reinterpret_cast<const BC7m3*>(src);

            uint8_t swap0;

            swap0 = b.R0;
            b.R0 = b.R1;
            b.R1 = swap0;

            swap0 = b.G0;
            b.G0 = b.G1;
            b.G1 = swap0;

            swap0 = b.B0;
            b.B0 = b.B1;
            b.B1 = swap0;

            *reinterpret_cast<BC7m3*>(pDest) = b;
        }
        else if (mode == 7)
        {
            BC7m7 b = *reinterpret_cast<const BC7m7*>(src);

            uint8_t swap0;

            swap0 = b.R0;
            b.R0 = b.R1;
            b.R1 = swap0;

            swap0 = b.G0;
            b.G0 = b.G1;
            b.G1 = swap0;

            swap0 = b.B0;
            b.B0 = b.B1;
            b.B1 = swap0;

            *reinterpret_cast<BC7m7*>(pDest) = b;
        }
        else
#endif
        // rotation bits indicate thus:
        //  case 1: swap(r, a); break;
        //  case 2: swap(g, a); break;
        //  case 3: swap(b, a); break;

        if (mode == 4)
        {
            BC7m4_Derotated drb = *reinterpret_cast<const BC7m4_Derotated*>(src);

            uint8_t swap0, swap1;
            if (drb.Rotation)
            {
                switch (drb.Rotation)
                {
                case 1:
                    swap0 = drb.R0;
                    swap1 = drb.R1;
                    drb.R0 = drb.A0;
                    drb.R1 = drb.A1;
                    break;
                case 2:
                    swap0 = drb.G0;
                    swap1 = drb.G1;
                    drb.G0 = drb.A0;
                    drb.G1 = drb.A1;
                    break;
                default:
                case 3:
                    swap0 = drb.B0;
                    swap1 = drb.B1;
                    drb.B0 = drb.A0;
                    drb.B1 = drb.A1;
                    break;
                }
                drb.A0 = swap0;
                drb.A1 = swap1;
            }
            *reinterpret_cast<BC7m4_Derotated*>(pDest) = drb;
        }
        else if (mode == 5)
        {
            BC7m5_Derotated drb = *reinterpret_cast<const BC7m5_Derotated*>(src);

            uint8_t swap0, swap1;
            if (drb.Rotation)
            {
                switch (drb.Rotation)
                {
                case 1:
                    swap0 = drb.R0;
                    swap1 = drb.R1;
                    drb.R0 = drb.A0;
                    drb.R1 = drb.A1();
                    break;
                case 2:
                    swap0 = drb.G0;
                    swap1 = drb.G1;
                    drb.G0 = drb.A0;
                    drb.G1 = drb.A1();
                    break;
                default:
                case 3:
                    swap0 = drb.B0;
                    swap1 = drb.B1;
                    drb.B0 = drb.A0;
                    drb.B1 = drb.A1();
                    break;
                }
                drb.A0 = swap0;
                drb.SetA1(swap1);
            }
            *reinterpret_cast<BC7m5_Derotated*>(pDest) = drb;
        }
        else
        {
            *reinterpret_cast<__m128i*>(pDest) = *reinterpret_cast<const __m128i*>(src);
        }
    }
}

static void CompressTask(const uint8_t* pSrc, size_t sizeInBytes, std::vector<uint8_t>* pCompressed, SHUFFLE_COMPRESS_PARAMETERS& params, HRESULT& errorOut)
{
    void* cc = nullptr;
    size_t requiredBytes;
    HRESULT cleanup = S_OK, status = GACL_Compression_InitRoutine(&cc, &requiredBytes, &params);

    if (SUCCEEDED(status) && cc != nullptr)
    {
        pCompressed->resize(requiredBytes);
        status = GACL_Compression_CompressRoutine(cc, pCompressed->data(), &requiredBytes, pSrc, sizeInBytes);

        pCompressed->resize(SUCCEEDED(status) ? requiredBytes : 0);

        cleanup = GACL_Compression_CleanupRoutine(cc);
    }

    if (!SUCCEEDED(status))
    {
        errorOut = status;
    }
    else if (!SUCCEEDED(cleanup))
    {
        errorOut = cleanup;
    }
}

static void strat_BC7_NoShuffle(const uint8_t* src, size_t srcSize, SHUFFLE_COMPRESS_PARAMETERS *params, vector<uint8_t>* compressedData, HRESULT *errorOut)
{
    CompressTask(src, srcSize, compressedData, *params, *errorOut);
}

static void strat_BC7_JoinMode(const uint8_t* src, size_t srcSize, BC7ModeJoinShuffleOptions* opt, BC7TextureMetrics* metrics, SHUFFLE_COMPRESS_PARAMETERS* params, vector<uint8_t>* compressedData, HRESULT* errorOut)
{
    std::vector<uint8_t> shuffled, reordered45;
    std::vector<uint8_t> ctrl;

    BC7_Mode45_ReorderRGBA(src, srcSize, reordered45);
#ifdef VALIDATE
    std::vector<uint8_t> ddi;
    BC7_Mode45_ReorderRGBA(reordered45.data(), reordered45.size(), ddi);     // verify reversible
    assert(memcmp(reordered45.data(), ddi.data(), reordered45.size()) == 0);
#endif


    BC7_ModeJoin_Transform(reordered45.data(), reordered45.size(), shuffled, *opt, *metrics, ctrl);

    vector<uint8_t> unshuffled;
    BC7_ModeJoin_Reverse(shuffled.data(), shuffled.size(), unshuffled, *opt, *metrics, ctrl);     // verify reversible

    assert(memcmp(unshuffled.data(), reordered45.data(), unshuffled.size()) == 0);

    CompressTask(shuffled.data(), shuffled.size(), compressedData, *params, *errorOut);
}


static void strat_BC7_JoinModeRot(const uint8_t* src, size_t srcSize, BC7ModeJoinShuffleOptions* opt, BC7TextureMetrics* metrics, SHUFFLE_COMPRESS_PARAMETERS* params, size_t rotationRegionSize, vector<uint8_t>* compressedData, HRESULT* errorOut)
{
    std::vector<uint8_t> shuffled, reordered45, rotated;
    std::vector<uint8_t> ctrl;

    BC7_Mode45_ReorderRGBA(src, srcSize, reordered45);
    BC7_ModeJoin_RotateColors(reordered45.data(), reordered45.size(), rotated, opt->AltEncode, rotationRegionSize);
    BC7_ModeJoin_Transform(rotated.data(), rotated.size(), shuffled, *opt, *metrics, ctrl);

    CompressTask(shuffled.data(), shuffled.size(), compressedData, *params, *errorOut);
}

static void strat_BC7_SplitMode(const uint8_t* src, size_t srcSize, BC7ModeSplitShuffleOptions* opt, BC7TextureMetrics* metrics, SHUFFLE_COMPRESS_PARAMETERS* params, vector<uint8_t>* compressedDataA, vector<uint8_t>* compressedDataB, HRESULT* errorOut)
{
    std::vector<uint8_t> outA, outB;
    thread compressTaskA;

    BC7_ModeSplit_Transform(src, srcSize, outA, outB, *opt, *metrics);

    if ((opt->ModeTransform == BC7ModeSplitModeTransformA || opt->ModeTransform == BC7ModeSplitModeTransformAny) && outA.size() <= srcSize)
    {
        thread t([&] { CompressTask(outA.data(), outA.size(), compressedDataA, *params, *errorOut); });
        compressTaskA.swap(t);
    }
    else
    {
        compressedDataA->resize(0);     // enforce that shuffled size <= original
    }

    if (outB.size() <= srcSize)
    {
        CompressTask(outB.data(), outB.size(), compressedDataB, *params, *errorOut);
#if _DEBUG
        std::vector<uint8_t> unshuffled(srcSize);
        BC7_ModeSplit_Reverse(outB.data(), outB.size(), unshuffled, unshuffled.size(), src);
        assert(memcmp(unshuffled.data(), src, unshuffled.size()) == 0);
#endif
    }
    else
    {
        compressedDataB->resize(0);     // enforce that shuffled size <= original
    }

    if (opt->ModeTransform == BC7ModeSplitModeTransformA || opt->ModeTransform == BC7ModeSplitModeTransformAny && outA.size() <= srcSize)
    {
        compressTaskA.join();
#if _DEBUG
        std::vector<uint8_t> unshuffled(srcSize);
        BC7_ModeSplit_Reverse(outA.data(), outA.size(), unshuffled, unshuffled.size(), src);
        assert(memcmp(unshuffled.data(), src, unshuffled.size()) == 0);
#endif

    }
}

//    LibFuzzer has difficulty handling APIs that go wide on threads, 
//    plus with allocation hooks and ASAN checking, fuzzing goes >1000x 
//    faster if we force it to single threading.

#ifdef SINGLETHREAD 
#define ADDTASK(func, ...) func(__VA_ARGS__)
#else
#define ADDTASK(func, ...) tasks.push_back(thread(func, __VA_ARGS__))
#endif

HRESULT ShuffleCompress_BC7(
    _Out_writes_all_(params.SizeInBytes) uint8_t* dest,
    _Inout_ GACL_SHUFFLE_TRANSFORM* destTransformId,
    _Out_ size_t* destBytesWritten,
    SHUFFLE_COMPRESS_PARAMETERS& params
)
{
    vector<thread> tasks;

    // non-shuffle compressed output vectors
    vector<uint8_t> noShuffle;
    vector<uint8_t> curved;
    *destBytesWritten = 0;
    HRESULT hr = S_OK;


    if (dest == nullptr ||
        destTransformId == nullptr ||
        destBytesWritten == nullptr)
        return E_INVALIDARG;

    const bool bIncludeSplitMode =
        *destTransformId == GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT ||
        *destTransformId == GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT_SC ||
        //destTransformId == GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED ||                  // Experimental transform, temporarily removed from supported list
        *destTransformId == GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;

    const bool bIncludeJoinMode =
        *destTransformId == GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN ||
        *destTransformId == GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN_SC ||
        //destTransformId == GACL_SHUFFLE_TRANSFORM_GROUP_ANY_SUPPORTED ||                  // Experimental transform, temporarily removed from supported list
        *destTransformId == GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL;

    bool encodedStreamIsCurved = false;
    const uint8_t* srcforUncurvedTransforms = params.TextureData;
    const uint8_t* srcForCurvedTransforms = params.CurvedTransforms.TextureData;
    const size_t srcSize = params.SizeInBytes;

    if (srcForCurvedTransforms != nullptr)
    {
        encodedStreamIsCurved = true;
        bool skipCurveApplication = (params.CurvedTransforms.DataIsCurved == true);
        if (!skipCurveApplication)
        {
            curved.resize(srcSize);
            srcForCurvedTransforms = curved.data();
        }
        if (!GACL_Shuffle_ApplySpaceCurve(
            skipCurveApplication ? nullptr : curved.data(),
            skipCurveApplication ? nullptr : params.CurvedTransforms.TextureData,
            srcSize,
            16, 
            params.CurvedTransforms.WidthInPixels,
            true))
        {
            return E_INVALIDARG;
        }

#if _DEBUG
        if (skipCurveApplication == false) 
        {
            std::vector<uint8_t> uncurvedData(srcSize);
            GACL_Shuffle_ApplySpaceCurve(uncurvedData.data(), curved.data(), curved.size(), 16, params.CurvedTransforms.WidthInPixels, false);
            assert(0 == memcmp(params.CurvedTransforms.TextureData, uncurvedData.data(), srcSize));
        }
#endif
    }

    BC7TextureMetrics metrics = {}; // , metrics16K, metrics256K;
    BC7_CollectTextureMetrics((srcForCurvedTransforms ? srcForCurvedTransforms : srcforUncurvedTransforms), srcSize, metrics);
    ADDTASK(strat_BC7_NoShuffle, srcforUncurvedTransforms, srcSize, &params, &noShuffle, &hr);

    bool bIncludeCurvedZstdOnly = nullptr != srcForCurvedTransforms &&
        (*destTransformId == GACL_SHUFFLE_TRANSFORM_ZSTD_SC || *destTransformId == GACL_SHUFFLE_TRANSFORM_GROUP_ANY_EXPERIMENTAL);

    vector<uint8_t> curvedNoShuffle;
    if (bIncludeCurvedZstdOnly)
    {
        ADDTASK(strat_BC7_NoShuffle, srcForCurvedTransforms, srcSize, &params, &curvedNoShuffle, &hr);
    }


    // for all other "real" BC shuffle modes, set the source based on whether a curved version was possible
    const uint8_t* src = encodedStreamIsCurved ? srcForCurvedTransforms : srcforUncurvedTransforms;

    // join-mode compressed output vectors
    vector<uint8_t> joinMode, joinModeNM, joinModeAlt, joinModeRot16, joinModeRot256;
    if (bIncludeJoinMode)    // skip mode-join shuffles if the caller has explicitly requested only mode-split
    {
        BC7TextureMetrics blankMetrics = {};

        BC7ModeJoinShuffleOptions optJoin = { false };
        BC7ModeJoinShuffleOptions optJoinAlt = { true };

        ADDTASK(strat_BC7_JoinMode, src, srcSize, &optJoin, &metrics, &params, &joinMode, &hr);
        ADDTASK(strat_BC7_JoinMode, src, srcSize, &optJoin, &blankMetrics, &params, &joinModeNM, &hr);
        //ADDTASKstrat_BC7_JoinMode, src, srcSize, &optJoinAlt, &metrics, &params, &joinModeAlt, &hr);
        ADDTASK(strat_BC7_JoinModeRot, src, srcSize, &optJoin, &metrics, &params, 16 * 1024, &joinModeRot16, &hr);
        ADDTASK(strat_BC7_JoinModeRot, src, srcSize, &optJoin, &metrics, &params, 256 * 1024, &joinModeRot256, &hr);
    }
    
    struct SplitStrat{
        const wchar_t* Name;
        BC7ModeSplitShuffleOptions Options;
        vector<uint8_t> DestA;
        vector<uint8_t> DestB;
    };
    vector<const SplitStrat*> splitStrategies;


    // supported patterns:                          0   1   2   3   4   5   6   7
    //                          EndpointPair4bit    Y       Y                     
    //                            ColorPlane4bit    Y       Y                     
    //     EndpointPairSignificantBitInderleaved    Y   Y   Y   Y   Y   Y   Y   Y       
    //     EndpointQuadSignificantBitInderleaved        Y       Y               Y  
    //  EndpointQuadSignificantBitInderleavedAlt                                Y
    //                              StableIsland                    Y   Y   Y      
#if GACL_EXPERIMENTAL_SHUFFLE_ENABLE_BC7_SPLIT_MODE_A
    BC7ModeSplitModeTransform mt = BC7ModeSplitModeTransformAny;
#else
    BC7ModeSplitModeTransform mt = BC7ModeSplitModeTransformB;
#endif
    constexpr size_t _64MB = 64ull * 1024 *1024;

    // Strategy #1 - prefer stable island, endpoint quad, then 4 bit endpoint pair
    SplitStrat ss1 = { L"Stable(4,5,6), EQSBI(1,3,7), EP4b(0,2)    ", 
        { mt, { EndpointPair4bit, EndpointQuadSignificantBitInderleaved, EndpointPair4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt }, 0, 0 }
    };
    SplitStrat ss1r = { L"Stable(4,5,6), EQSBI(1,3,7), EP4b(0,2) r  ",
        { mt, { EndpointPair4bit, EndpointQuadSignificantBitInderleaved, EndpointPair4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt }, 0, _64MB }
    };
    SplitStrat ss1o1r = { L"Stable(4,5,6), EQSBI(1,3,7), EP4b(0,2) o1r",
        { mt, { EndpointPair4bit, EndpointQuadSignificantBitInderleaved, EndpointPair4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt }, 1, _64MB }
    };
    SplitStrat ss1o2r = { L"Stable(4,5,6), EQSBI(1,3,7), EP4b(0,2) o2r",
        { mt, { EndpointPair4bit, EndpointQuadSignificantBitInderleaved, EndpointPair4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt }, 2, _64MB }
    };


    // Strategy #2 - prefer stable island, endpoint quad, then 4 bit color plane
    SplitStrat ss2 = { L"Stable(4,5,6), EQSBI(1,3,7), CP4b(0,2)    ",
        { mt, { ColorPlane4bit, EndpointQuadSignificantBitInderleaved, ColorPlane4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt}, 0, 0 }
    };
    SplitStrat ss2o1r = { L"Stable(4,5,6), EQSBI(1,3,7), CP4b(0,2) o1r",
        { mt, { ColorPlane4bit, EndpointQuadSignificantBitInderleaved, ColorPlane4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt}, 1, _64MB }
    };
    SplitStrat ss2o2r = { L"Stable(4,5,6), EQSBI(1,3,7), CP4b(0,2) o2r",
        { mt, { ColorPlane4bit, EndpointQuadSignificantBitInderleaved, ColorPlane4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt}, 2, _64MB }
    };

    // Strategy #3 - prefer endpoint pair interleave 
    SplitStrat ss3 = { L"EPSBI(0,1,2,3,4,5,6,7)    ",
        { mt, { EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved}, 0, 0}
    };
    SplitStrat ss3o1r = { L"EPSBI(0,1,2,3,4,5,6,7) o1r",
        { mt, { EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved}, 1, _64MB}
    };
    SplitStrat ss3o2r = { L"EPSBI(0,1,2,3,4,5,6,7) o2r",
        { mt, { EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved}, 2, _64MB}
    };

    // Strategy #4 - prefer stable island, otherwise endpoint pair
    SplitStrat ss4 = { L"Stable(4,5,6), EPSBI(1,3,7), CP4b(0,2)    ",
        { mt, { ColorPlane4bit, EndpointPairSignificantBitInderleaved, ColorPlane4bit, EndpointPairSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointPairSignificantBitInderleaved}, 0, 0}
    };
    SplitStrat ss4o1r = { L"Stable(4,5,6), EPSBI(1,3,7), CP4b(0,2) o1r",
        { mt, { ColorPlane4bit, EndpointPairSignificantBitInderleaved, ColorPlane4bit, EndpointPairSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointPairSignificantBitInderleaved}, 1, _64MB}
    };
    SplitStrat ss4o2r = { L"Stable(4,5,6), EPSBI(1,3,7), CP4b(0,2) o2r",
        { mt, { ColorPlane4bit, EndpointPairSignificantBitInderleaved, ColorPlane4bit, EndpointPairSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointPairSignificantBitInderleaved}, 2, _64MB}
    };

    // Strategy #5 - similar to #3, but placing the smaller stable region of mode 7 quads at a different byte edge
    SplitStrat ss5 = { L"Stable(4,5,6), EQSBI(1,3), EQSBIA(7), CP4b(0,2)  ",
        { mt, { ColorPlane4bit, EndpointQuadSignificantBitInderleaved, ColorPlane4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt}, 0, 0 }
    };
    SplitStrat ss5r = { L"Stable(4,5,6), EQSBI(1,3), EQSBIA(7), CP4b(0,2) r",
        { mt, { ColorPlane4bit, EndpointQuadSignificantBitInderleaved, ColorPlane4bit, EndpointQuadSignificantBitInderleaved, StableIsland, StableIsland, StableIsland, EndpointQuadSignificantBitInderleavedAlt}, 0, _64MB }
    };

    // Strategy #7 - prefer endpoint pair interleave or 4 bit
    SplitStrat ss7 = { L"EPSBI(1,3,4,5,6,7), CP4b(0,2)    ",
        { mt, { ColorPlane4bit, EndpointPairSignificantBitInderleaved, ColorPlane4bit, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved}, 0, 0}
    };
    SplitStrat ss7o1r = { L"EPSBI(1,3,4,5,6,7), CP4b(0,2) o1r",
        { mt, { ColorPlane4bit, EndpointPairSignificantBitInderleaved, ColorPlane4bit, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved}, 1, _64MB}
    };
    SplitStrat ss7o2r = { L"EPSBI(1,3,4,5,6,7), CP4b(0,2) o2r",
        { mt, { ColorPlane4bit, EndpointPairSignificantBitInderleaved, ColorPlane4bit, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved, EndpointPairSignificantBitInderleaved}, 2, _64MB}
    };


    if (bIncludeSplitMode)  // skip mode-split shuffles if the caller has explicitly requested only mode-join
    {
        // short circuit to just single Endpoint pair modes for now...
        uint32_t maxOrderStrategies = std::max<uint32_t>({
            0, //metrics.M[0].OrderingStrategies->Count(), 
            0, //metrics.M[1].OrderingStrategies->Count(),
            0, //metrics.M[2].OrderingStrategies->Count(),
            0, //metrics.M[3].OrderingStrategies->Count(),
            metrics.M[4].OrderingStrategies[1].BitsUsed() ? 2u : (metrics.M[4].OrderingStrategies[0].BitsUsed() ? 1u : 0u),
            metrics.M[5].OrderingStrategies[1].BitsUsed() ? 2u : (metrics.M[5].OrderingStrategies[0].BitsUsed() ? 1u : 0u),
            metrics.M[6].OrderingStrategies[1].BitsUsed() ? 2u : (metrics.M[6].OrderingStrategies[0].BitsUsed() ? 1u : 0u),
            0, //metrics.M[7].OrderingStrategies->Count(),
        });

        splitStrategies.push_back(&ss1);
        ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss1.Options, &metrics, &params, &ss1.DestA, &ss1.DestB, &hr);
        splitStrategies.push_back(&ss1r);
        ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss1r.Options, &metrics, &params, &ss1r.DestA, &ss1r.DestB, &hr);

        if (maxOrderStrategies > 0)
        {
            splitStrategies.push_back(&ss1o1r);
            ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss1o1r.Options, &metrics, &params, &ss1o1r.DestA, &ss1o1r.DestB, &hr);
            if (maxOrderStrategies > 1)
            {
                splitStrategies.push_back(&ss1o2r);
                ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss1o2r.Options, &metrics, &params, &ss1o2r.DestA, &ss1o2r.DestB, &hr);
            }
        }

        splitStrategies.push_back(&ss2);
        ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss2.Options, &metrics, &params, &ss2.DestA, &ss2.DestB, &hr);
        if (maxOrderStrategies > 0)
        {
            splitStrategies.push_back(&ss2o1r);
            ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss2o1r.Options, &metrics, &params, &ss2o1r.DestA, &ss2o1r.DestB, &hr);
            if (maxOrderStrategies > 1)
            {
                splitStrategies.push_back(&ss2o2r);
                ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss2o2r.Options, &metrics, &params, &ss2o2r.DestA, &ss2o2r.DestB, &hr);
            }
        }

        splitStrategies.push_back(&ss3);
        ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss3.Options, &metrics, &params, &ss3.DestA, &ss3.DestB, &hr);
        if (maxOrderStrategies > 0)
        {
            splitStrategies.push_back(&ss3o1r);
            ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss3o1r.Options, &metrics, &params, &ss3o1r.DestA, &ss3o1r.DestB, &hr);
            if (maxOrderStrategies > 1)
            {
                splitStrategies.push_back(&ss3o2r);
                ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss3o2r.Options, &metrics, &params, &ss3o2r.DestA, &ss3o2r.DestB, &hr);
            }
        }

        splitStrategies.push_back(&ss7);
        ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss7.Options, &metrics, &params, &ss7.DestA, &ss7.DestB, &hr);
        if (maxOrderStrategies > 0)
        {
            splitStrategies.push_back(&ss7o1r);
            ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss7o1r.Options, &metrics, &params, &ss7o1r.DestA, &ss7o1r.DestB, &hr);
            if (maxOrderStrategies > 1)
            {
                splitStrategies.push_back(&ss7o2r);
                ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss7o2r.Options, &metrics, &params, &ss7o2r.DestA, &ss7o2r.DestB, &hr);
            }
        }

        splitStrategies.push_back(&ss4);
        ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss4.Options, &metrics, &params, &ss4.DestA, &ss4.DestB, &hr);
        if (maxOrderStrategies > 0)
        {
            splitStrategies.push_back(&ss4o1r);
            ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss4o1r.Options, &metrics, &params, &ss4o1r.DestA, &ss4o1r.DestB, &hr);
            if (maxOrderStrategies > 1)
            {
                splitStrategies.push_back(&ss4o2r);
                ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss4o2r.Options, &metrics, &params, &ss4o2r.DestA, &ss4o2r.DestB, &hr);
            }
        }

        splitStrategies.push_back(&ss5);
        ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss5.Options, &metrics, &params, &ss5.DestA, &ss5.DestB, &hr);
        splitStrategies.push_back(&ss5r);
        ADDTASK(strat_BC7_SplitMode, src, srcSize, &ss5r.Options, &metrics, &params, &ss5r.DestA, &ss5r.DestB, &hr);

    }

    for (auto& task : tasks) task.join();

    if (!SUCCEEDED(hr))
    {
        Utility::Printf(GACL_Logging_Priority_High, L"Fatal shuffle+compress error.\n");
        *destTransformId = GACL_SHUFFLE_TRANSFORM_NONE;
        return hr;
    }

    size_t bestSize = srcSize;
    const uint8_t* best = src;
    for (auto b : { &noShuffle, &curvedNoShuffle, &joinMode, &joinModeNM, &joinModeAlt, &joinModeRot16, &joinModeRot256 })
    {
        if (b->size() < bestSize && b->size() > 0)
        {
            best = b->data();
            bestSize = b->size();
        }
    }
    for (auto b : splitStrategies)
    {
        if (b->DestA.size() < bestSize && b->DestA.size() > 0)
        {
            best = b->DestA.data();
            bestSize = b->DestA.size();
        }
        if (b->DestB.size() < bestSize && b->DestB.size() > 0)
        {
            best = b->DestB.data();
            bestSize = b->DestB.size();
        }
    }

    wchar_t compressionStr[20] = L"(custom)";

    if (GACL_Compression_InitRoutine == &GACL_Compression_DefaultInitRoutine &&
        GACL_Compression_CompressRoutine == &GACL_Compression_DefaultCompressRoutine &&
        GACL_Compression_CleanupRoutine == &GACL_Compression_DefaultCleanupRoutine)
    {
        swprintf_s(compressionStr, _countof(compressionStr), L"(zstd-%d)", params.CompressSettings.Default.ZstdCompressionLevel);
    }

    Utility::Printf(L"Original BC7 texture (no compression)                         %u%c  \n", srcSize, (best == src ? L'*' : L' '));
    Utility::Printf(L"No Shuffle [linear]  %s                                %u%c  \n", compressionStr, noShuffle.size(), (best == noShuffle.data() ? L'*' : L' '));

    if (bIncludeCurvedZstdOnly)
    {
        Utility::Printf(L"No Shuffle [curved]  %s                                %u%c  \n", compressionStr, curvedNoShuffle.size(), (bestSize > curvedNoShuffle.size() ? L'!' : L' '));
    }
    if (bIncludeJoinMode)
    {
        Utility::Printf(L"Join..........................................................%u%c  \n", joinMode.size(), (best == joinMode.data() ? L'*' : L' '));
        Utility::Printf(L"Join (no metrics)                                             %u%c  \n", joinModeNM.size(), (best == joinModeNM.data() ? L'*' : L' '));
        Utility::Printf(L"Join (Alt)                                                    %u%c  \n", joinModeAlt.size(), (best == joinModeAlt.data() ? L'*' : L' '));
        Utility::Printf(L"Join (4K rot)                                                 %u%c  \n", joinModeRot16.size(), (best == joinModeRot16.data() ? L'*' : L' '));
        Utility::Printf(L"Join (256K rot)                                               %u%c  \n", joinModeRot256.size(), (best == joinModeRot256.data() ? L'*' : L' '));
    }
    if (bIncludeSplitMode)
    {
        for (auto b : splitStrategies)
        {
            if (b->DestA.size())
            {
                Utility::Printf(L"Split %51ws A   %u%c  \n", b->Name, b->DestA.size(), (best == b->DestA.data() ? L'*' : L' '));
            }
            if (b->DestB.size())
            {
                Utility::Printf(L"Split %51ws B   %u%c  \n", b->Name, b->DestB.size(), (best == b->DestB.data() ? L'*' : L' '));
            }
        }
    }

    if (best == src)
    {
        Utility::Printf(L"shuffle\\compress produced no data size savings.\n");
        *destTransformId = GACL_SHUFFLE_TRANSFORM_NONE;
        return S_FALSE;
    }
    else
    {
        memcpy_s(dest, srcSize, best, bestSize);
        *destBytesWritten = bestSize;

        if (best == noShuffle.data())
        {
            *destTransformId = GACL_SHUFFLE_TRANSFORM_ZSTD_ONLY;
        }
        else if (best == curvedNoShuffle.data())
        { 
            *destTransformId = GACL_SHUFFLE_TRANSFORM_ZSTD_SC;
        }
        else if (best == joinMode.data() ||
            best == joinModeNM.data() ||
            best == joinModeAlt.data() ||
            best == joinModeRot16.data() ||
            best == joinModeRot256.data())
        {
            *destTransformId = encodedStreamIsCurved ? GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN_SC : GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_JOIN;
        }
        else
        {
            *destTransformId = encodedStreamIsCurved ? GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT_SC : GACL_SHUFFLE_TRANSFORM_ZSTD_BC7_SPLIT;
        }
    }
    return S_OK;
}



