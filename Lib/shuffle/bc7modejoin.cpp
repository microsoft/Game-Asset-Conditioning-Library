//-------------------------------------------------------------------------------------
// bc7modejoin.cpp
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

#include <chrono>
#include <cassert>
#include <algorithm>
#include <windows.h>

namespace 
{

struct InplaceShiftBitsOrder
{
    uint8_t DestinationBitOffset;
    uint8_t CopyBitCount;
};

struct InplaceShiftBitsSequence
{
    size_t OpCount;
    InplaceShiftBitsOrder Ops[120];
};

const uint64_t maskTable[17][2] = {
    {0b0000000000000000000000000000000000000000000000000000000000000000, // 0 non-static bits
     0b0000000000000000000000000000000000000000000000000000000000000000},

    {0b1111111111111111111111111111111111111111111111111111111111111111, // 1 non-static bit
     0b1111111111111111111111111111111111111111111111111111111111111111},

    {0b0101010101010101010101010101010101010101010101010101010101010101, // 2 non-static bits
     0b0101010101010101010101010101010101010101010101010101010101010101},

    {0b1001001001001001001001001001001001001001001001001001001001001001, // 3 non-static bits
     0b0100100100100100100100100100100100100100100100100100100100100100},

    {0b0001000100010001000100010001000100010001000100010001000100010001, // 4 non-static bits
     0b0001000100010001000100010001000100010001000100010001000100010001},
    
    {0b0001000010000100001000010000100001000010000100001000010000100001, // 5 non-static bits
     0b0010000100001000010000100001000010000100001000010000100001000010},

    {0b0001000001000001000001000001000001000001000001000001000001000001, // 6 non-static bits
     0b0100000100000100000100000100000100000100000100000100000100000100},
    
    {0b1000000100000010000001000000100000010000001000000100000010000001, // 7 non-static bits
     0b0100000010000001000000100000010000001000000100000010000001000000},

    {0b0000000100000001000000010000000100000001000000010000000100000001, // 8 non-static bits
     0b0000000100000001000000010000000100000001000000010000000100000001},

    {0b1000000001000000001000000001000000001000000001000000001000000001, // 9 non-static bits
     0b0100000000100000000100000000100000000100000000100000000100000000},

    {0b0001000000000100000000010000000001000000000100000000010000000001, // 10 non-static bits
     0b0000000100000000010000000001000000000100000000010000000001000000},

    {0b0000000010000000000100000000001000000000010000000000100000000001, // 11 non-static bits
     0b0000001000000000010000000000100000000001000000000010000000000100},

    {0b0001000000000001000000000001000000000001000000000001000000000001,  // 12 non-static bits
     0b0000000100000000000100000000000100000000000100000000000100000000},

    {0b0000000000010000000000001000000000000100000000000010000000000001, // 13 non-static bits
	 0b0000000000001000000000010000000000001000000000000100000000000010}, 

    {0b0000000100000000000001000000000000010000000000000100000000000001, // 14 non-static bits
	 0b0100000000000001000000000000010000000000000100000000000001000000},

    {0b0001000000000000001000000000000001000000000000001000000000000001, // 15 non-static bits
	 0b0000000100000000000000100000000000000100000000000000100000000000},

    {0b0000000000000001000000000000000100000000000000010000000000000001, // 16 non-static bits
     0b0000000000000001000000000000000100000000000000010000000000000001}
};


void Get_BC7_ModeJoin_CopyBitsOrderMode0(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics)
{
    UNREFERENCED_PARAMETER(opt);
    memset(&sequence, 0, sizeof(InplaceShiftBitsSequence));

    // all "join" modes are 16 bytes "color" data, but with a segmentation of 12B + 4B
    // Most color data is encoded within that 12B block, and always as endpoint pairs
    // stacking bits from least to most significant, ending in a byte boundary.

    //  Bit Offset  Bits or ordering and count
    //  0           1 bit Mode (1)
    //  1           4 bits Partition
    //  5           4 bits R0   4 bits R1   4 bits R2   4 bits R3   4 bits R4   4 bits R5
    //  29          4 bits G0   4 bits G1   4 bits G2   4 bits G3   4 bits G4   4 bits G5
    //  53          4 bits B0   4 bits B1   4 bits B2   4 bits B3   4 bits B4   4 bits B5
    //  77          1 bit P0    1 bit P1    1 bit P2    1 bit P3    1 bit P4    1 bit P5
    //  83          45 bits Index


    if (metrics.M[0].Statics)
    {
        // group static bits before byte 12, interleave others

        //const size_t staticFields = 3 * _mm_popcnt_u64(metrics.M[0].Statics);
        //const size_t nonStaticFields = 18 - staticFields;
        uint8_t nextStaticBit = 96;

        uint8_t bitSpread = 18;
        enum {
            R0 = 24,
            G0 = 25,
            B0 = 26,

            R1 = 27,
            G1 = 28,
            B1 = 29,

            R2 = 30,
            G2 = 31,
            B2 = 32,

            R3 = 33,
            G3 = 34,
            B3 = 35,

            R4 = 36,
            G4 = 37,
            B4 = 38,

            R5 = 39,
            G5 = 40,
            B5 = 41,
        };

        uint8_t fb[] = {    // first bit
            R0, R1, R2, R3, R4, R5,
            G0, G1, G2, G3, G4, G5,
            B0, B1, B2, B3, B4, B5,
        };

        for (size_t cc = 0; cc < 6; cc++)  // 
        {
            if (metrics.M[0].Statics & (1 << cc))    // static bits cover color channel 6/8 bits depending on RGB[A], multiple endpoint pairs mean both Cx\Cx+2 are static
            {
                // if channel is static (R0+R2, R1+R3, G0+G2, G1+G3, B0+B2, B1+B3;
                size_t e0c = (cc / 2) * 6 + (cc % 2);
                size_t e1c = (cc / 2) * 6 + (cc % 2) + 2;
                size_t e2c = (cc / 2) * 6 + (cc % 2) + 4;
                for (size_t e : {e0c, e1c, e2c})
                {
                    for (int i = 0; i < _countof(fb); i++)
                    {
                        if (fb[i] > fb[e])
                        {
                            fb[i]--;
                        }
                    }
                    nextStaticBit -= 4;
                    fb[e] = 0;
                }
                bitSpread -= 3;
            }
        }


        InplaceShiftBitsOrder ops[80] = {
            {   // Mode (1)
                96,
                1
            },

            {   // Partition bits
                97,
                4
            },
        };

        size_t opCount = 2;

        for (size_t epc = 0; epc < 18; epc++)  // 18 total endpoint pair color channels
        {
            size_t c = (epc % 2) + 2 * (epc / 6);
            if (metrics.M[0].Statics & (1 << c))
            {
                ops[opCount].CopyBitCount = 4;
                ops[opCount].DestinationBitOffset = nextStaticBit;
                opCount++;
                nextStaticBit += 4;
            }
            else
            {
                for (uint8_t b = 0; b < 4; b++)
                {
                    ops[opCount].CopyBitCount = 1;
                    ops[opCount].DestinationBitOffset = uint8_t(fb[epc] + bitSpread * b);
                    opCount++;
                }
            }
        }

        // P bits + index to fill out the 4-Byte tail with 5 bits 
        // 27 bits needed, starting at 96+5 = 101
        ops[opCount].CopyBitCount = 27;
        ops[opCount].DestinationBitOffset = 101;
        opCount++;

        ops[opCount].CopyBitCount = 24;
        ops[opCount].DestinationBitOffset = 0;
        opCount++;

        sequence.OpCount = opCount;
        memcpy(sequence.Ops, &ops, opCount * sizeof(InplaceShiftBitsOrder));
        return;
    }
    else
    {
        //   ---------------------------------------------------   3 bytes ------------------------------------------------------
        //  24   25   26   27   28   29   30   31   32   33   34   35   36   37   38   39   40   41   42   43   44   45   46   47
        // R0'0 G0'0 B0'0 R1'0 G1'0 B1'0 R0'1 G0'1 B0'1 R1'1 G1'1 B1'1 R0'2 G0'2 B0'2 R1'2 G1'2 B1'2 R0'3 G0'3 B0'3 R1'3 G1'3 B1'3 

        //   ---------------------------------------------------   3 bytes ------------------------------------------------------
        //  48   49   50   51   52   53   54   55   56   57   58   59   60   61   62   63   64   65   66   67   68   69   70   71
        // R2'0 G2'0 B2'0 R3'0 G3'0 B3'0 R2'1 G2'1 B2'1 R3'1 G3'1 B3'1 R2'2 G2'2 B2'2 R3'2 G3'2 B3'2 R2'3 G2'3 B2'3 R3'3 G3'3 B3'3 

        //   ---------------------------------------------------   3 bytes ------------------------------------------------------
        //  72   73   74   75   76   77   78   79   80   81   82   83   84   85   86   87   88   89   90   91   92   93   94   95
        // R4'0 G4'0 B4'0 R5'0 G5'0 B5'0 R4'1 G4'1 B4'1 R5'1 G5'1 B5'1 R4'2 G4'2 B4'2 R5'2 G5'2 B5'2 R4'3 G4'3 B4'3 R5'3 G5'3 B5'3 


        enum {
            R0 = 24,
            G0 = 25,
            B0 = 26,
            R1 = 27,
            G1 = 28,
            B1 = 29,

            R2 = 48,
            G2 = 49,
            B2 = 50,
            R3 = 51,
            G3 = 52,
            B3 = 53,

            R4 = 72,
            G4 = 73,
            B4 = 74,
            R5 = 75,
            G5 = 76,
            B5 = 77,

        };

        static const InplaceShiftBitsOrder ops[] = {
            {   // Mode (1)
                96,
                1
            },

            {   // Partition bits
                97,
                4
            },

            {   R0 + 6 * 0, 1 },
            {   R0 + 6 * 1, 1 },
            {   R0 + 6 * 2, 1 },
            {   R0 + 6 * 3, 1 },

            {   R1 + 6 * 0, 1 },
            {   R1 + 6 * 1, 1 },
            {   R1 + 6 * 2, 1 },
            {   R1 + 6 * 3, 1 },

            {   R2 + 6 * 0, 1 },
            {   R2 + 6 * 1, 1 },
            {   R2 + 6 * 2, 1 },
            {   R2 + 6 * 3, 1 },

            {   R3 + 6 * 0, 1 },
            {   R3 + 6 * 1, 1 },
            {   R3 + 6 * 2, 1 },
            {   R3 + 6 * 3, 1 },

            {   R4 + 6 * 0, 1 },
            {   R4 + 6 * 1, 1 },
            {   R4 + 6 * 2, 1 },
            {   R4 + 6 * 3, 1 },

            {   R5 + 6 * 0, 1 },
            {   R5 + 6 * 1, 1 },
            {   R5 + 6 * 2, 1 },
            {   R5 + 6 * 3, 1 },


            {   G0 + 6 * 0, 1 },
            {   G0 + 6 * 1, 1 },
            {   G0 + 6 * 2, 1 },
            {   G0 + 6 * 3, 1 },

            {   G1 + 6 * 0, 1 },
            {   G1 + 6 * 1, 1 },
            {   G1 + 6 * 2, 1 },
            {   G1 + 6 * 3, 1 },

            {   G2 + 6 * 0, 1 },
            {   G2 + 6 * 1, 1 },
            {   G2 + 6 * 2, 1 },
            {   G2 + 6 * 3, 1 },

            {   G3 + 6 * 0, 1 },
            {   G3 + 6 * 1, 1 },
            {   G3 + 6 * 2, 1 },
            {   G3 + 6 * 3, 1 },

            {   G4 + 6 * 0, 1 },
            {   G4 + 6 * 1, 1 },
            {   G4 + 6 * 2, 1 },
            {   G4 + 6 * 3, 1 },

            {   G5 + 6 * 0, 1 },
            {   G5 + 6 * 1, 1 },
            {   G5 + 6 * 2, 1 },
            {   G5 + 6 * 3, 1 },

            {   B0 + 6 * 0, 1 },
            {   B0 + 6 * 1, 1 },
            {   B0 + 6 * 2, 1 },
            {   B0 + 6 * 3, 1 },

            {   B1 + 6 * 0, 1 },
            {   B1 + 6 * 1, 1 },
            {   B1 + 6 * 2, 1 },
            {   B1 + 6 * 3, 1 },

            {   B2 + 6 * 0, 1 },
            {   B2 + 6 * 1, 1 },
            {   B2 + 6 * 2, 1 },
            {   B2 + 6 * 3, 1 },

            {   B3 + 6 * 0, 1 },
            {   B3 + 6 * 1, 1 },
            {   B3 + 6 * 2, 1 },
            {   B3 + 6 * 3, 1 },

            {   B4 + 6 * 0, 1 },
            {   B4 + 6 * 1, 1 },
            {   B4 + 6 * 2, 1 },
            {   B4 + 6 * 3, 1 },

            {   B5 + 6 * 0, 1 },
            {   B5 + 6 * 1, 1 },
            {   B5 + 6 * 2, 1 },
            {   B5 + 6 * 3, 1 },

            {   // P bits + index to fill out the 4-Byte tail with 5 bits 
                // 27 bits needed, starting at 96+5 = 101
                101,
                27
            },


            {   // remaiing index bits fill in the first 3 bytes 
                0,
                24
            },
        };

        sequence.OpCount = ARRAYSIZE(ops);
        memcpy(sequence.Ops, &ops, sizeof(ops));
        return;
    }
}

void Get_BC7_ModeJoin_CopyBitsOrderMode1(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics)
{
    UNREFERENCED_PARAMETER(opt);
    // all "join" modes are 16 bytes "color" data, but with a segmentation of 12B + 4B
    // Most color data is encoded within that 12B block, and always as endpoint pairs
    // stacking bits from least to most significant, ending in a byte boundary.

    //  Bit Offset  Bits or ordering and count
    //  0           2 bits Mode (10)
    //  2           6 bits Partition
    //  8           6 bits R0   6 bits R1   6 bits R2   6 bits R3
    //  32          6 bits G0   6 bits G1   6 bits G2   6 bits G3
    //  56          6 bits B0   6 bits B1   6 bits B2   6 bits B3
    //  80          1 bit P0    1 bit P1
    //  82          46 bits Index

    if (metrics.M[1].Statics)
    {
        // group static bits before byte 12, interleave others

        //const size_t staticFields = 2 * _mm_popcnt_u64(metrics.M[1].Statics);
        //const size_t nonStaticFields = 12 - staticFields;
        uint8_t nextStaticBit = 96;

        uint8_t bitSpread = 12;
        enum {
            R0 = 24,
            G0 = 25,
            B0 = 26,

            R1 = 27,
            G1 = 28,
            B1 = 29,

            R2 = 30,
            G2 = 31,
            B2 = 32,

            R3 = 33,
            G3 = 34,
            B3 = 35,
        };

        uint8_t fb[] = {    // first bit
            R0, R1, R2, R3,
            G0, G1, G2, G3,
            B0, B1, B2, B3,
        };

        for (size_t cc = 0; cc < 6; cc++)  // 
        {
            if (metrics.M[1].Statics & (1 << cc))    // static bits cover color channel 6/8 bits depending on RGB[A], multiple endpoint pairs mean both Cx\Cx+2 are static
            {
                // if channel is static (R0+R2, R1+R3, G0+G2, G1+G3, B0+B2, B1+B3;
                size_t e0c = (cc / 2) * 4 + (cc % 2);
                size_t e1c = (cc / 2) * 4 + (cc % 2) + 2;
                for (size_t e : {e0c, e1c})
                {
                    for (int i = 0; i < _countof(fb); i++)
                    {
                        if (fb[i] > fb[e])
                        {
                            fb[i]--;
                        }
                    }
                    nextStaticBit -= 6;
                    fb[e] = 0;
                }
                bitSpread -= 2;
            }
        }


        InplaceShiftBitsOrder ops[80] = {
            {   // Mode (01)
                96,
                2
            },

            {   // Partition bits
                98,
                6
            },
        };

        size_t opCount = 2;

        for (size_t epc = 0; epc < 12; epc++)  // 12 total endpoint pair color channels
        {
            size_t c = (epc % 2) + 2 * (epc / 4);
            if (metrics.M[1].Statics & (1 << c))
            {
                ops[opCount].CopyBitCount = 6;
                ops[opCount].DestinationBitOffset = nextStaticBit;
                opCount++;
                nextStaticBit += 6;
            }
            else
            {
                for (uint8_t b = 0; b < 6; b++)
                {
                    ops[opCount].CopyBitCount = 1;
                    ops[opCount].DestinationBitOffset = uint8_t(fb[epc] + bitSpread * b);
                    opCount++;
                }
            }
        }

        // P bits + index to fill out the 4-Byte tail with 8 bits 
        // 24 bits needed, starting at 96+8 = 104
        ops[opCount].CopyBitCount = 24;
        ops[opCount].DestinationBitOffset = 104;
        opCount++;

        ops[opCount].CopyBitCount = 24;
        ops[opCount].DestinationBitOffset = 0;
        opCount++;

        sequence.OpCount = opCount;
        memcpy(sequence.Ops, &ops, opCount * sizeof(InplaceShiftBitsOrder));
        return;
    }
    else
    {
        // Mode 1 contains 2 endpoint pairs each with 36 bits
        // E0/1 ends at byte 12, E2/E3 ends at byte 6, so there are 12 extra bits before each:

        //                  ------------------------------------------------------------------------   4 bytes + 4 bits -------------------------------------------------------------------------------------  
        //  56  57  58  59   60   61   62   63   64   65 - 66   67   68   69   70   71 - 72   73   74   76   76   77 - 78   79   80   81   82   83 - 84   85   86   87   88   89 - 90   91   92   93   94   95  
        //   ?   ?   ?   ?  R0'0 G0'0 B0'0 R1'0 G1'0 B1'0 R0'1 G0'1 B0'1 R1'1 G1'1 B1'1 R0'2 G0'2 B0'2 R1'2 G1'2 B1'2 R0'3 G0'3 B0'3 R1'3 G1'3 B1'3 R0'4 G0'4 B0'4 R1'4 G1'4 B1'4 R0'5 G0'5 B0'5 R1'5 G1'5 B1'5 

        //                  ------------------------------------------------------------------------   4 bytes + 4 bits -------------------------------------------------------------------------------------  
        //   8   9  10  11   12   13   14   15   16   17 - 18   19   20   21   22   23 - 24   25   26   27   28   29 - 30   31   32   33   34   35 - 36   37   38   39   40   41 - 42   43   44   45   46   47  
        //   ?   ?   ?   ?  R2'0 G2'0 B2'0 R3'0 G3'0 B3'0 R2'1 G2'1 B2'1 R3'1 G3'1 B3'1 R2'2 G2'2 B2'2 R3'2 G3'2 B3'2 R2'3 G2'3 B2'3 R3'3 G3'3 B3'3 R2'4 G2'4 B2'4 R3'4 G3'4 B3'4 R2'5 G2'5 B2'5 R3'5 G3'5 B3'5 

        enum {
            R0 = 60,
            G0 = 61,
            B0 = 62,
            R1 = 63,
            G1 = 64,
            B1 = 65,

            R2 = 12,
            G2 = 13,
            B2 = 14,
            R3 = 15,
            G3 = 16,
            B3 = 17,
        };


        static const InplaceShiftBitsOrder ops[] = {
            {   // Mode (01)
                96,
                2
            },

            {   // Partition bits
                98,
                6
            },


            {    R0 + 6 * 0,  1 }, // R0'0
            {    R0 + 6 * 1,  1 }, // R0'1
            {    R0 + 6 * 2,  1 }, // R0'2
            {    R0 + 6 * 3,  1 }, // R0'3
            {    R0 + 6 * 4,  1 }, // R0'4
            {    R0 + 6 * 5,  1 }, // R0'5

            {    R1 + 6 * 0,  1 }, // R1'0
            {    R1 + 6 * 1,  1 }, // R1'1
            {    R1 + 6 * 2,  1 }, // R1'2
            {    R1 + 6 * 3,  1 }, // R1'3
            {    R1 + 6 * 4,  1 }, // R1'4
            {    R1 + 6 * 5,  1 }, // R1'5

            {    R2 + 6 * 0,  1 }, // R2'0
            {    R2 + 6 * 1,  1 }, // R2'1
            {    R2 + 6 * 2,  1 }, // R2'2
            {    R2 + 6 * 3,  1 }, // R2'3
            {    R2 + 6 * 4,  1 }, // R2'4
            {    R2 + 6 * 5,  1 }, // R2'5

            {    R3 + 6 * 0,  1 }, // R3'0
            {    R3 + 6 * 1,  1 }, // R3'1
            {    R3 + 6 * 2,  1 }, // R3'2
            {    R3 + 6 * 3,  1 }, // R3'3
            {    R3 + 6 * 4,  1 }, // R3'4
            {    R3 + 6 * 5,  1 }, // R3'5


            {    G0 + 6 * 0,  1 }, // G0'0
            {    G0 + 6 * 1,  1 }, // G0'1
            {    G0 + 6 * 2,  1 }, // G0'2
            {    G0 + 6 * 3,  1 }, // G0'3
            {    G0 + 6 * 4,  1 }, // G0'4
            {    G0 + 6 * 5,  1 }, // G0'5


            {    G1 + 6 * 0,  1 }, // G1'0
            {    G1 + 6 * 1,  1 }, // G1'1
            {    G1 + 6 * 2,  1 }, // G1'2
            {    G1 + 6 * 3,  1 }, // G1'3
            {    G1 + 6 * 4,  1 }, // G1'4
            {    G1 + 6 * 5,  1 }, // G1'5


            {    G2 + 6 * 0,  1 }, // G2'0
            {    G2 + 6 * 1,  1 }, // G2'1
            {    G2 + 6 * 2,  1 }, // G2'2
            {    G2 + 6 * 3,  1 }, // G2'3
            {    G2 + 6 * 4,  1 }, // G2'4
            {    G2 + 6 * 5,  1 }, // G2'5


            {    G3 + 6 * 0,  1 }, // G3'0
            {    G3 + 6 * 1,  1 }, // G3'1
            {    G3 + 6 * 2,  1 }, // G3'2
            {    G3 + 6 * 3,  1 }, // G3'3
            {    G3 + 6 * 4,  1 }, // G3'4
            {    G3 + 6 * 5,  1 }, // G3'5


            {    B0 + 6 * 0,  1 }, // B0'0
            {    B0 + 6 * 1,  1 }, // B0'1
            {    B0 + 6 * 2,  1 }, // B0'2
            {    B0 + 6 * 3,  1 }, // B0'3
            {    B0 + 6 * 4,  1 }, // B0'4
            {    B0 + 6 * 5,  1 }, // B0'5


            {    B1 + 6 * 0,  1 }, // B1'0
            {    B1 + 6 * 1,  1 }, // B1'1
            {    B1 + 6 * 2,  1 }, // B1'2
            {    B1 + 6 * 3,  1 }, // B1'3
            {    B1 + 6 * 4,  1 }, // B1'4
            {    B1 + 6 * 5,  1 }, // B1'5


            {    B2 + 6 * 0,  1 }, // B2'0
            {    B2 + 6 * 1,  1 }, // B2'1
            {    B2 + 6 * 2,  1 }, // B2'2
            {    B2 + 6 * 3,  1 }, // B2'3
            {    B2 + 6 * 4,  1 }, // B2'4
            {    B2 + 6 * 5,  1 }, // B2'5


            {    B3 + 6 * 0,  1 }, // B3'0
            {    B3 + 6 * 1,  1 }, // B3'1
            {    B3 + 6 * 2,  1 }, // B3'2
            {    B3 + 6 * 3,  1 }, // B3'3
            {    B3 + 6 * 4,  1 }, // B3'4
            {    B3 + 6 * 5,  1 }, // B3'5

            {   // P bits + index to fill out the 4-Byte tail with 8 bits 
                // 24 bits needed, starting at 96+8 = 104
                104,
                24
            },


            {   // remaiing index bits fill in two 12 bit gaps before the color fields 
                0,
                12
            },
            {
                48,
                12
            },

        };

        sequence.OpCount = ARRAYSIZE(ops);
        memcpy(sequence.Ops, &ops, sizeof(ops));
        return;
    }
}

void Get_BC7_ModeJoin_CopyBitsOrderMode2(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics)
{
    UNREFERENCED_PARAMETER(opt);
    // all "join" modes are 16 bytes "color" data, but with a segmentation of 12B + 4B
    // Most color data is encoded within that 12B block, and always as endpoint pairs
    // stacking bits from least to most significant, ending in a byte boundary.

    //  Bit Offset  Bits or ordering and count
    //  0           3 bits Mode (100)
    //  3           6 bits Partition
    //  9           5 bits R0   5 bits R1   5 bits R2   5 bits R3   5 bits R4   5 bits R5
    //  39          5 bits G0   5 bits G1   5 bits G2   5 bits G3   5 bits R4   5 bits R5
    //  69          5 bits B0   5 bits B1   5 bits B2   5 bits B3   5 bits R4   5 bits R5
    //  99          29 bits Index


    if (metrics.M[2].Statics)
    {
        // group static bits before byte 12, interleave others

        //const uint8_t staticFields = 3 * _mm_popcnt_u64(metrics.M[2].Statics);
        //const uint8_t nonStaticFields = 18 - staticFields;
        uint8_t nextStaticBit = 96;

        uint8_t bitSpread = 18;
        enum {
            R0 = 6,
            G0 = 7,
            B0 = 8,

            R1 = 9,
            G1 = 10,
            B1 = 11,

            R2 = 12,
            G2 = 13,
            B2 = 14,

            R3 = 15,
            G3 = 16,
            B3 = 17,

            R4 = 18,
            G4 = 19,
            B4 = 20,

            R5 = 21,
            G5 = 22,
            B5 = 23,
        };

        uint8_t fb[] = {    // first bit
            R0, R1, R2, R3, R4, R5,
            G0, G1, G2, G3, G4, G5,
            B0, B1, B2, B3, B4, B5,
        };

        for (size_t cc = 0; cc < 6; cc++)  // 
        {
            if (metrics.M[2].Statics & (1 << cc))    // static bits cover color channel 6/8 bits depending on RGB[A], multiple endpoint pairs mean both Cx\Cx+2 are static
            {
                // if channel is static (R0+R2, R1+R3, G0+G2, G1+G3, B0+B2, B1+B3;
                size_t e0c = (cc / 2) * 6 + (cc % 2);
                size_t e1c = (cc / 2) * 6 + (cc % 2) + 2;
                size_t e2c = (cc / 2) * 6 + (cc % 2) + 4;
                for (size_t e : {e0c, e1c, e2c})
                {
                    for (int i = 0; i < _countof(fb); i++)
                    {
                        if (fb[i] > fb[e])
                        {
                            fb[i]--;
                        }
                    }
                    nextStaticBit -= 5;
                    fb[e] = 0;
                }
                bitSpread -= 3;
            }
        }


        InplaceShiftBitsOrder ops[85] = {       // max of 82 ops with 1 static field
            {   // Mode (001)
                96,
                3
            },

            {   // Partition bits
                99,
                6
            },
        };

        size_t opCount = 2;

        for (size_t epc = 0; epc < 18; epc++)  // 18 total endpoint pair color channels
        {
            size_t c = (epc % 2) + 2 * (epc / 6);
            if (metrics.M[2].Statics & (1 << c))
            {
                ops[opCount].CopyBitCount = 5;
                ops[opCount].DestinationBitOffset = nextStaticBit;
                opCount++;
                nextStaticBit += 5;
            }
            else
            {
                for (uint32_t b = 0; b < 5; b++)
                {
                    ops[opCount].CopyBitCount = 1;
                    ops[opCount].DestinationBitOffset = uint8_t(fb[epc] + bitSpread * b);
                    opCount++;
                }
            }
        }


        // P bits + index to fill out the 4-Byte tail with 9 bits 
        // 23 bits needed, starting at 96+9 = 109
        ops[opCount].CopyBitCount = 23;
        ops[opCount].DestinationBitOffset = 105;
        opCount++;

        ops[opCount].CopyBitCount = 6;
        ops[opCount].DestinationBitOffset = 0;
        opCount++;

        assert(opCount <= _countof(ops));

        sequence.OpCount = opCount;
        memcpy(sequence.Ops, &ops, opCount * sizeof(InplaceShiftBitsOrder));
        return;
    }
    else
    {
        // Mode 2 has 3 endpoint pairs with 30 bits each, ending at byte 4, 8, 12

        //  ---------------------------------------------------------------   3 bytes + 6 bits -----------------------------------------------------------------
        //  66   67   68   69   70   71   72   73   74   75   76   77 - 78   79   80   81   82   83 - 84   85   86   87   88   89 - 90   91   92   93   94   95
        // R0'0 G0'0 B0'0 R1'0 G1'0 B1'0 R0'1 G0'1 B0'1 R1'1 G1'1 B1'1 R0'2 G0'2 B0'2 R1'2 G1'2 B1'2 R0'3 G0'3 B0'3 R1'3 G1'3 B1'3 R0'4 G0'4 B0'4 R1'4 G1'4 B1'4 

        //  ---------------------------------------------------------------   3 bytes + 6 bits -----------------------------------------------------------------
        //  34   35   36   37   38   39   40   41   42   43   44   45 - 46   47   48   49   50   51 - 52   53   54   55   56   57 - 58   59   60   61   62   63
        // R2'0 G2'0 B2'0 R3'0 G3'0 B3'0 R2'1 G2'1 B2'1 R3'1 G3'1 B3'1 R2'2 G2'2 B2'2 R3'2 G3'2 B3'2 R2'3 G2'3 B2'3 R3'3 G3'3 B3'3 R2'4 G2'4 B2'4 R3'4 G3'4 B3'4 

        //  ---------------------------------------------------------------   3 bytes + 6 bits -----------------------------------------------------------------
        //   2    3    4    5    6    7    8    9   10   11   12   13 - 14   15   16   17   18   19 - 20   21   22   23   24   25 - 26   27   28   29   30   31
        // R4'0 G4'0 B4'0 R5'0 G5'0 B5'0 R4'1 G4'1 B4'1 R5'1 G5'1 B5'1 R4'2 G4'2 B4'2 R5'2 G5'2 B5'2 R4'3 G4'3 B4'3 R5'3 G5'3 B5'3 R4'4 G4'4 B4'4 R5'4 G5'4 B5'4 

        enum {
            R0 = 66,  // 8 bytes + 2 bits
            G0 = 67,
            B0 = 68,
            R1 = 69,
            G1 = 70,
            B1 = 71,

            R2 = 34,    // 4 bytes + 2 bits
            G2 = 35,
            B2 = 36,
            R3 = 37,
            G3 = 38,
            B3 = 39,

            R4 = 2,     // 0 bytes + 2 bits
            G4 = 3,
            B4 = 4,
            R5 = 5,
            G5 = 6,
            B5 = 7,

        };


        static const InplaceShiftBitsOrder ops[] = {
            {   // Mode (001)
                96,
                3
            },

            {   // Partition bits
                99,
                6
            },

            {   R0 + 6 * 0, 1 },
            {   R0 + 6 * 1, 1 },
            {   R0 + 6 * 2, 1 },
            {   R0 + 6 * 3, 1 },
            {   R0 + 6 * 4, 1 },

            {   R1 + 6 * 0, 1 },
            {   R1 + 6 * 1, 1 },
            {   R1 + 6 * 2, 1 },
            {   R1 + 6 * 3, 1 },
            {   R1 + 6 * 4, 1 },

            {   R2 + 6 * 0, 1 },
            {   R2 + 6 * 1, 1 },
            {   R2 + 6 * 2, 1 },
            {   R2 + 6 * 3, 1 },
            {   R2 + 6 * 4, 1 },

            {   R3 + 6 * 0, 1 },
            {   R3 + 6 * 1, 1 },
            {   R3 + 6 * 2, 1 },
            {   R3 + 6 * 3, 1 },
            {   R3 + 6 * 4, 1 },

            {   R4 + 6 * 0, 1 },
            {   R4 + 6 * 1, 1 },
            {   R4 + 6 * 2, 1 },
            {   R4 + 6 * 3, 1 },
            {   R4 + 6 * 4, 1 },

            {   R5 + 6 * 0, 1 },
            {   R5 + 6 * 1, 1 },
            {   R5 + 6 * 2, 1 },
            {   R5 + 6 * 3, 1 },
            {   R5 + 6 * 4, 1 },

            {   G0 + 6 * 0, 1 },
            {   G0 + 6 * 1, 1 },
            {   G0 + 6 * 2, 1 },
            {   G0 + 6 * 3, 1 },
            {   G0 + 6 * 4, 1 },

            {   G1 + 6 * 0, 1 },
            {   G1 + 6 * 1, 1 },
            {   G1 + 6 * 2, 1 },
            {   G1 + 6 * 3, 1 },
            {   G1 + 6 * 4, 1 },

            {   G2 + 6 * 0, 1 },
            {   G2 + 6 * 1, 1 },
            {   G2 + 6 * 2, 1 },
            {   G2 + 6 * 3, 1 },
            {   G2 + 6 * 4, 1 },

            {   G3 + 6 * 0, 1 },
            {   G3 + 6 * 1, 1 },
            {   G3 + 6 * 2, 1 },
            {   G3 + 6 * 3, 1 },
            {   G3 + 6 * 4, 1 },

            {   G4 + 6 * 0, 1 },
            {   G4 + 6 * 1, 1 },
            {   G4 + 6 * 2, 1 },
            {   G4 + 6 * 3, 1 },
            {   G4 + 6 * 4, 1 },

            {   G5 + 6 * 0, 1 },
            {   G5 + 6 * 1, 1 },
            {   G5 + 6 * 2, 1 },
            {   G5 + 6 * 3, 1 },
            {   G5 + 6 * 4, 1 },

            {   B0 + 6 * 0, 1 },
            {   B0 + 6 * 1, 1 },
            {   B0 + 6 * 2, 1 },
            {   B0 + 6 * 3, 1 },
            {   B0 + 6 * 4, 1 },

            {   B1 + 6 * 0, 1 },
            {   B1 + 6 * 1, 1 },
            {   B1 + 6 * 2, 1 },
            {   B1 + 6 * 3, 1 },
            {   B1 + 6 * 4, 1 },

            {   B2 + 6 * 0, 1 },
            {   B2 + 6 * 1, 1 },
            {   B2 + 6 * 2, 1 },
            {   B2 + 6 * 3, 1 },
            {   B2 + 6 * 4, 1 },

            {   B3 + 6 * 0, 1 },
            {   B3 + 6 * 1, 1 },
            {   B3 + 6 * 2, 1 },
            {   B3 + 6 * 3, 1 },
            {   B3 + 6 * 4, 1 },

            {   B4 + 6 * 0, 1 },
            {   B4 + 6 * 1, 1 },
            {   B4 + 6 * 2, 1 },
            {   B4 + 6 * 3, 1 },
            {   B4 + 6 * 4, 1 },

            {   B5 + 6 * 0, 1 },
            {   B5 + 6 * 1, 1 },
            {   B5 + 6 * 2, 1 },
            {   B5 + 6 * 3, 1 },
            {   B5 + 6 * 4, 1 },

            {   // index to fill out the 4-Byte tail with 9 bits 
                // 23 bits needed, starting at 96+9 = 105
                105,
                23
            },

            {   // remaiing index bits fill in the first 2-bit gap before each color point 
                0,
                2
            },
            {
                32,
                2
            },
            {
                64,
                2
            },

        };

        sequence.OpCount = ARRAYSIZE(ops);
        memcpy(sequence.Ops, &ops, sizeof(ops));
        return;
    }
}

void Get_BC7_ModeJoin_CopyBitsOrderMode3(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics)
{
    UNREFERENCED_PARAMETER(opt);
    // all "join" modes are 16 bytes "color" data, but with a segmentation of 12B + 4B
    // Most color data is encoded within that 12B block, and always as endpoint pairs
    // stacking bits from least to most significant, ending in a byte boundary.

    //  Bit Offset  Bits or ordering and count
    //  0           4 bits Mode (1000)
    //  4           6 bits Partition
    //  10          7 bits R0   7 bits R1   7 bits R2   7 bits R3
    //  38          7 bits G0   7 bits G1   7 bits G2   7 bits G3
    //  66          7 bits B0   7 bits B1   7 bits B2   7 bits B3
    //  94          1 bit P0    1 bit P1    1 bit P2    1 bit P3
    //  98          30 bits Index


    if (metrics.M[3].Statics)
    {
        // group static bits before byte 12, interleave others

        //const uint8_t staticFields = 2 * _mm_popcnt_u64(metrics.M[3].Statics);
        //const uint8_t nonStaticFields = 12 - staticFields;
        uint8_t nextStaticBit = 96;

        uint8_t bitSpread = 12;
        enum {
            R0 = 12,
            G0 = 13,
            B0 = 14,

            R1 = 15,
            G1 = 16,
            B1 = 17,

            R2 = 18,
            G2 = 19,
            B2 = 20,

            R3 = 21,
            G3 = 22,
            B3 = 23,
        };

        uint8_t fb[] = {    // first bit
            R0, R1, R2, R3,
            G0, G1, G2, G3,
            B0, B1, B2, B3,
        };

        for (size_t cc = 0; cc < 6; cc++)  // 
        {
            if (metrics.M[3].Statics & (1 << cc))    // static bits cover color channel 6/8 bits depending on RGB[A], multiple endpoint pairs mean both Cx\Cx+2 are static
            {
                // if channel is static (R0+R2, R1+R3, G0+G2, G1+G3, B0+B2, B1+B3;
                size_t e0c = (cc / 2) * 4 + (cc % 2);
                size_t e1c = (cc / 2) * 4 + (cc % 2) + 2;
                for (size_t e : {e0c, e1c})
                {
                    for (int i = 0; i < _countof(fb); i++)
                    {
                        if (fb[i] > fb[e])
                        {
                            fb[i]--;
                        }
                    }
                    nextStaticBit -= 7;
                    fb[e] = 0;
                }
                bitSpread -= 2;
            }
        }


        InplaceShiftBitsOrder ops[90] = {
            {   // Mode (0001)
                96,
                4
            },

            {   // Partition bits
                100,
                6
            },
        };

        size_t opCount = 2;

        for (size_t epc = 0; epc < 12; epc++)  // 12 total endpoint pair color channels
        {
            size_t c = (epc % 2) + 2 * (epc / 4);
            if (metrics.M[3].Statics & (1 << c))
            {
                ops[opCount].CopyBitCount = 7;
                ops[opCount].DestinationBitOffset = nextStaticBit;
                opCount++;
                nextStaticBit += 7;
            }
            else
            {
                for (uint8_t b = 0; b < 7; b++)
                {
                    ops[opCount].CopyBitCount = 1;
                    ops[opCount].DestinationBitOffset = uint8_t(fb[epc] + bitSpread * b);
                    opCount++;
                }
            }
        }

        // P bits + index to fill out the 4-Byte tail with 8 bits 
        // 24 bits needed, starting at 96+8 = 104
        ops[opCount].CopyBitCount = 22;
        ops[opCount].DestinationBitOffset = 106;
        opCount++;

        ops[opCount].CopyBitCount = 12;
        ops[opCount].DestinationBitOffset = 0;
        opCount++;

        sequence.OpCount = opCount;
        memcpy(sequence.Ops, &ops, opCount * sizeof(InplaceShiftBitsOrder));
        return;
    }
    else
    {

        // Mode 3 contains 2 endpoint pairs each with 42 bits
        // E0/1 ends at byte 12, E2/E3 ends at byte 6, so there are 6 extra bits before each

        //                  ------------------------------------------------------------------------   5 bytes + 2 bits -------------------------------------------------------------------------------------  
        //   54   55   56   57   58   59   60   61   62   63   64   65 - 66   67   68   69   70   71 - 72   73   74   76   76   77 - 78   79   80   81   82   83 - 84   85   86   87   88   89 - 90   91   92   93   94   95  
        //  R0'0 G0'0 B0'0 R1'0 G1'0 B1'0 R0'1 G0'1 B0'1 R1'1 G1'1 B1'1 R0'2 G0'2 B0'2 R1'2 G1'2 B1'2 R0'3 G0'3 B0'3 R1'3 G1'3 B1'3 R0'4 G0'4 B0'4 R1'4 G1'4 B1'4 R0'5 G0'5 B0'5 R1'5 G1'5 B1'5 R0'6 G0'6 B0'6 R1'6 G1'6 B1'6

        //                  ------------------------------------------------------------------------   5 bytes + 2 bits -------------------------------------------------------------------------------------  
        //   6    7    8    9    10   11   12   13   14   15   16   17 - 18   19   20   21   22   23 - 24   25   26   27   28   29 - 30   31   32   33   34   35 - 36   37   38   39   40   41 - 42   43   44   45   46   47  
        //  R2'0 G2'0 B2'0 R3'0 G3'0 B3'0 R2'1 G2'1 B2'1 R3'1 G3'1 B3'1 R2'2 G2'2 B2'2 R3'2 G3'2 B3'2 R2'3 G2'3 B2'3 R3'3 G3'3 B3'3 R2'4 G2'4 B2'4 R3'4 G3'4 B3'4 R2'5 G2'5 B2'5 R3'5 G3'5 B3'5 R2'6 G2'6 B2'6 R3'6 G3'6 B3'6  

        enum {
            R0 = 54,        // 6 bytes + 6 bits
            G0 = 55,
            B0 = 56,
            R1 = 57,
            G1 = 58,
            B1 = 59,

            R2 = 6,
            G2 = 7,
            B2 = 8,
            R3 = 9,
            G3 = 10,
            B3 = 11,
        };

        static const InplaceShiftBitsOrder ops[] = {
            {   // Mode (0001)
                96,
                4
            },

            {   // Partition bits
                100,
                6
            },

            {   R0 + 6 * 0, 1 },
            {   R0 + 6 * 1, 1 },
            {   R0 + 6 * 2, 1 },
            {   R0 + 6 * 3, 1 },
            {   R0 + 6 * 4, 1 },
            {   R0 + 6 * 5, 1 },
            {   R0 + 6 * 6, 1 },

            {   R1 + 6 * 0, 1 },
            {   R1 + 6 * 1, 1 },
            {   R1 + 6 * 2, 1 },
            {   R1 + 6 * 3, 1 },
            {   R1 + 6 * 4, 1 },
            {   R1 + 6 * 5, 1 },
            {   R1 + 6 * 6, 1 },

            {   R2 + 6 * 0, 1 },
            {   R2 + 6 * 1, 1 },
            {   R2 + 6 * 2, 1 },
            {   R2 + 6 * 3, 1 },
            {   R2 + 6 * 4, 1 },
            {   R2 + 6 * 5, 1 },
            {   R2 + 6 * 6, 1 },

            {   R3 + 6 * 0, 1 },
            {   R3 + 6 * 1, 1 },
            {   R3 + 6 * 2, 1 },
            {   R3 + 6 * 3, 1 },
            {   R3 + 6 * 4, 1 },
            {   R3 + 6 * 5, 1 },
            {   R3 + 6 * 6, 1 },

            {   G0 + 6 * 0, 1 },
            {   G0 + 6 * 1, 1 },
            {   G0 + 6 * 2, 1 },
            {   G0 + 6 * 3, 1 },
            {   G0 + 6 * 4, 1 },
            {   G0 + 6 * 5, 1 },
            {   G0 + 6 * 6, 1 },

            {   G1 + 6 * 0, 1 },
            {   G1 + 6 * 1, 1 },
            {   G1 + 6 * 2, 1 },
            {   G1 + 6 * 3, 1 },
            {   G1 + 6 * 4, 1 },
            {   G1 + 6 * 5, 1 },
            {   G1 + 6 * 6, 1 },

            {   G2 + 6 * 0, 1 },
            {   G2 + 6 * 1, 1 },
            {   G2 + 6 * 2, 1 },
            {   G2 + 6 * 3, 1 },
            {   G2 + 6 * 4, 1 },
            {   G2 + 6 * 5, 1 },
            {   G2 + 6 * 6, 1 },

            {   G3 + 6 * 0, 1 },
            {   G3 + 6 * 1, 1 },
            {   G3 + 6 * 2, 1 },
            {   G3 + 6 * 3, 1 },
            {   G3 + 6 * 4, 1 },
            {   G3 + 6 * 5, 1 },
            {   G3 + 6 * 6, 1 },

            {   B0 + 6 * 0, 1 },
            {   B0 + 6 * 1, 1 },
            {   B0 + 6 * 2, 1 },
            {   B0 + 6 * 3, 1 },
            {   B0 + 6 * 4, 1 },
            {   B0 + 6 * 5, 1 },
            {   B0 + 6 * 6, 1 },

            {   B1 + 6 * 0, 1 },
            {   B1 + 6 * 1, 1 },
            {   B1 + 6 * 2, 1 },
            {   B1 + 6 * 3, 1 },
            {   B1 + 6 * 4, 1 },
            {   B1 + 6 * 5, 1 },
            {   B1 + 6 * 6, 1 },

            {   B2 + 6 * 0, 1 },
            {   B2 + 6 * 1, 1 },
            {   B2 + 6 * 2, 1 },
            {   B2 + 6 * 3, 1 },
            {   B2 + 6 * 4, 1 },
            {   B2 + 6 * 5, 1 },
            {   B2 + 6 * 6, 1 },

            {   B3 + 6 * 0, 1 },
            {   B3 + 6 * 1, 1 },
            {   B3 + 6 * 2, 1 },
            {   B3 + 6 * 3, 1 },
            {   B3 + 6 * 4, 1 },
            {   B3 + 6 * 5, 1 },
            {   B3 + 6 * 6, 1 },

            {   // index to fill out the 4-Byte tail with 10 bits 
                // 22 bits needed, starting at 96 + 10 = 106
                106,
                22
            },

            {   // remaiing index bits fill in two 6 bit gaps before the color fields 
                0,
                6
            },
            {
                48,
                6
            },
        };

        sequence.OpCount = ARRAYSIZE(ops);
        memcpy(sequence.Ops, &ops, sizeof(ops));
        return;
    }
}

void Get_BC7_ModeJoin_CopyBitsOrderMode4(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics)
{
    UNREFERENCED_PARAMETER(opt);
    // all "join" modes are 16 bytes "color" data, but with a segmentation of 12B + 4B
    // Most color data is encoded within that 12B block, and always as endpoint pairs
    // stacking bits from least to most significant, ending in a byte boundary.

    //  Bit Offset  Bits or ordering and count
    //  0           5 bits Mode (10000)
    //  5           2 bits Rotation
    //  7           1 bit Idx Mode
    //  8           5 bits R0   5 bits R1
    //  18          5 bits G0   5 bits G1
    //  28          5 bits B0   5 bits B1
    //  38          6 bits A0   6 bits A1
    //  50          31 bits Index Data
    //  81          47 bits Index Data

    // However, we actually operate off the "derotated" layout, which ensures the color data is always RGBA order:

    //  Bit Offset  Bits or ordering and count
    //  0           5 bits Mode (10000)
    //  5           2 bits Rotation
    //  7           1 bit Idx Mode
    //  8           5 bits R0   5 bits R1
    //  18          5 bits G0   5 bits G1
    //  28          5 bits B0   5 bits B1
    //  38          1 bit  ex0
    //  39          5 bits A0
    //  44                      1 bit  ex1
    //  45                      5 bits A1
    //  50          31 bits Index Data
    //  81          47 bits Index Data


    if (metrics.M[4].Statics)
    {
        // group static bits before byte 12, interleave others

        //const uint8_t staticFields = _mm_popcnt_u64(metrics.M[4].Statics);
        //const uint8_t nonStaticFields = 8 - staticFields;
        uint8_t nextStaticBit = 96;

        uint8_t bitSpread = 8;
        enum {
            R0 = 56,
            G0 = 57,
            B0 = 58,
            A0 = 59,

            R1 = 60,
            G1 = 61,
            B1 = 62,
            A1 = 63,
        };

        uint8_t fb[] = {    // first bit
            R0, R1,
            G0, G1,
            B0, B1,
            A0, A1
        };

        for (size_t cc = 0; cc < 8; cc++)  // 
        {
            if (metrics.M[4].Statics & (1 << cc))    // static bits cover color channel 6/8 bits depending on RGB[A]
            {
                for (int i = 0; i < _countof(fb); i++)
                {
                    if (fb[i] > fb[cc])
                    {
                        fb[i]--;
                    }
                }
                nextStaticBit -= 5;
                fb[cc] = 0;

                bitSpread--;
            }
        }

        InplaceShiftBitsOrder ops[90] = {
            {   // Mode + rotation + idx
                96,
                8
            },
        };

        size_t opCount = 1;
        uint8_t nextExBit = 54;

        for (size_t epc = 0; epc < 8; epc++)  // 8 total endpoint pair color channels
        {
            if (epc >= 6)
            {
                ops[opCount].CopyBitCount = 1;
                ops[opCount].DestinationBitOffset = nextExBit;
                opCount++;
                nextExBit++;
            }

            if (metrics.M[4].Statics & (1 << epc))
            {
                ops[opCount].CopyBitCount = 5;
                ops[opCount].DestinationBitOffset = nextStaticBit;
                opCount++;
                nextStaticBit += 5;
            }
            else
            {
                for (uint8_t b = 0; b < 5; b++)
                {
                    ops[opCount].CopyBitCount = 1;
                    ops[opCount].DestinationBitOffset = uint8_t(fb[epc] + bitSpread * b);
                    opCount++;
                }
            }
        }

        // P bits + index to fill out the 4-Byte tail with 8 bits 
        // 24 bits needed, starting at 96+8 = 104
        ops[opCount].CopyBitCount = 24;
        ops[opCount].DestinationBitOffset = 104;
        opCount++;

        ops[opCount].CopyBitCount = 54;
        ops[opCount].DestinationBitOffset = 0;
        opCount++;

        sequence.OpCount = opCount;
        memcpy(sequence.Ops, &ops, opCount * sizeof(InplaceShiftBitsOrder));
        return;
    }
    else
    {

        // Mode 4 contains 1 endpoint pair with 40 bits  (not counting the two least significant bits in the alpha channel, which might not be alpha)
        // E0/1 ends at byte 12, so begins at 96 - 40 = bit 56
        // ex0/ex1 are pushed into bits 54/55

        //   -------------------------------------------------------------------------------------   5 bytes  ----------------------------------------------------------------------------------------------------  
        //   56   57   58   59   60   61   62   63 - 64   65   66   67   68   69   70   71 - 72   73   74   76   76   77   78   79 - 80   81   82   83   84   85   86   87 - 88   89   90   91   92   93   94   95  
        //  R0'0 G0'0 B0'0 A0'0 R1'0 G1'0 B1'0 A1'0 R0'1 G0'1 B0'1 A0'1 R1'1 G1'1 B1'1 A1'1 R0'2 G0'2 B0'2 A0'2 R1'2 G1'2 B1'2 A1'2 R0'3 G0'3 B0'3 A0'3 R1'3 G1'3 B1'3 A1'3 R0'4 G0'4 B0'4 A0'4 R1'4 G1'4 B1'4 A1'4 

        enum {
            R0 = 56,
            G0 = 57,
            B0 = 58,

            R1 = 59,
            G1 = 60,
            B1 = 61,

            A0 = 62,
            A1 = 63,

            ex0 = A0 - 8,
            ex1 = A1 - 8
        };

        static const InplaceShiftBitsOrder ops[] = {
            {   // Mode + rotation + idx
                96,
                8
            },

            {   R0 + 8 * 0, 1 },
            {   R0 + 8 * 1, 1 },
            {   R0 + 8 * 2, 1 },
            {   R0 + 8 * 3, 1 },
            {   R0 + 8 * 4, 1 },

            {   R1 + 8 * 0, 1 },
            {   R1 + 8 * 1, 1 },
            {   R1 + 8 * 2, 1 },
            {   R1 + 8 * 3, 1 },
            {   R1 + 8 * 4, 1 },

            {   G0 + 8 * 0, 1 },
            {   G0 + 8 * 1, 1 },
            {   G0 + 8 * 2, 1 },
            {   G0 + 8 * 3, 1 },
            {   G0 + 8 * 4, 1 },

            {   G1 + 8 * 0, 1 },
            {   G1 + 8 * 1, 1 },
            {   G1 + 8 * 2, 1 },
            {   G1 + 8 * 3, 1 },
            {   G1 + 8 * 4, 1 },

            {   B0 + 8 * 0, 1 },
            {   B0 + 8 * 1, 1 },
            {   B0 + 8 * 2, 1 },
            {   B0 + 8 * 3, 1 },
            {   B0 + 8 * 4, 1 },

            {   B1 + 8 * 0, 1 },
            {   B1 + 8 * 1, 1 },
            {   B1 + 8 * 2, 1 },
            {   B1 + 8 * 3, 1 },
            {   B1 + 8 * 4, 1 },

            {   ex0,  1  },     // first of two bits before the comingled endpoint pair starting at bit 56

            {   A0 + 8 * 0, 1 },
            {   A0 + 8 * 1, 1 },
            {   A0 + 8 * 2, 1 },
            {   A0 + 8 * 3, 1 },
            {   A0 + 8 * 4, 1 },

            {   ex1,  1  },    // second of two bits before the comingled endpoint pair starting at bit 56

            {   A1 + 8 * 0, 1 },
            {   A1 + 8 * 1, 1 },
            {   A1 + 8 * 2, 1 },
            {   A1 + 8 * 3, 1 },
            {   A1 + 8 * 4, 1 },

            {   // index to fill out the 4-Byte tail with 8 bits of existing data
                // 24 bits needed, starting at 96 + 8 = 104
                104,
                24,
            },


            {   // remaiing index bits fill in 54 bit gap before the color field 
                0,
                54          // 31 + 47 - 24
            },
        };

        sequence.OpCount = ARRAYSIZE(ops);
        memcpy(sequence.Ops, &ops, sizeof(ops));;
        return;
    }
}

void Get_BC7_ModeJoin_CopyBitsOrderMode5(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics)
{
    UNREFERENCED_PARAMETER(opt);
    // all "join" modes are 16 bytes "color" data, but with a segmentation of 12B + 4B
    // Most color data is encoded within that 12B block, and always as endpoint pairs
    // stacking bits from least to most significant, ending in a byte boundary.


    //  Bit Offset  Bits or ordering and count
    //  0           6 bits Mode (100000)
    //  6           2 bits Rotation
    //  8           7 bits R0   7 bits R1
    //  22          7 bits G0   7 bits G1
    //  36          7 bits B0   7 bits B1
    //  50          8 bits A0   8 bits A1
    //  66          31 bits Color Index
    //  97          31 bits Alpha Index

    // However, we actually operate off the "derotated" layout, which ensures the color data is always RGBA order:

    //  Bit Offset  Bits or ordering and count
    //  0           6 bits Mode (100000)
    //  6           2 bits Rotation
    //  8           7 bits R0   7 bits R1
    //  22          7 bits G0   7 bits G1
    //  36          7 bits B0   7 bits B1
    //  50          1 bit ex0   
    //  51          7 bits A0   
    //  58                      1 bit ex1
    //  59                      7 bits A1
    //  66          31 bits Color Index
    //  97          31 bits Alpha Index

    if (metrics.M[5].Statics)
    {
        // group static bits before byte 12, interleave others

        //const uint8_t staticFields = _mm_popcnt_u64(metrics.M[5].Statics);
        //const uint8_t nonStaticFields = 8 - staticFields;
        uint8_t nextStaticBit = 96;

        uint8_t bitSpread = 8;
        enum {
            R0 = 40,
            G0 = 41,
            B0 = 42,
            A0 = 43,

            R1 = 44,
            G1 = 45,
            B1 = 46,
            A1 = 47,
        };

        uint8_t fb[] = {    // first bit
            R0, R1,
            G0, G1,
            B0, B1,
            A0, A1
        };

        for (size_t cc = 0; cc < 8; cc++)  // 
        {
            if (metrics.M[5].Statics & (1 << cc))    // static bits cover color channel 6/8 bits depending on RGB[A]
            {
                for (int i = 0; i < _countof(fb); i++)
                {
                    if (fb[i] > fb[cc])
                    {
                        fb[i]--;
                    }
                }
                nextStaticBit -= 7;
                fb[cc] = 0;
                bitSpread--;
            }
        }

        InplaceShiftBitsOrder ops[90] = {
            {   // Mode + rotation
                96,
                8
            },
        };

        size_t opCount = 1;
        uint8_t nextExBit = 38;

        for (size_t epc = 0; epc < 8; epc++)  // 8 total endpoint pair color channels
        {
            if (epc >= 6)
            {
                ops[opCount].CopyBitCount = 1;
                ops[opCount].DestinationBitOffset = nextExBit;
                opCount++;
                nextExBit++;
            }

            if (metrics.M[5].Statics & (1 << epc))
            {
                ops[opCount].CopyBitCount = 7;
                ops[opCount].DestinationBitOffset = nextStaticBit;
                opCount++;
                nextStaticBit += 7;
            }
            else
            {
                for (uint8_t b = 0; b < 7; b++)
                {
                    ops[opCount].CopyBitCount = 1;
                    ops[opCount].DestinationBitOffset = uint8_t(fb[epc] + bitSpread * b);
                    opCount++;
                }
            }
        }

        // P bits + index to fill out the 4-Byte tail with 8 bits 
        // 24 bits needed, starting at 96+8 = 104
        ops[opCount].CopyBitCount = 24;
        ops[opCount].DestinationBitOffset = 104;
        opCount++;

        ops[opCount].CopyBitCount = 38;
        ops[opCount].DestinationBitOffset = 0;
        opCount++;

        sequence.OpCount = opCount;
        memcpy(sequence.Ops, &ops, opCount * sizeof(InplaceShiftBitsOrder));
        return;
    }
    else
    {

        // Mode 5 contains 1 endpoint pair with 58 bits, 56 after derotation
        // E0/1 ends at byte 12, so begins at 96 - 56 = bit 40

        //   --------------------------------------------------------------------------------------------------------------------------   7 bytes   ----------------------------------------------------------------------------------------------------------------------------------------------  
        //   40   41   42   43   44   45   46   47 - 48   49   50   51   52   53   54   55 - 56   57   58   59   60   61   62   63   64   65 - 66   67   68   69   70   71 - 72   73   74   76   76   77   78   79 - 80   81   82   83   84   85   86   87 - 88   89   90   91   92   93   94   95  
        //  R0'0 G0'0 B0'0 A0'0 R1'0 G1'0 B1'0 A1'0 R0'1 G0'1 B0'1 A0'1 R1'1 G1'1 B1'1 A1'1 R0'2 G0'2 B0'2 A0'2 R1'2 G1'2 B1'2 A1'2 R0'3 G0'3 B0'3 A0'3 R1'3 G1'3 B1'3 A1'3 R0'4 G0'4 B0'4 A0'4 R1'4 G1'4 B1'4 A1'4 R0'5 G0'5 B0'5 A0'5 R1'5 G1'5 B1'5 A1'5 R0'6 G0'6 B0'6 A0'6 R1'6 G1'6 B1'6 A1'6


        enum {
            R0 = 40,
            G0 = 41,
            B0 = 42,

            R1 = 43,
            G1 = 44,
            B1 = 45,

            A0 = 46,
            A1 = 47,
            ex0 = A0 - 8,
            ex1 = A1 - 8,

        };

        static const InplaceShiftBitsOrder ops[] = {
            {   // Mode + rotation
                96,
                8
            },

            {   R0 + 8 * 0, 1 },
            {   R0 + 8 * 1, 1 },
            {   R0 + 8 * 2, 1 },
            {   R0 + 8 * 3, 1 },
            {   R0 + 8 * 4, 1 },
            {   R0 + 8 * 5, 1 },
            {   R0 + 8 * 6, 1 },

            {   R1 + 8 * 0, 1 },
            {   R1 + 8 * 1, 1 },
            {   R1 + 8 * 2, 1 },
            {   R1 + 8 * 3, 1 },
            {   R1 + 8 * 4, 1 },
            {   R1 + 8 * 5, 1 },
            {   R1 + 8 * 6, 1 },

            {   G0 + 8 * 0, 1 },
            {   G0 + 8 * 1, 1 },
            {   G0 + 8 * 2, 1 },
            {   G0 + 8 * 3, 1 },
            {   G0 + 8 * 4, 1 },
            {   G0 + 8 * 5, 1 },
            {   G0 + 8 * 6, 1 },

            {   G1 + 8 * 0, 1 },
            {   G1 + 8 * 1, 1 },
            {   G1 + 8 * 2, 1 },
            {   G1 + 8 * 3, 1 },
            {   G1 + 8 * 4, 1 },
            {   G1 + 8 * 5, 1 },
            {   G1 + 8 * 6, 1 },

            {   B0 + 8 * 0, 1 },
            {   B0 + 8 * 1, 1 },
            {   B0 + 8 * 2, 1 },
            {   B0 + 8 * 3, 1 },
            {   B0 + 8 * 4, 1 },
            {   B0 + 8 * 5, 1 },
            {   B0 + 8 * 6, 1 },

            {   B1 + 8 * 0, 1 },
            {   B1 + 8 * 1, 1 },
            {   B1 + 8 * 2, 1 },
            {   B1 + 8 * 3, 1 },
            {   B1 + 8 * 4, 1 },
            {   B1 + 8 * 5, 1 },
            {   B1 + 8 * 6, 1 },

            {   ex0,  1 },      // first of two bits before the comingled endpoint pair starting at bit 40

            {   A0 + 8 * 0, 1 },
            {   A0 + 8 * 1, 1 },
            {   A0 + 8 * 2, 1 },
            {   A0 + 8 * 3, 1 },
            {   A0 + 8 * 4, 1 },
            {   A0 + 8 * 5, 1 },
            {   A0 + 8 * 6, 1 },

            {   ex1,  1 },      // second of two bits before the comingled endpoint pair starting at bit 40

            {   A1 + 8 * 0, 1 },
            {   A1 + 8 * 1, 1 },
            {   A1 + 8 * 2, 1 },
            {   A1 + 8 * 3, 1 },
            {   A1 + 8 * 4, 1 },
            {   A1 + 8 * 5, 1 },
            {   A1 + 8 * 6, 1 },

            // original "tail"

            {   // index to fill out the 4-Byte tail with 8 bits of existing data
                // 24 bits needed, starting at 96 + 8 = 104
                104,
                24
            },

            {   // remaining index bits fill in 54 bit gap before the color field 
                0,
                38              // 62 bits of index - 24 already filled above
            },

            //{   // some textures with Mode 5 blocks have low entropy alpha index data, stack it next to the significant bits
            //    0,
            //    38
            //},

            //{   // remaining index bits fill in 54 bit gap before the color field 
            //    104,
            //    24              // put 3 bytes of Alpha index next to stable bits, sometimes field is unused
            //},

        };

        sequence.OpCount = ARRAYSIZE(ops);
        memcpy(sequence.Ops, &ops, sizeof(ops));
        return;
    }
}

void Get_BC7_ModeJoin_CopyBitsOrderMode6(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics)
{
    UNREFERENCED_PARAMETER(opt);
    // all "join" modes are 16 bytes "color" data, but with a segmentation of 12B + 4B
    // Most color data is encoded within that 12B block, and always as endpoint pairs
    // stacking bits from least to most significant, ending in a byte boundary.

    //  Bit Offset  Bits or ordering and count
    //  0           7 bits Mode (1000000)
    //  7           7 bits R0   7 bits R1
    //  21          7 bits G0   7 bits G1
    //  35          7 bits B0   7 bits B1
    //  49          7 bits A0   7 bits A1
    //  63          1 bit P0    1 bit P1
    //  65          63 bits Index

    if (metrics.M[6].Statics)
    {
        // group static bits before byte 12, interleave others

        //const uint8_t staticFields = _mm_popcnt_u64(metrics.M[6].Statics);
        //const uint8_t nonStaticFields = 8 - staticFields;
        uint8_t nextStaticBit = 96;

        uint8_t bitSpread = 8;
        enum {
            R0 = 40,
            G0 = 41,
            B0 = 42,
            A0 = 43,

            R1 = 44,
            G1 = 45,
            B1 = 46,
            A1 = 47,
        };

        uint8_t fb[] = {    // first bit
            R0, R1,
            G0, G1,
            B0, B1,
            A0, A1
        };

        for (size_t cc = 0; cc < 8; cc++)  // 
        {
            if (metrics.M[6].Statics & (1 << cc))    // static bits cover color channel 6/8 bits depending on RGB[A]
            {
                for (int i = 0; i < _countof(fb); i++)
                {
                    if (fb[i] > fb[cc])
                    {
                        fb[i]--;
                    }
                }
                nextStaticBit -= 7;
                fb[cc] = 0;
                bitSpread--;
            }
        }

        InplaceShiftBitsOrder ops[90] = {
            {   // Mode + rotation
                96,
                7
            }
        };

        size_t opCount = 1;

        for (size_t epc = 0; epc < 8; epc++)  // 12 total endpoint pair color channels
        {
            if (metrics.M[6].Statics & (1 << epc))
            {
                ops[opCount].CopyBitCount = 7;
                ops[opCount].DestinationBitOffset = nextStaticBit;
                opCount++;
                nextStaticBit += 7;
            }
            else
            {
                for (uint32_t b = 0; b < 7; b++)
                {
                    ops[opCount].CopyBitCount = 1;
                    ops[opCount].DestinationBitOffset = uint8_t(fb[epc] + bitSpread * b);
                    opCount++;
                }
            }
        }

        // P bits + index to fill out the 4-Byte tail with 8 bits 
        // 24 bits needed, starting at 96+8 = 104
        ops[opCount].CopyBitCount = 25;
        ops[opCount].DestinationBitOffset = 103;
        opCount++;

        ops[opCount].CopyBitCount = 40;
        ops[opCount].DestinationBitOffset = 0;
        opCount++;

        sequence.OpCount = opCount;
        memcpy(sequence.Ops, &ops, opCount * sizeof(InplaceShiftBitsOrder));
        return;
    }
    else
    {
        // Mode 6 contains 1 endpoint pair with 56 bits
        // E0/1 ends at byte 12, so begins at 96 - 56 = bit 40

        //   --------------------------------------------------------------------------------------------------------------------------   7 bytes   ----------------------------------------------------------------------------------------------------------------------------------------------  
        //   40   41   42   43   44   45   46   47 - 48   49   50   51   52   53   54   55 - 56   57   58   59   60   61   62   63   64   65 - 66   67   68   69   70   71 - 72   73   74   76   76   77   78   79 - 80   81   82   83   84   85   86   87 - 88   89   90   91   92   93   94   95  
        //  R0'0 G0'0 B0'0 A0'0 R1'0 G1'0 B1'0 A1'0 R0'1 G0'1 B0'1 A0'1 R1'1 G1'1 B1'1 A1'1 R0'2 G0'2 B0'2 A0'2 R1'2 G1'2 B1'2 A1'2 R0'3 G0'3 B0'3 A0'3 R1'3 G1'3 B1'3 A1'3 R0'4 G0'4 B0'4 A0'4 R1'4 G1'4 B1'4 A1'4 R0'5 G0'5 B0'5 A0'5 R1'5 G1'5 B1'5 A1'5 R0'6 G0'6 B0'6 A0'6 R1'6 G1'6 B1'6 A1'6

        enum {
            R0 = 40,
            G0 = 41,
            B0 = 42,
            R1 = 43,
            G1 = 44,
            B1 = 45,
            A0 = 46,
            A1 = 47,
        };

        static const InplaceShiftBitsOrder ops[] = {
            {   // Mode + rotation
                96,
                7
            },

            {   R0 + 8 * 0, 1 },
            {   R0 + 8 * 1, 1 },
            {   R0 + 8 * 2, 1 },
            {   R0 + 8 * 3, 1 },
            {   R0 + 8 * 4, 1 },
            {   R0 + 8 * 5, 1 },
            {   R0 + 8 * 6, 1 },

            {   R1 + 8 * 0, 1 },
            {   R1 + 8 * 1, 1 },
            {   R1 + 8 * 2, 1 },
            {   R1 + 8 * 3, 1 },
            {   R1 + 8 * 4, 1 },
            {   R1 + 8 * 5, 1 },
            {   R1 + 8 * 6, 1 },

            {   G0 + 8 * 0, 1 },
            {   G0 + 8 * 1, 1 },
            {   G0 + 8 * 2, 1 },
            {   G0 + 8 * 3, 1 },
            {   G0 + 8 * 4, 1 },
            {   G0 + 8 * 5, 1 },
            {   G0 + 8 * 6, 1 },

            {   G1 + 8 * 0, 1 },
            {   G1 + 8 * 1, 1 },
            {   G1 + 8 * 2, 1 },
            {   G1 + 8 * 3, 1 },
            {   G1 + 8 * 4, 1 },
            {   G1 + 8 * 5, 1 },
            {   G1 + 8 * 6, 1 },

            {   B0 + 8 * 0, 1 },
            {   B0 + 8 * 1, 1 },
            {   B0 + 8 * 2, 1 },
            {   B0 + 8 * 3, 1 },
            {   B0 + 8 * 4, 1 },
            {   B0 + 8 * 5, 1 },
            {   B0 + 8 * 6, 1 },

            {   B1 + 8 * 0, 1 },
            {   B1 + 8 * 1, 1 },
            {   B1 + 8 * 2, 1 },
            {   B1 + 8 * 3, 1 },
            {   B1 + 8 * 4, 1 },
            {   B1 + 8 * 5, 1 },
            {   B1 + 8 * 6, 1 },

            {   A0 + 8 * 0, 1 },
            {   A0 + 8 * 1, 1 },
            {   A0 + 8 * 2, 1 },
            {   A0 + 8 * 3, 1 },
            {   A0 + 8 * 4, 1 },
            {   A0 + 8 * 5, 1 },
            {   A0 + 8 * 6, 1 },

            {   A1 + 8 * 0, 1 },
            {   A1 + 8 * 1, 1 },
            {   A1 + 8 * 2, 1 },
            {   A1 + 8 * 3, 1 },
            {   A1 + 8 * 4, 1 },
            {   A1 + 8 * 5, 1 },
            {   A1 + 8 * 6, 1 },

            {   // index to fill out the 4-Byte tail with 7 bits of existing data
                // 25 bits needed, starting at 96 + 7 = 103
                103,
                25
            },


            {   // remaiing index bits fill in 54 bit gap before the color field 
                0,
                40         // 65 pbit & index - 25 bits already used 
            },
        };

        sequence.OpCount = ARRAYSIZE(ops);
        memcpy(sequence.Ops, &ops, sizeof(ops));
        return;
    }
}

void Get_BC7_ModeJoin_CopyBitsOrderMode7(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics)
{
    UNREFERENCED_PARAMETER(opt);
    // all "join" modes are 16 bytes "color" data, but with a segmentation of 12B + 4B
    // Most color data is encoded within that 12B block, and always as endpoint pairs
    // stacking bits from least to most significant, ending in a byte boundary.

    //  Bit Offset  Bits or ordering and count
    //  0           8 bits Mode (10000000)
    //  6           6 bits Partition
    //  14          5 bits R0   5 bits R1   5 bits R2   5 bits R3
    //  34          5 bits G0   5 bits G1   5 bits G2   5 bits G3
    //  54          5 bits B0   5 bits B1   5 bits B2   5 bits B3
    //  74          5 bits A0   5 bits A1   5 bits A2   5 bits A3
    //  94          1 bit P0    1 bit P1    1 bit P2    1 bit P2
    //  98          30 bits Index

    if (metrics.M[7].Statics)
    {
        // group static bits before byte 12, interleave others

        //const uint8_t staticFields = 2 * _mm_popcnt_u64(metrics.M[7].Statics);
        //const uint8_t nonStaticFields = 16 - staticFields;
        uint8_t nextStaticBit = 96;

        uint8_t bitSpread = 16;
        enum {
            R0 = 16,
            G0 = 17,
            B0 = 18,
            A0 = 19,

            R1 = 20,
            G1 = 21,
            B1 = 22,
            A1 = 23,

            R2 = 24,
            G2 = 25,
            B2 = 26,
            A2 = 27,

            R3 = 28,
            G3 = 29,
            B3 = 30,
            A3 = 31,
        };

        uint8_t fb[] = {    // first bit
            R0, R1, R2, R3,
            G0, G1, G2, G3,
            B0, B1, B2, B3,
            A0, A1, A2, A3,
        };

        for (size_t cc = 0; cc < 8; cc++)  // 
        {
            if (metrics.M[7].Statics & (1 << cc))    // static bits cover color channel 6/8 bits depending on RGB[A], multiple endpoint pairs mean both Cx\Cx+2 are static
            {
                // if channel is static (R0+R2, R1+R3, G0+G2, G1+G3, B0+B2, B1+B3;
                size_t e0c = (cc / 2) * 4 + (cc % 2);
                size_t e1c = (cc / 2) * 4 + (cc % 2) + 2;
                for (size_t e : {e0c, e1c})
                {
                    for (uint8_t i = 0; i < _countof(fb); i++)
                    {
                        if (fb[i] > fb[e])
                        {
                            fb[i]--;
                        }
                    }
                    nextStaticBit -= 5;
                    fb[e] = 0;
                }
                bitSpread -= 2;
            }
        }

        InplaceShiftBitsOrder ops[90] = {
            {   // Mode + partition
                96,
                14
            },
        };

        size_t opCount = 1;

        for (size_t epc = 0; epc < 16; epc++)  // 12 total endpoint pair color channels
        {
            size_t c = (epc % 2) + 2 * (epc / 4);
            if (metrics.M[7].Statics & (1 << c))
            {
                ops[opCount].CopyBitCount = 5;
                ops[opCount].DestinationBitOffset = nextStaticBit;
                opCount++;
                nextStaticBit += 5;
            }
            else
            {
                for (uint32_t b = 0; b < 5; b++)
                {
                    ops[opCount].CopyBitCount = 1;
                    ops[opCount].DestinationBitOffset = uint8_t(fb[epc] + bitSpread * b);
                    opCount++;
                }
            }
        }

        // P bits + index to fill out the 4-Byte tail with 8 bits 
        // 18 bits needed, starting at 96+14 = 110
        ops[opCount].CopyBitCount = 18;
        ops[opCount].DestinationBitOffset = 110;
        opCount++;

        ops[opCount].CopyBitCount = 16;
        ops[opCount].DestinationBitOffset = 0;
        opCount++;

        sequence.OpCount = opCount;
        memcpy(sequence.Ops, &ops, opCount * sizeof(InplaceShiftBitsOrder));
        return;
    }
    else
    {

        // Mode 7 contains 2 endpoint pairs with 40 bits each
        // E0/1 ends at byte 12, so begins at 96 - 40 = bit 56
        // E2/3 ends at byte 5, so begins at 40 - 40 = bit 0

        //   -------------------------------------------------------------------------------------   5 bytes  ----------------------------------------------------------------------------------------------------  
        //   56   57   58   59   60   61   62   63 - 64   65   66   67   68   69   70   71 - 72   73   74   76   76   77   78   79 - 80   81   82   83   84   85   86   87 - 88   89   90   91   92   93   94   95  
        //  R0'0 G0'0 B0'0 A0'0 R1'0 G1'0 B1'0 A1'0 R0'1 G0'1 B0'1 A0'1 R1'1 G1'1 B1'1 A1'1 R0'2 G0'2 B0'2 A0'2 R1'2 G1'2 B1'2 A1'2 R0'3 G0'3 B0'3 A0'3 R1'3 G1'3 B1'3 A1'3 R0'4 G0'4 B0'4 A0'4 R1'4 G1'4 B1'4 A1'4 

        //   -------------------------------------------------------------------------------------   5 bytes  ----------------------------------------------------------------------------------------------------  
        //    0    1    2    3    4    5    6    7 -  8    9   10   11   12   13   14   15 - 16   17   18   19   20   21   22   23 - 24   25   26   27   28   29   30   31 - 32   33   34   35   36   37   38   39  
        //  R2'0 G2'0 B2'0 A2'0 R3'0 G3'0 B3'0 A3'0 R2'1 G2'1 B2'1 A2'1 R3'1 G3'1 B3'1 A3'1 R2'2 G2'2 B2'2 A2'2 R3'2 G3'2 B3'2 A3'2 R2'3 G2'3 B2'3 A2'3 R3'3 G3'3 B3'3 A3'3 R2'4 G2'4 B2'4 A2'4 R3'4 G3'4 B3'4 A3'4 


        enum {
            R0 = 56,
            G0 = 57,
            B0 = 58,
            R1 = 59,
            G1 = 60,
            B1 = 61,
            A0 = 62,
            A1 = 63,

            R2 = 0,
            G2 = 1,
            B2 = 2,
            R3 = 3,
            G3 = 4,
            B3 = 5,
            A2 = 6,
            A3 = 7,
        };

        static const InplaceShiftBitsOrder ops[] = {
            {   // Mode + partition
                96,
                14
            },


            {   R0 + 8 * 0, 1 },
            {   R0 + 8 * 1, 1 },
            {   R0 + 8 * 2, 1 },
            {   R0 + 8 * 3, 1 },
            {   R0 + 8 * 4, 1 },

            {   R1 + 8 * 0, 1 },
            {   R1 + 8 * 1, 1 },
            {   R1 + 8 * 2, 1 },
            {   R1 + 8 * 3, 1 },
            {   R1 + 8 * 4, 1 },

            {   R2 + 8 * 0, 1 },
            {   R2 + 8 * 1, 1 },
            {   R2 + 8 * 2, 1 },
            {   R2 + 8 * 3, 1 },
            {   R2 + 8 * 4, 1 },

            {   R3 + 8 * 0, 1 },
            {   R3 + 8 * 1, 1 },
            {   R3 + 8 * 2, 1 },
            {   R3 + 8 * 3, 1 },
            {   R3 + 8 * 4, 1 },

            // green
            {   G0 + 8 * 0, 1 },
            {   G0 + 8 * 1, 1 },
            {   G0 + 8 * 2, 1 },
            {   G0 + 8 * 3, 1 },
            {   G0 + 8 * 4, 1 },

            {   G1 + 8 * 0, 1 },
            {   G1 + 8 * 1, 1 },
            {   G1 + 8 * 2, 1 },
            {   G1 + 8 * 3, 1 },
            {   G1 + 8 * 4, 1 },

            {   G2 + 8 * 0, 1 },
            {   G2 + 8 * 1, 1 },
            {   G2 + 8 * 2, 1 },
            {   G2 + 8 * 3, 1 },
            {   G2 + 8 * 4, 1 },

            {   G3 + 8 * 0, 1 },
            {   G3 + 8 * 1, 1 },
            {   G3 + 8 * 2, 1 },
            {   G3 + 8 * 3, 1 },
            {   G3 + 8 * 4, 1 },

            // blue
            {   B0 + 8 * 0, 1 },
            {   B0 + 8 * 1, 1 },
            {   B0 + 8 * 2, 1 },
            {   B0 + 8 * 3, 1 },
            {   B0 + 8 * 4, 1 },

            {   B1 + 8 * 0, 1 },
            {   B1 + 8 * 1, 1 },
            {   B1 + 8 * 2, 1 },
            {   B1 + 8 * 3, 1 },
            {   B1 + 8 * 4, 1 },

            {   B2 + 8 * 0, 1 },
            {   B2 + 8 * 1, 1 },
            {   B2 + 8 * 2, 1 },
            {   B2 + 8 * 3, 1 },
            {   B2 + 8 * 4, 1 },

            {   B3 + 8 * 0, 1 },
            {   B3 + 8 * 1, 1 },
            {   B3 + 8 * 2, 1 },
            {   B3 + 8 * 3, 1 },
            {   B3 + 8 * 4, 1 },

            // alpha
            {   A0 + 8 * 0, 1 },
            {   A0 + 8 * 1, 1 },
            {   A0 + 8 * 2, 1 },
            {   A0 + 8 * 3, 1 },
            {   A0 + 8 * 4, 1 },

            {   A1 + 8 * 0, 1 },
            {   A1 + 8 * 1, 1 },
            {   A1 + 8 * 2, 1 },
            {   A1 + 8 * 3, 1 },
            {   A1 + 8 * 4, 1 },

            {   A2 + 8 * 0, 1 },
            {   A2 + 8 * 1, 1 },
            {   A2 + 8 * 2, 1 },
            {   A2 + 8 * 3, 1 },
            {   A2 + 8 * 4, 1 },

            {   A3 + 8 * 0, 1 },
            {   A3 + 8 * 1, 1 },
            {   A3 + 8 * 2, 1 },
            {   A3 + 8 * 3, 1 },
            {   A3 + 8 * 4, 1 },

            {   // index to fill out the 4-Byte tail with 14 bits of existing data
                // 18 bits needed, starting at 96 + 14 = 110
                110,
                18
            },


            {   // remaiing index bits fill in the gap between the two endpoint pairs
                40,
                16          // 34 p & index bits - 18 copied above
            },
        };

        sequence.OpCount = ARRAYSIZE(ops);
        memcpy(sequence.Ops, &ops, sizeof(ops));
        return;
    }
}

void Get_BC7_ModeJoinAlt_CopyBitsOrderMode4(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics)
{
    UNREFERENCED_PARAMETER(opt);
    UNREFERENCED_PARAMETER(metrics);
    // all "join" modes are 16 bytes "color" data, but with a segmentation of 12B + 4B
    // Most color data is encoded within that 12B block, and always as endpoint pairs
    // stacking bits from least to most significant, ending in a byte boundary.


    // Alternate (Alt) encoding is intended if there's a large distribution of alpha (4\5\6\7) and non-alpha (0\1\2\3) modes.
    // This mode does not include Alpha in the endpoint pair, but splits that out.
    // Atl encodings also put a second boundary at 6 bytes, for any mode bits over 4, and for the mode7 second endpoint

    //  Bit Offset  Bits or ordering and count
    //  0           5 bits Mode (10000)
    //  5           2 bits Rotation
    //  7           1 bit Idx Mode
    //  8           5 bits R0   5 bits R1
    //  18          5 bits G0   5 bits G1
    //  28          5 bits B0   5 bits B1
    //  38          6 bits A0   6 bits A1
    //  50          31 bits Index Data
    //  81          47 bits Index Data

    // However, we actually operate off the "derotated" layout, which ensures the color data is always RGBA order:

    //  Bit Offset  Bits or ordering and count
    //  0           5 bits Mode (10000)
    //  5           2 bits Rotation
    //  7           1 bit Idx Mode
    //  8           5 bits R0   5 bits R1
    //  18          5 bits G0   5 bits G1
    //  28          5 bits B0   5 bits B1
    //  38          1 bit  ex0
    //  39          5 bits A0
    //  44                      1 bit  ex1
    //  45                      5 bits A1
    //  50          31 bits Index Data
    //  81          47 bits Index Data

    // Mode 4 contains 1 endpoint pair with 30 bits, not counting alpha, which will start at bit 100-111
    // E0/1 ends at byte 12, so begins at 96 - 30 = bit 66
    // ex0/ex1 are pushed into bits 54/55

    //   ------------------------------------------------------------   3 bytes + 6 bits   -----------------------------------------------------------------  
    //   66   67   68   69   70   71 - 72   73   74   76   76   77   78   79 - 80   81   82   83   84   85   86   87 - 88   89   90   91   92   93   94   95  
    //  R0'0 G0'0 B0'0 R1'0 G1'0 B1'0 R0'1 G0'1 B0'1 R1'1 G1'1 B1'1 R0'2 G0'2 B0'2 R1'2 G1'2 B1'2 R0'3 G0'3 B0'3 R1'3 G1'3 B1'3 R0'4 G0'4 B0'4 R1'4 G1'4 B1'4  

    //   -------------------------------   2 bytes  ----------------------------------  
    //   96   97   98   99  100  101  102  103 -104  105  106  107  108  109  110  111
    //   m'0  m'1  m'2  m'3 A1'4 A0'4 A1'3 A0'3 A1'2 A0'2 A1'1 A0'1 A1'0 A0'0 ex1  ex0

    enum {
        R0 = 66,
        G0 = 67,
        B0 = 68,
        R1 = 69,
        G1 = 70,
        B1 = 71,

        A0 = 111,
        A1 = 110,
    };

    static const InplaceShiftBitsOrder ops[] = {
        {   // Mode bit 0-3 [0000]
            96,
            4
        },

        {   // Mode bit 4 [1] & rotation & index
            48,
            4
        },

        {   R0 + 6 * 0, 1 },
        {   R0 + 6 * 1, 1 },
        {   R0 + 6 * 2, 1 },
        {   R0 + 6 * 3, 1 },
        {   R0 + 6 * 4, 1 },

        {   R1 + 6 * 0, 1 },
        {   R1 + 6 * 1, 1 },
        {   R1 + 6 * 2, 1 },
        {   R1 + 6 * 3, 1 },
        {   R1 + 6 * 4, 1 },

        // green
        {   G0 + 6 * 0, 1 },
        {   G0 + 6 * 1, 1 },
        {   G0 + 6 * 2, 1 },
        {   G0 + 6 * 3, 1 },
        {   G0 + 6 * 4, 1 },

        {   G1 + 6 * 0, 1 },
        {   G1 + 6 * 1, 1 },
        {   G1 + 6 * 2, 1 },
        {   G1 + 6 * 3, 1 },
        {   G1 + 6 * 4, 1 },

        // blue
        {   B0 + 6 * 0, 1 },
        {   B0 + 6 * 1, 1 },
        {   B0 + 6 * 2, 1 },
        {   B0 + 6 * 3, 1 },
        {   B0 + 6 * 4, 1 },

        {   B1 + 6 * 0, 1 },
        {   B1 + 6 * 1, 1 },
        {   B1 + 6 * 2, 1 },
        {   B1 + 6 * 3, 1 },
        {   B1 + 6 * 4, 1 },


        {   A0 - 2 * 0, 1 },   // A0'0   ...aka ex0
        {   A0 - 2 * 1, 1 },
        {   A0 - 2 * 2, 1 },
        {   A0 - 2 * 3, 1 },
        {   A0 - 2 * 4, 1 },
        {   A0 - 2 * 5, 1 },


        {   A1 - 2 * 0, 1 },   // A1'0   ...aka ex1
        {   A1 - 2 * 1, 1 },
        {   A1 - 2 * 2, 1 },
        {   A1 - 2 * 3, 1 },
        {   A1 - 2 * 4, 1 },
        {   A1 - 2 * 5, 1 },


        {   // index to fill out the 4-Byte tail with 16 bits of existing data
            // 16 bits needed, starting at 96 + 16 = 112
            112,
            16,
        },


        {   // remaining index bits fill in 48 bit gap before 6B boundary
            0,
            48
        },
        {   // and the section after the second mode field (48-51), and the first color data (66)
            52,
            14
        },
    };

    sequence.OpCount = ARRAYSIZE(ops);
    memcpy(sequence.Ops, &ops, sizeof(ops));
    return;
}

void Get_BC7_ModeJoinAlt_CopyBitsOrderMode5(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics)
{
    UNREFERENCED_PARAMETER(opt);
    UNREFERENCED_PARAMETER(metrics);
    // all "join" modes are 16 bytes "color" data, but with a segmentation of 12B + 4B
    // Most color data is encoded within that 12B block, and always as endpoint pairs
    // stacking bits from least to most significant, ending in a byte boundary.

    // Alternate (Alt) encoding is intended if there's a large distribution of alpha (4\5\6\7) and non-alpha (0\1\2\3) modes.
    // This mode does not include Alpha in the endpoint pair, but splits that out.
    // Atl encodings also put a second boundary at 6 bytes, for any mode bits over 4, and for the mode7 second endpoint

    //  Bit Offset  Bits or ordering and count
    //  0           6 bits Mode (100000)
    //  6           2 bits Rotation
    //  8           7 bits R0   7 bits R1
    //  22          7 bits G0   7 bits G1
    //  36          7 bits B0   7 bits B1
    //  50          8 bits A0   8 bits A1
    //  66          31 bits Color Index
    //  97          31 bits Alpha Index

    // However, we actually operate off the "derotated" layout, which ensures the color data is always RGBA order:

    //  Bit Offset  Bits or ordering and count
    //  0           6 bits Mode (100000)
    //  6           2 bits Rotation
    //  8           7 bits R0   7 bits R1
    //  22          7 bits G0   7 bits G1
    //  36          7 bits B0   7 bits B1
    //  50          1 bit ex0   
    //  51          7 bits A0   
    //  58                      1 bit ex1
    //  59                      7 bits A1
    //  66          31 bits Color Index
    //  97          31 bits Alpha Index

    // Mode 5 contains 1 endpoint pair with 42 bits, not counting alpha, which will start at bit 100-113 (two bits are relocated at de-rotation time)
    // E0/1 ends at byte 12, so begins at 96 - 42 = bit 54


    //   -----------------------------------------------------------------------------------------------   5 bytes + 2 bits   ------------------------------------------------------------------------------------------  
    //   54   55   56   57   58   59   60   61   62   63   64   65   66   67   68   69   70   71 - 72   73   74   76   76   77   78   79 - 80   81   82   83   84   85   86   87 - 88   89   90   91   92   93   94   95  
    //  R0'0 G0'0 B0'0 R1'0 G1'0 B1'0 R0'1 G0'1 B0'1 R1'1 G1'1 B1'1 R0'2 G0'2 B0'2 R1'2 G1'2 B1'2 R0'3 G0'3 B0'3 R1'3 G1'3 B1'3 R0'4 G0'4 B0'4 R1'4 G1'4 B1'4 R0'5 G0'5 B0'5 R1'5 G1'5 B1'5 R0'6 G0'6 B0'6 R1'6 G1'6 B1'6    

    //   ---------------------------------------   2 bytes + 4 bits --------------------------------------  
    //   96   97   98   99  100  101  102  103 -104  105  106  107  108  109  110  111 -112  113  114  115
    //   m'0  m'1  m'2  m'3 A1'6 A0'6 A1'5 A0'5 A1'4 A0'4 A1'3 A0'3 A1'2 A0'2 A1'1 A0'1 A1'0 A0'0 ex1  ex0

    enum {
        R0 = 54,
        G0 = 55,
        B0 = 56,
        R1 = 57,
        G1 = 58,
        B1 = 59,

        A0 = 115,
        A1 = 114,
    };

    static const InplaceShiftBitsOrder ops[] = {
        {   // Mode bits 0-3 [0000]
            96,
            4
        },
        {   // Mode bits 4-5, rotation
            48,
            4
        },

        {   R0 + 6 * 0, 1 },
        {   R0 + 6 * 1, 1 },
        {   R0 + 6 * 2, 1 },
        {   R0 + 6 * 3, 1 },
        {   R0 + 6 * 4, 1 },
        {   R0 + 6 * 5, 1 },
        {   R0 + 6 * 6, 1 },

        {   R1 + 6 * 0, 1 },
        {   R1 + 6 * 1, 1 },
        {   R1 + 6 * 2, 1 },
        {   R1 + 6 * 3, 1 },
        {   R1 + 6 * 4, 1 },
        {   R1 + 6 * 5, 1 },
        {   R1 + 6 * 6, 1 },

        // green
        {   G0 + 6 * 0, 1 },
        {   G0 + 6 * 1, 1 },
        {   G0 + 6 * 2, 1 },
        {   G0 + 6 * 3, 1 },
        {   G0 + 6 * 4, 1 },
        {   G0 + 6 * 5, 1 },
        {   G0 + 6 * 6, 1 },

        {   G1 + 6 * 0, 1 },
        {   G1 + 6 * 1, 1 },
        {   G1 + 6 * 2, 1 },
        {   G1 + 6 * 3, 1 },
        {   G1 + 6 * 4, 1 },
        {   G1 + 6 * 5, 1 },
        {   G1 + 6 * 6, 1 },

        // blue
        {   B0 + 6 * 0, 1 },
        {   B0 + 6 * 1, 1 },
        {   B0 + 6 * 2, 1 },
        {   B0 + 6 * 3, 1 },
        {   B0 + 6 * 4, 1 },
        {   B0 + 6 * 5, 1 },
        {   B0 + 6 * 6, 1 },

        {   B1 + 6 * 0, 1 },
        {   B1 + 6 * 1, 1 },
        {   B1 + 6 * 2, 1 },
        {   B1 + 6 * 3, 1 },
        {   B1 + 6 * 4, 1 },
        {   B1 + 6 * 5, 1 },
        {   B1 + 6 * 6, 1 },

        // alpha
        {   A0 - 2 * 0, 1 },  // A0'0    ....aka ex0
        {   A0 - 2 * 1, 1 },
        {   A0 - 2 * 2, 1 },
        {   A0 - 2 * 3, 1 },
        {   A0 - 2 * 4, 1 },
        {   A0 - 2 * 5, 1 },
        {   A0 - 2 * 6, 1 },
        {   A0 - 2 * 7, 1 },

        {   A1 - 2 * 0, 1 },  // A1'0    ....aka ex1
        {   A1 - 2 * 1, 1 },
        {   A1 - 2 * 2, 1 },
        {   A1 - 2 * 3, 1 },
        {   A1 - 2 * 4, 1 },
        {   A1 - 2 * 5, 1 },
        {   A1 - 2 * 6, 1 },
        {   A1 - 2 * 7, 1 },

        {   // index to fill out the 4-Byte tail with 20 bits of existing data
            // 14 bits needed, starting at 96 + 20 = 116
            116,
            12,
        },


        {   // remaining index bits fill in 48 bit gap before 6B boundary
            0,
            48
        },
        {   // and the section after the second mode field (48-51), and the first color data (54)
            52,
            2
        },
    };

    sequence.OpCount = ARRAYSIZE(ops);
    memcpy(sequence.Ops, &ops, sizeof(ops));
    return;
}

void Get_BC7_ModeJoinAlt_CopyBitsOrderMode6(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics)
{
    UNREFERENCED_PARAMETER(opt);
    // all "join" modes are 16 bytes "color" data, but with a segmentation of 12B + 4B
    // Most color data is encoded within that 12B block, and always as endpoint pairs
    // stacking bits from least to most significant, ending in a byte boundary.

    // Alternate (Alt) encoding is intended if there's a large distribution of alpha (4\5\6\7) and non-alpha (0\1\2\3) modes.
    // This mode does not include Alpha in the endpoint pair, but splits that out.
    // Atl encodings also put a second boundary at 6 bytes, for any mode bits over 4, and for the mode7 second endpoint

    //  Bit Offset  Bits or ordering and count
    //  0           7 bits Mode (1000000)
    //  7           7 bits R0   7 bits R1
    //  21          7 bits G0   7 bits G1
    //  35          7 bits B0   7 bits B1
    //  49          7 bits A0   7 bits A1
    //  63          1 bit P0    1 bit P1
    //  65          63 bits Index


    // TODO should we implement static patterning ?
    if (false) //(metrics.M[6].Statics)  BUGBUG!
    {
        // group static bits before byte 12, interleave others

        const uint8_t staticColorFields = uint8_t(_mm_popcnt_u64(metrics.M[6].Statics & 0b00111111u));
        const uint8_t staticAlphaFields = uint8_t(_mm_popcnt_u64(metrics.M[6].Statics & 0b11000000u));
        //const uint8_t nonStaticFields = 6 - staticFields;
        uint8_t nextStaticBit = 96;

        uint8_t bitSpreadColor = 6 - staticColorFields;
        uint8_t bitSpreadAlpha = 2 - staticAlphaFields;

        enum {
            R0 = 54,
            G0 = 55,
            B0 = 56,
            R1 = 57,
            G1 = 58,
            B1 = 59,

            A0 = 113,
            A1 = 112,
        };

        uint8_t fb[] = {    // first bit
            R0, R1,
            G0, G1,
            B0, B1,
            A0, A1
        };

        for (size_t cc = 0; cc < 6; cc++)  // 
        {
            if (metrics.M[6].Statics & (1 << cc))    // static bits cover color channel 6/8 bits depending on RGB[A]
            {
                nextStaticBit -= 7;

                for (int i = 0; i < _countof(fb); i++)
                {
                    if (fb[i] > fb[cc])
                    {
                        fb[i]--;
                    }
                }
            }
        }

        InplaceShiftBitsOrder ops[90] = {
            {   // Mode 0-3 
                96,
                4
            },
            {   // Mode bits 4-6 [001]
                48,
                3
            },
        };

        size_t opCount = 2;

        for (size_t epc = 0; epc < 6; epc++)  // 12 total endpoint pair color channels
        {
            if (metrics.M[6].Statics & (1 << epc))
            {
                ops[opCount].CopyBitCount = 7;
                ops[opCount].DestinationBitOffset = nextStaticBit;
                opCount++;
                nextStaticBit += 7;
            }
            else
            {
                for (uint8_t b = 0; b < 7; b++)
                {
                    ops[opCount].CopyBitCount = 1;
                    ops[opCount].DestinationBitOffset = uint8_t(fb[epc] + bitSpreadColor * b);
                    opCount++;
                }
            }
        }
        nextStaticBit = 100;
        for (size_t epc = 6; epc < 8; epc++)  // 12 total endpoint pair color channels
        {
            if (metrics.M[6].Statics & (1 << epc))
            {
                ops[opCount].CopyBitCount = 7;
                ops[opCount].DestinationBitOffset = nextStaticBit;
                opCount++;
                nextStaticBit += 7;
            }
            else
            {
                for (uint8_t b = 0; b < 7; b++)
                {
                    ops[opCount].CopyBitCount = 1;
                    ops[opCount].DestinationBitOffset = uint8_t(fb[epc] + bitSpreadAlpha * b);
                    opCount++;
                }
            }
        }

        // P bits + index to fill out the 4-Byte tail with 18 bits 
        // 14 bits needed, starting at 96+18 = 114
        ops[opCount].CopyBitCount = 14;
        ops[opCount].DestinationBitOffset = 114;
        opCount++;

        ops[opCount].CopyBitCount = 48;
        ops[opCount].DestinationBitOffset = 0;
        opCount++;

        ops[opCount].CopyBitCount = 3;
        ops[opCount].DestinationBitOffset = 51;
        opCount++;

        sequence.OpCount = opCount;
        memcpy(sequence.Ops, &ops, opCount * sizeof(InplaceShiftBitsOrder));
        return;
    }
    else
    {

        // Mode 6 contains 1 endpoint pair with 42 bits, not counting alpha, which will start at bit 100-113 
        // E0/1 ends at byte 12, so begins at 96 - 42 = bit 54

        //   -----------------------------------------------------------------------------------------------   5 bytes + 2 bits   ------------------------------------------------------------------------------------------  
        //   54   55   56   57   58   59   60   61   62   63   64   65   66   67   68   69   70   71 - 72   73   74   76   76   77   78   79 - 80   81   82   83   84   85   86   87 - 88   89   90   91   92   93   94   95  
        //  R0'0 G0'0 B0'0 R1'0 G1'0 B1'0 R0'1 G0'1 B0'1 R1'1 G1'1 B1'1 R0'2 G0'2 B0'2 R1'2 G1'2 B1'2 R0'3 G0'3 B0'3 R1'3 G1'3 B1'3 R0'4 G0'4 B0'4 R1'4 G1'4 B1'4 R0'5 G0'5 B0'5 R1'5 G1'5 B1'5 R0'6 G0'6 B0'6 R1'6 G1'6 B1'6    

        //   ----------------------------------   2 bytes + 2 bits ---------------------------------  
        //   96   97   98   99  100  101  102  103 -104  105  106  107  108  109  110  111 -112  113  
        //   m'0  m'1  m'2  m'3 A1'6 A0'6 A1'5 A0'5 A1'4 A0'4 A1'3 A0'3 A1'2 A0'2 A1'1 A0'1 A1'0 A0'0 

        enum {
            R0 = 54,
            G0 = 55,
            B0 = 56,
            R1 = 57,
            G1 = 58,
            B1 = 59,

            A0 = 113,
            A1 = 112,
        };

        static const InplaceShiftBitsOrder ops[] = {
            {   // Mode bits 0-3 [0000]
                96,
                4
            },
            {   // Mode bits 4-6 [001]
                48,
                3
            },


            {   R0 + 6 * 0, 1 },
            {   R0 + 6 * 1, 1 },
            {   R0 + 6 * 2, 1 },
            {   R0 + 6 * 3, 1 },
            {   R0 + 6 * 4, 1 },
            {   R0 + 6 * 5, 1 },
            {   R0 + 6 * 6, 1 },

            {   R1 + 6 * 0, 1 },
            {   R1 + 6 * 1, 1 },
            {   R1 + 6 * 2, 1 },
            {   R1 + 6 * 3, 1 },
            {   R1 + 6 * 4, 1 },
            {   R1 + 6 * 5, 1 },
            {   R1 + 6 * 6, 1 },

            // green
            {   G0 + 6 * 0, 1 },
            {   G0 + 6 * 1, 1 },
            {   G0 + 6 * 2, 1 },
            {   G0 + 6 * 3, 1 },
            {   G0 + 6 * 4, 1 },
            {   G0 + 6 * 5, 1 },
            {   G0 + 6 * 6, 1 },

            {   G1 + 6 * 0, 1 },
            {   G1 + 6 * 1, 1 },
            {   G1 + 6 * 2, 1 },
            {   G1 + 6 * 3, 1 },
            {   G1 + 6 * 4, 1 },
            {   G1 + 6 * 5, 1 },
            {   G1 + 6 * 6, 1 },

            // blue
            {   B0 + 6 * 0, 1 },
            {   B0 + 6 * 1, 1 },
            {   B0 + 6 * 2, 1 },
            {   B0 + 6 * 3, 1 },
            {   B0 + 6 * 4, 1 },
            {   B0 + 6 * 5, 1 },
            {   B0 + 6 * 6, 1 },

            {   B1 + 6 * 0, 1 },
            {   B1 + 6 * 1, 1 },
            {   B1 + 6 * 2, 1 },
            {   B1 + 6 * 3, 1 },
            {   B1 + 6 * 4, 1 },
            {   B1 + 6 * 5, 1 },
            {   B1 + 6 * 6, 1 },


            // alpha
            {   A0 - 2 * 0, 1 },
            {   A0 - 2 * 1, 1 },
            {   A0 - 2 * 2, 1 },
            {   A0 - 2 * 3, 1 },
            {   A0 - 2 * 4, 1 },
            {   A0 - 2 * 5, 1 },
            {   A0 - 2 * 6, 1 },

            {   A1 - 2 * 0, 1 },
            {   A1 - 2 * 1, 1 },
            {   A1 - 2 * 2, 1 },
            {   A1 - 2 * 3, 1 },
            {   A1 - 2 * 4, 1 },
            {   A1 - 2 * 5, 1 },
            {   A1 - 2 * 6, 1 },


            {   // index to fill out the 4-Byte tail with 18 bits of existing data
                // 14 bits needed, starting at 96 + 18 = 114
                114,
                14,
            },


            {   // remaining index bits fill in 48 bit gap before 6B boundary
                0,
                48
            },
            {   // and the section after the second mode field (48-51), and the first color data (54)
                51,
                3
            },
        };

        sequence.OpCount = ARRAYSIZE(ops);
        memcpy(sequence.Ops, &ops, sizeof(ops));;
        return;
    }
}

void Get_BC7_ModeJoinAlt_CopyBitsOrderMode7(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics)
{
    UNREFERENCED_PARAMETER(opt);
    UNREFERENCED_PARAMETER(metrics);
    // all "join" modes are 16 bytes "color" data, but with a segmentation of 12B + 4B
    // Most color data is encoded within that 12B block, and always as endpoint pairs
    // stacking bits from least to most significant, ending in a byte boundary.

    //  Bit Offset  Bits or ordering and count
    //  0           8 bits Mode (10000000)
    //  6           6 bits Partition
    //  14          5 bits R0   5 bits R1   5 bits R2   5 bits R3
    //  34          5 bits G0   5 bits G1   5 bits G2   5 bits G3
    //  54          5 bits B0   5 bits B1   5 bits B2   5 bits B3
    //  74          5 bits A0   5 bits A1   5 bits A2   5 bits A3
    //  94          1 bit P0    1 bit P1    1 bit P2    1 bit P2
    //  98          30 bits Index

    // Mode 4 contains 2 endpoint pairs with 30 bits each, not counting alpha
    // E0/1 ends at byte 12, so begins at 96 - 30 = bit 66     alpha is 100 - 109
    // E2/3 ends at byte 6, so begins at 48 - 30 = bit 18      alpha is 52 - 61

    //   ------------------------------------------------------------   3 bytes + 6 bits   -----------------------------------------------------------------  
    //   66   67   68   69   70   71 - 72   73   74   76   76   77   78   79 - 80   81   82   83   84   85   86   87 - 88   89   90   91   92   93   94   95  
    //  R0'0 G0'0 B0'0 R1'0 G1'0 B1'0 R0'1 G0'1 B0'1 R1'1 G1'1 B1'1 R0'2 G0'2 B0'2 R1'2 G1'2 B1'2 R0'3 G0'3 B0'3 R1'3 G1'3 B1'3 R0'4 G0'4 B0'4 R1'4 G1'4 B1'4  

    //   ---------------------   1 bytes + 6 bits  -------------------------  
    //   96   97   98   99  100  101  102  103 -104  105  106  107  108  109  
    //   m'0  m'1  m'2  m'3 A1'4 A0'4 A1'3 A0'3 A1'2 A0'2 A1'1 A0'1 A1'0 A0'0 

    //   ------------------------------------------------------------   3 bytes + 6 bits   -----------------------------------------------------------------  
    //   18   19   20   21   22   23 - 24   25   26   27   28   29   30   31 - 32   33   34   35   36   37   38   39 - 40   41   42   43   44   45   46   47  
    //  R2'0 G2'0 B2'0 R3'0 G3'0 B3'0 R2'1 G2'1 B2'1 R3'1 G3'1 B3'1 R2'2 G2'2 B2'2 R3'2 G3'2 B3'2 R2'3 G2'3 B2'3 R3'3 G3'3 B3'3 R2'4 G2'4 B2'4 R3'4 G3'4 B3'4  

    //   ---------------------   1 bytes + 6 bits  -------------------------  
    //   48   49   50   51   52   53   54   55 - 56   57   58   59   60   61  
    //   m'4  m'5  m'6  m'7 A3'4 A2'4 A3'3 A2'3 A3'2 A2'2 A3'1 A2'1 A3'0 A2'0 


    enum {
        R0 = 66,
        G0 = 67,
        B0 = 68,
        R1 = 69,
        G1 = 70,
        B1 = 71,

        A0 = 109,
        A1 = 108,

        R2 = 18,
        G2 = 19,
        B2 = 20,
        R3 = 21,
        G3 = 22,
        B3 = 23,

        A2 = 61,
        A3 = 60,
    };

    static const InplaceShiftBitsOrder ops[] = {
        {   // Mode bits 0-3 [0000]
            96,
            4
        },
        {   // Mode bits 4-7 [0001] 
            48,
            4
        },

        {   // Partition bits 0-3
            62,
            4
        },
        {   // Partition bits 4-5
            16,
            2
        },

        {   R0 + 6 * 0, 1 },
        {   R0 + 6 * 1, 1 },
        {   R0 + 6 * 2, 1 },
        {   R0 + 6 * 3, 1 },
        {   R0 + 6 * 4, 1 },

        {   R1 + 6 * 0, 1 },
        {   R1 + 6 * 1, 1 },
        {   R1 + 6 * 2, 1 },
        {   R1 + 6 * 3, 1 },
        {   R1 + 6 * 4, 1 },

        {   R2 + 6 * 0, 1 },
        {   R2 + 6 * 1, 1 },
        {   R2 + 6 * 2, 1 },
        {   R2 + 6 * 3, 1 },
        {   R2 + 6 * 4, 1 },

        {   R3 + 6 * 0, 1 },
        {   R3 + 6 * 1, 1 },
        {   R3 + 6 * 2, 1 },
        {   R3 + 6 * 3, 1 },
        {   R3 + 6 * 4, 1 },


        // green
        {   G0 + 6 * 0, 1 },
        {   G0 + 6 * 1, 1 },
        {   G0 + 6 * 2, 1 },
        {   G0 + 6 * 3, 1 },
        {   G0 + 6 * 4, 1 },

        {   G1 + 6 * 0, 1 },
        {   G1 + 6 * 1, 1 },
        {   G1 + 6 * 2, 1 },
        {   G1 + 6 * 3, 1 },
        {   G1 + 6 * 4, 1 },

        {   G2 + 6 * 0, 1 },
        {   G2 + 6 * 1, 1 },
        {   G2 + 6 * 2, 1 },
        {   G2 + 6 * 3, 1 },
        {   G2 + 6 * 4, 1 },

        {   G3 + 6 * 0, 1 },
        {   G3 + 6 * 1, 1 },
        {   G3 + 6 * 2, 1 },
        {   G3 + 6 * 3, 1 },
        {   G3 + 6 * 4, 1 },


        // blue
        {   B0 + 6 * 0, 1 },
        {   B0 + 6 * 1, 1 },
        {   B0 + 6 * 2, 1 },
        {   B0 + 6 * 3, 1 },
        {   B0 + 6 * 4, 1 },

        {   B1 + 6 * 0, 1 },
        {   B1 + 6 * 1, 1 },
        {   B1 + 6 * 2, 1 },
        {   B1 + 6 * 3, 1 },
        {   B1 + 6 * 4, 1 },

        {   B2 + 6 * 0, 1 },
        {   B2 + 6 * 1, 1 },
        {   B2 + 6 * 2, 1 },
        {   B2 + 6 * 3, 1 },
        {   B2 + 6 * 4, 1 },

        {   B3 + 6 * 0, 1 },
        {   B3 + 6 * 1, 1 },
        {   B3 + 6 * 2, 1 },
        {   B3 + 6 * 3, 1 },
        {   B3 + 6 * 4, 1 },

        // alpha
        {   A0 - 2 * 0, 1 },
        {   A0 - 2 * 1, 1 },
        {   A0 - 2 * 2, 1 },
        {   A0 - 2 * 3, 1 },
        {   A0 - 2 * 4, 1 },

        {   A1 - 2 * 0, 1 },
        {   A1 - 2 * 1, 1 },
        {   A1 - 2 * 2, 1 },
        {   A1 - 2 * 3, 1 },
        {   A1 - 2 * 4, 1 },

        {   A2 - 2 * 0, 1 },
        {   A2 - 2 * 1, 1 },
        {   A2 - 2 * 2, 1 },
        {   A2 - 2 * 3, 1 },
        {   A2 - 2 * 4, 1 },

        {   A3 - 2 * 0, 1 },
        {   A3 - 2 * 1, 1 },
        {   A3 - 2 * 2, 1 },
        {   A3 - 2 * 3, 1 },
        {   A3 - 2 * 4, 1 },


        {   // fill in the 4 byte tail with 14 bits already mapped
            // 18 bits needed, starting at 96 + 14 = 110
            110,
            18,
        },
        {   // remaining index bits fill in 16 bit gap before the first color point
            0,
            16
        },
    };

    sequence.OpCount = ARRAYSIZE(ops);
    memcpy(sequence.Ops, &ops, sizeof(ops));
    return;
}



void (*BC7_ModeJoin_Shuffle_OpLists[8])(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics) =
{
    &Get_BC7_ModeJoin_CopyBitsOrderMode0,
    &Get_BC7_ModeJoin_CopyBitsOrderMode1,
    &Get_BC7_ModeJoin_CopyBitsOrderMode2,
    &Get_BC7_ModeJoin_CopyBitsOrderMode3,
    &Get_BC7_ModeJoin_CopyBitsOrderMode4,
    &Get_BC7_ModeJoin_CopyBitsOrderMode5,
    &Get_BC7_ModeJoin_CopyBitsOrderMode6,
    &Get_BC7_ModeJoin_CopyBitsOrderMode7
};

void (*BC7_ModeJoinAlt_Shuffle_OpLists[8])(InplaceShiftBitsSequence& sequence, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics) =
{
    &Get_BC7_ModeJoin_CopyBitsOrderMode0,
    &Get_BC7_ModeJoin_CopyBitsOrderMode1,
    &Get_BC7_ModeJoin_CopyBitsOrderMode2,
    &Get_BC7_ModeJoin_CopyBitsOrderMode3,
    &Get_BC7_ModeJoinAlt_CopyBitsOrderMode4,
    &Get_BC7_ModeJoinAlt_CopyBitsOrderMode5,
    &Get_BC7_ModeJoinAlt_CopyBitsOrderMode6,
    &Get_BC7_ModeJoinAlt_CopyBitsOrderMode7
};


void BC7_ModeJoin_Shuffle_Fast_Mode0(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = metrics.M[0].Statics & 0x3f;
    const BC7m0* b = reinterpret_cast<const BC7m0*>(src);
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    uint64_t eppData[2] = { 0, 0 };
    if (statics)
    {
        if (staticCount == 6) // All color channel pairs are static - simple copy case
        {
            eppData[0] = (b->Raw[0] >> 5) | (b->Raw[1] << 59);
            eppData[1] = ((b->Raw[1] >> 5) & 0xff);
        }
        else
        {
            // Mixed static/dynamic fields - need to separate and reorganize
            // Precomputed masks handle interleaving patterns for different field counts
            const uint8_t nonStaticCount = 6 - staticCount;

            // Separate fields into static and non-static lists for processing
            uint8_t staticFields[18] = {};
            uint8_t nonStaticFields[18] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Group endpoint pairs by static/dynamic status
            // Each field covers 3 endpoint pairs (e.g., R0+R2+R4, R1+R3+R5)
            for (uint8_t field = 0; field < 6; field++)
            {
                uint8_t epp0Index = field + (field - field % 2) * 2;
                uint8_t epp1Index = epp0Index + 2;
                uint8_t epp2Index = epp0Index + 4;
                if (statics & (1 << field))
                {
                    staticFields[staticIndex++] = epp0Index;
                    staticFields[staticIndex++] = epp1Index;
                    staticFields[staticIndex++] = epp2Index;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = epp0Index;
                    nonStaticFields[nonStaticIndex++] = epp1Index;
                    nonStaticFields[nonStaticIndex++] = epp2Index;
                }
            }

            // Extract all field values from the source block
            const uint64_t fieldValues[18] = { b->R0, b->R1, b->R2, b->R3, b->R4, b->R5,
                                              b->G0, b->G1, b->G2, b->G3, b->G4, b->G5,
                                              b->B0, b->B1, b->B2(), b->B3, b->B4, b->B5 };

            // Reorder mapping for optimal shuffled layout (endpoint pair ordering)
            const uint8_t mapToShuffledOrder[18] = { 0, 3, 6, 9, 12, 15, 1, 4, 7, 10, 13, 16, 2, 5, 8, 11, 14, 17 };

            // Sort non-static fields by their target shuffled positions
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount * 3,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Keep static fields in original order for contiguous placement
            std::sort(staticFields, staticFields + staticCount * 3);

            // Build interleaved endpoint data for non-static fields using bit deposition
            uint64_t nonStaticEpp = 0;
            if (nonStaticCount > 0)
            {
                const uint8_t totalNonStaticFields = nonStaticCount * 3;
                const uint64_t mask = maskTable[totalNonStaticFields][0];
                for (uint8_t i = 0; i < totalNonStaticFields; i++)
                {
                    const uint8_t fieldIdx = nonStaticFields[i];
                    nonStaticEpp |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                }
            }

            // Pack static fields consecutively (4 bits each)
            uint64_t staticData[2] = { 0, 0 };
            for (uint8_t i = 0; i < uint8_t(staticCount * 3); i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                if (i * 4 < 64)
                {
                    staticData[0] |= (uint64_t(fieldValues[fieldIdx]) << (i * 4));
                    if (i * 4 > 60)
                    {
                        staticData[1] |= (uint64_t(fieldValues[fieldIdx]) >> (64 - i * 4));
                    }
                }
                else
                {
                    staticData[1] |= (uint64_t(fieldValues[fieldIdx]) << (i * 4 - 64));
                }
            }

            // Combine interleaved non-static data with packed static data
            const uint8_t nonStaticBits = nonStaticCount * 3 * 4;

            eppData[0] = nonStaticEpp | (staticData[0] << nonStaticBits);
            eppData[1] = (staticData[0] >> (64 - nonStaticBits)) | (staticData[1] << nonStaticBits);
        }

        uint64_t extra = b->Raw[1] >> 13;                     // Extract P-bits and index data (51 bits)
        uint64_t mode_part = (b->Raw[0] & 0x1f);                     // Extract mode and partition bits (5 bits)

        // Assemble final shuffled layout with endpoint data positioned optimally
        d[0] = (extra >> 27) |
            (eppData[0] << 24);

        d[1] = (eppData[0] >> 40) |
            (eppData[1] << 24) |
            (mode_part << 32) |
            (extra << 37);
    }
    else
    {
        // No static fields - use optimized parallel bit extraction for all dynamic fields
        // Each mask extracts specific bit positions for R/G/B channels across endpoint pairs
        //                                     1'3   1'2   1'1   1'0      0'3   0'2   0'1   0'0
        const uint64_t rm = 0b0000000000000000001000001000001000001000000001000001000001000001ull;
        const uint64_t gm = 0b0000000000000000010000010000010000010000000010000010000010000010ull;
        const uint64_t bm = 0b0000000000000000100000100000100000100000000100000100000100000100ull;
        //                    \______/\______/\______/\______/\______/\______/\______/\______/ 

        BC7m0 be = *reinterpret_cast<const BC7m0*>(src);
        // Extract raw color bit fields from source
        uint64_t rBits = be.Raw[0] >> 5;
        uint64_t gBits = be.Raw[0] >> 29;
        uint64_t bBits = __shiftright128(be.Raw[0], be.Raw[1], 53);

        // Prepare destination with mode/partition and remaining index bits
        uint64_t de[2] = {
            be.Raw[1] >> 40,                    // Place remaining 24 index bits
            ((be.Raw[0] & 0x1f) << 32) |        // Position mode & partition at byte boundary
            ((be.Raw[1] & ~0x1fffull) << 24)    // Position first index bits (27 bits from offset 13)
        };

        // Use parallel bit deposit to interleave color values efficiently
        uint64_t eppp0 =
            _pdep_u64(rBits, rm) |
            _pdep_u64(gBits, gm) |
            _pdep_u64(bBits, bm);
        de[0] |= (0xFFFFFF000000ul & (eppp0 | eppp0 << 24));

        uint64_t eppp1 =
            _pdep_u64(rBits >> 8, rm) |
            _pdep_u64(gBits >> 8, gm) |
            _pdep_u64(bBits >> 8, bm);
        eppp1 = (0xFFFFFFul & (eppp1 >> 24 | eppp1));
        de[0] |= (eppp1 << 48);

        uint64_t eppp2 =
            _pdep_u64(rBits >> 16, rm) |
            _pdep_u64(gBits >> 16, gm) |
            _pdep_u64(bBits >> 16, bm);
        eppp2 = (0xFFFFFFul & (eppp2 >> 24 | eppp2));
        de[1] |= (eppp1 >> 16) | (eppp2 << 8);

        d[0] = de[0];
        d[1] = de[1];
    }
}

void BC7_ModeJoin_Shuffle_Fast_Mode1(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = metrics.M[1].Statics & 0x3f;
    const BC7m1* b = reinterpret_cast<const BC7m1*>(src);
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    uint64_t eppData[2] = { 0, 0 };
    if (statics)
    {
        if (staticCount == 6) // All color channel pairs are static - direct extraction
        {
            eppData[0] = (b->Raw[0] >> 8) | (b->Raw[1] << 56);
            eppData[1] = ((b->Raw[1] >> 8) & 0xff);
        }
        else
        {
            // Mixed static/dynamic case requiring field separation and reorganization
            const uint8_t nonStaticCount = 6 - staticCount;

            // Build separate lists for static and dynamic field processing
            uint8_t staticFields[12] = {};
            uint8_t nonStaticFields[12] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Classify fields based on static mask (2 endpoint pairs per field)
            for (uint8_t field = 0; field < 6; field++)
            {
                uint8_t epp0Index = (field % 2 ? field * 2 - 1 : field * 2);
                uint8_t epp1Index = epp0Index + 2;
                if (statics & (1 << field))
                {
                    staticFields[staticIndex++] = epp0Index;
                    staticFields[staticIndex++] = epp1Index;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = epp0Index;
                    nonStaticFields[nonStaticIndex++] = epp1Index;
                }
            }

            // Extract field values from source block structure
            const uint64_t fieldValues[12] = { b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3, b->B0, b->B1(), b->B2, b->B3 };

            // Mapping for optimal endpoint pair ordering in shuffled output
            const uint8_t mapToShuffledOrder[12] = { 0, 3, 6, 9, 1, 4, 7, 10, 2, 5, 8, 11 };

            // Sort non-static fields by their target positions for optimal layout
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount * 2,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Maintain original order for static fields (contiguous placement)
            std::sort(staticFields, staticFields + staticCount * 2);

            // Create interleaved endpoint pattern for non-static fields
            uint64_t nonStaticEpp = 0;
            if (nonStaticCount > 0)
            {
                const uint8_t totalNonStaticFields = nonStaticCount * 2;
                const uint64_t mask = maskTable[totalNonStaticFields][0];
                for (uint8_t i = 0; i < totalNonStaticFields; i++)
                {
                    const uint8_t fieldIdx = nonStaticFields[i];
                    nonStaticEpp |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                }
            }

            // Pack static fields consecutively (6 bits each for Mode 1)
            uint64_t staticData[2] = { 0, 0 };
            for (uint8_t i = 0; i < uint8_t(staticCount * 2); i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                if (i * 6 < 64)
                {
                    staticData[0] |= (uint64_t(fieldValues[fieldIdx]) << (i * 6));
                    if (i * 6 > 58)
                    {
                        staticData[1] |= (uint64_t(fieldValues[fieldIdx]) >> (64 - i * 6));
                    }
                }
                else
                {
                    staticData[1] |= (uint64_t(fieldValues[fieldIdx]) << (i * 6 - 64));
                }
            }

            // Combine interleaved and static data with proper bit alignment
            const uint8_t nonStaticBits = nonStaticCount * 2 * 6;

            eppData[0] = nonStaticEpp | (staticData[0] << nonStaticBits);
            eppData[1] = (staticData[0] >> (64 - nonStaticBits)) | (staticData[1] << nonStaticBits);
        }

        uint64_t extra = b->Raw[1] >> 16;                     // Extract P-bits and index data (48 bits)
        uint64_t mode_part = (b->Raw[0] & 0xff);                     // Extract mode and partition (8 bits)

        // Assemble final layout with endpoint data positioned for compression efficiency
        d[0] = (extra >> 24) |
            (eppData[0] << 24);

        d[1] = (eppData[0] >> 40) |
            (eppData[1] << 24) |
            (mode_part << 32) |
            (extra << 40);
    }
    else
    {
        // All fields dynamic - use parallel extraction with optimized bit masks
        // Each endpoint pair uses 36 bits (6 bits/channel * 3 channels * 2 endpoints)
        // Masks define interleaving pattern with 6-bit spacing between field bits
        const uint64_t r0m = 0b000001000001000001000001000001000001;
        const uint64_t g0m = 0b000010000010000010000010000010000010;
        const uint64_t b0m = 0b000100000100000100000100000100000100;
        const uint64_t r1m = 0b001000001000001000001000001000001000;
        const uint64_t g1m = 0b010000010000010000010000010000010000;
        const uint64_t b1m = 0b100000100000100000100000100000100000;

        // Create endpoint pair 0 using parallel bit deposit for optimal interleaving
        uint64_t epp0 =
            _pdep_u64(b->R0, r0m) |
            _pdep_u64(b->R1, r1m) |
            _pdep_u64(b->G0, g0m) |
            _pdep_u64(b->G1, g1m) |
            _pdep_u64(b->B0, b0m) |
            _pdep_u64(b->B1(), b1m);

        // Create endpoint pair 1 with same interleaving pattern
        uint64_t epp1 =
            _pdep_u64(b->R2, r0m) |
            _pdep_u64(b->R3, r1m) |
            _pdep_u64(b->G2, g0m) |
            _pdep_u64(b->G3, g1m) |
            _pdep_u64(b->B2, b0m) |
            _pdep_u64(b->B3, b1m);

        // Position mode/partition at byte boundary (bit 96) for compression efficiency
        uint64_t extra = b->Raw[1] >> 16;                     // Extract P-bits and index (48 bits)
        uint64_t mode_part = (b->Raw[0] & 0xff);                     // Extract mode and partition (8 bits)

        // Assemble with endpoint pairs positioned to fill 12-byte boundary optimally
        d[0] = ((extra >> 24) & 0xfff) |
            (epp1 << 12) |
            ((extra >> 36) << 48) |
            (epp0 << 60);

        d[1] = (epp0 >> 4) |
            (mode_part << 32) |
            (extra << 40);
    }
}

void BC7_ModeJoin_Shuffle_Fast_Mode2(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = metrics.M[2].Statics & 0x3f;
    const BC7m2* b = reinterpret_cast<const BC7m2*>(src);
    //uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    uint64_t eppData[2] = { 0, 0 };
    if (statics)
    {
        if (staticCount == 6) // All color fields static - simple copy optimization
        {
            eppData[0] = (b->Raw[0] >> 9) | (b->Raw[1] << 55);
            eppData[1] = ((b->Raw[1] >> 9) & 0x3ffffff);
        }
        else
        {
            // Mixed static/dynamic requiring complex field reorganization
            const uint8_t nonStaticCount = 6 - staticCount;

            // Separate processing lists for static vs dynamic fields
            uint8_t staticFields[18] = {};
            uint8_t nonStaticFields[18] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Mode 2 has 3 endpoint pairs per color field grouping
            for (uint8_t field = 0; field < 6; field++)
            {
                uint8_t epp0Index = field + (field - field % 2) * 2;
                uint8_t epp1Index = epp0Index + 2;
                uint8_t epp2Index = epp0Index + 4;
                if (statics & (1 << field))
                {
                    staticFields[staticIndex++] = epp0Index;
                    staticFields[staticIndex++] = epp1Index;
                    staticFields[staticIndex++] = epp2Index;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = epp0Index;
                    nonStaticFields[nonStaticIndex++] = epp1Index;
                    nonStaticFields[nonStaticIndex++] = epp2Index;
                }
            }

            // Extract 5-bit color values from all endpoint positions
            const uint64_t fieldValues[18] = { b->R0, b->R1, b->R2, b->R3, b->R4, b->R5,
                                              b->G0, b->G1, b->G2, b->G3, b->G4, b->G5,
                                              b->B0, b->B1, b->B2, b->B3, b->B4, b->B5 };

            // Endpoint pair ordering for optimal shuffled layout
            const uint8_t mapToShuffledOrder[18] = { 0, 3, 6, 9, 12, 15, 1, 4, 7, 10, 13, 16, 2, 5, 8, 11, 14, 17 };

            // Sort fields by target shuffled positions for layout optimization
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount * 3,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Keep static fields in original order for contiguous placement
            std::sort(staticFields, staticFields + staticCount * 3);

            // Build interleaved pattern for non-static fields (may span two uint64s)
            uint64_t nonStaticEpp[2] = { 0, 0 };
            if (nonStaticCount > 0)
            {
                const uint8_t totalNonStaticFields = nonStaticCount * 3;

                if (totalNonStaticFields <= 12)  // Fits in single uint64 with interleaving
                {
                    const uint64_t mask = maskTable[totalNonStaticFields][0];
                    for (uint8_t i = 0; i < totalNonStaticFields; i++)
                    {
                        const uint8_t fieldIdx = nonStaticFields[i];
                        nonStaticEpp[0] |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                    }
                }
                else  // More fields require spanning two uint64s with split masks
                {
                    const uint64_t mask15_0 = maskTable[15][0];
                    const uint64_t mask15_1 = maskTable[15][1];

                    // Process using split mask approach for >60 bit patterns
                    for (uint8_t i = 0; i < totalNonStaticFields; i++)
                    {
                        const uint8_t fieldIdx = nonStaticFields[i];
                        const uint64_t fieldValue = fieldValues[fieldIdx];

                        // Deposit first 4 bits using primary mask
                        nonStaticEpp[0] |= _pdep_u64(fieldValue, mask15_0 << i);

                        // Deposit 5th bit using secondary mask for overflow
                        if (i > 3)
                            nonStaticEpp[1] |= _pdep_u64((fieldValue >> 4u) & 0x1u, mask15_1 >> (15u - i));
                    }
                }
            }

            // Pack static fields consecutively (5 bits each for Mode 2)
            uint64_t staticData[2] = { 0, 0 };
            for (uint8_t i = 0; i < uint8_t(staticCount * 3); i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                if (i * 5 < 64)
                {
                    staticData[0] |= (uint64_t(fieldValues[fieldIdx]) << (i * 5));
                    if (i * 5 > 59)
                    {
                        staticData[1] |= (uint64_t(fieldValues[fieldIdx]) >> (64 - i * 5));
                    }
                }
                else
                {
                    staticData[1] |= (uint64_t(fieldValues[fieldIdx]) << (i * 5 - 64));
                }
            }

            // Combine data with proper alignment for >64 bit cases
            const uint8_t nonStaticBits = nonStaticCount * 3 * 5;

            if (nonStaticBits > 64)
            {
                eppData[0] = nonStaticEpp[0];
                eppData[1] = nonStaticEpp[1] | (staticData[0] << (nonStaticBits - 64));
            }
            else
            {
                eppData[0] = nonStaticEpp[0] | (staticData[0] << nonStaticBits);
                eppData[1] = (staticData[0] >> (64 - nonStaticBits)) | (staticData[1] << nonStaticBits);
            }
        }

        uint64_t* d = reinterpret_cast<uint64_t*>(dest);
        uint64_t extra = b->Raw[1] >> 35;                     // Extract index data (29 bits)
        uint64_t mode_part = (b->Raw[0] & 0x1ff);                     // Extract mode and partition (9 bits)

        // Position data for optimal compression with byte-aligned mode placement
        d[0] = (extra >> 23) |
            (eppData[0] << 6);

        d[1] = (eppData[0] >> 58) |
            (eppData[1] << 6) |
            (mode_part << 32) |
            (extra << 41);
    }
    else
    {
        // All fields dynamic - use parallel bit extraction for 3 endpoint pairs
        // Each endpoint pair is 30 bits (5 bits/channel * 3 channels * 2 endpoints)
        // 6-bit spacing accommodates all field bits in interleaved pattern
        const uint64_t r0m = 0b000001000001000001000001000001;
        const uint64_t g0m = 0b000010000010000010000010000010;
        const uint64_t b0m = 0b000100000100000100000100000100;
        const uint64_t r1m = 0b001000001000001000001000001000;
        const uint64_t g1m = 0b010000010000010000010000010000;
        const uint64_t b1m = 0b100000100000100000100000100000;

        // Create all three endpoint pairs using parallel bit deposit
        uint64_t epp0 =
            _pdep_u64(b->R0, r0m) |
            _pdep_u64(b->R1, r1m) |
            _pdep_u64(b->G0, g0m) |
            _pdep_u64(b->G1, g1m) |
            _pdep_u64(b->B0, b0m) |
            _pdep_u64(b->B1, b1m);

        uint64_t epp1 =
            _pdep_u64(b->R2, r0m) |
            _pdep_u64(b->R3, r1m) |
            _pdep_u64(b->G2, g0m) |
            _pdep_u64(b->G3, g1m) |
            _pdep_u64(b->B2, b0m) |
            _pdep_u64(b->B3, b1m);

        uint64_t epp2 =
            _pdep_u64(b->R4, r0m) |
            _pdep_u64(b->R5, r1m) |
            _pdep_u64(b->G4, g0m) |
            _pdep_u64(b->G5, g1m) |
            _pdep_u64(b->B4, b0m) |
            _pdep_u64(b->B5, b1m);

        // Position mode at byte boundary and distribute endpoint pairs efficiently
        uint64_t* d = reinterpret_cast<uint64_t*>(dest);
        uint64_t extra = b->Raw[1] >> 35;                     // Extract index data (29 bits)
        uint64_t mode_part = (b->Raw[0] & 0x1ff);                     // Extract mode and partition (9 bits)

        // Pack endpoint pairs with gaps filled by index data fragments
        d[0] = ((extra >> 23) & 0x3) |
            (epp2 << 2) |
            (((extra >> 25) & 0x3) << 32) |
            (epp1 << 34);

        d[1] = (extra >> 27) |
            (epp0 << 2) |
            (mode_part << 32) |
            (extra << 41);
    }
}

void BC7_ModeJoin_Shuffle_Fast_Mode3(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = metrics.M[3].Statics & 0x3f;
    const BC7m3* b = reinterpret_cast<const BC7m3*>(src);
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    uint64_t eppData[2] = { 0, 0 };
    if (statics)
    {
        if (staticCount == 6) // All color fields static - optimized direct copy
        {
            eppData[0] = (b->Raw[0] >> 10) | (b->Raw[1] << 54);
            eppData[1] = ((b->Raw[1] >> 10) & 0xfffff);
        }
        else
        {
            // Mixed static/dynamic fields requiring reorganization
            const uint8_t nonStaticCount = 6 - staticCount;

            // Build processing lists for static vs dynamic field handling
            uint8_t staticFields[12] = {};
            uint8_t nonStaticFields[12] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Mode 3 has 2 endpoint pairs per color field grouping
            for (uint8_t field = 0; field < 6; field++)
            {
                uint8_t epp0Index = (field % 2 ? field * 2 - 1 : field * 2);
                uint8_t epp1Index = epp0Index + 2;
                if (statics & (1 << field))
                {
                    staticFields[staticIndex++] = epp0Index;
                    staticFields[staticIndex++] = epp1Index;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = epp0Index;
                    nonStaticFields[nonStaticIndex++] = epp1Index;
                }
            }

            // Extract 7-bit color values from all endpoint positions
            const uint64_t fieldValues[12] = { b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3(), b->B0, b->B1, b->B2, b->B3 };

            // Optimal endpoint pair ordering for shuffled layout
            const uint8_t mapToShuffledOrder[12] = { 0, 3, 6, 9, 1, 4, 7, 10, 2, 5, 8, 11 };

            // Sort fields by target positions for layout optimization
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount * 2,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Maintain original order for static fields
            std::sort(staticFields, staticFields + staticCount * 2);

            // Create interleaved pattern for non-static fields (may need two uint64s)
            uint64_t nonStaticEpp[2] = { 0, 0 };
            if (nonStaticCount > 0)
            {
                const uint8_t totalNonStaticFields = nonStaticCount * 2;

                if (totalNonStaticFields <= 8)  // Fits in single uint64 with 7-bit spacing
                {
                    const uint64_t mask = maskTable[totalNonStaticFields][0];
                    for (uint8_t i = 0; i < totalNonStaticFields; i++)
                    {
                        const uint8_t fieldIdx = nonStaticFields[i];
                        nonStaticEpp[0] |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                    }
                }
                else  // Requires spanning two uint64s with split mask handling
                {
                    const uint64_t mask10_0 = maskTable[10][0];
                    const uint64_t mask10_1 = maskTable[10][1];

                    // Process with split masks for fields exceeding 64-bit capacity
                    for (uint8_t i = 0; i < totalNonStaticFields; i++)
                    {
                        const uint8_t fieldIdx = nonStaticFields[i];
                        const uint64_t fieldValue = fieldValues[fieldIdx];

                        // Deposit first 6 bits using primary mask
                        nonStaticEpp[0] |= _pdep_u64(fieldValue, mask10_0 << i);

                        // Deposit 7th bit using secondary mask for overflow
                        if (i > 3)
                            nonStaticEpp[1] |= _pdep_u64((fieldValue >> 6u) & 0x1u, mask10_1 >> (10u - i));
                    }
                }
            }

            // Pack static fields consecutively (7 bits each for Mode 3)
            uint64_t staticData[2] = { 0, 0 };
            for (uint8_t i = 0; i < uint8_t(staticCount * 2); i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                if (i * 7 < 64)
                {
                    staticData[0] |= (uint64_t(fieldValues[fieldIdx]) << (i * 7));
                    if (i * 7 > 57)
                    {
                        staticData[1] |= (uint64_t(fieldValues[fieldIdx]) >> (64 - i * 7));
                    }
                }
                else
                {
                    staticData[1] |= (uint64_t(fieldValues[fieldIdx]) << (i * 7 - 64));
                }
            }

            // Combine with proper alignment handling for >64 bit cases
            const uint8_t nonStaticBits = nonStaticCount * 2 * 7;

            if (nonStaticBits > 64)
            {
                eppData[0] = nonStaticEpp[0];
                eppData[1] = nonStaticEpp[1] | (staticData[0] << (nonStaticBits - 64));
            }
            else
            {
                eppData[0] = nonStaticEpp[0] | (staticData[0] << nonStaticBits);
                eppData[1] = (staticData[0] >> (64 - nonStaticBits)) | (staticData[1] << nonStaticBits);
            }
        }

        uint64_t extra = b->Raw[1] >> 30;                     // Extract P-bits and index (34 bits)
        uint64_t mode_part = (b->Raw[0] & 0x3ff);                     // Extract mode and partition (10 bits)

        // Assemble with mode positioned at byte boundary for compression
        d[0] = ((extra >> 22) & 0xfff) |
            (eppData[0] << 12);

        d[1] = (eppData[0] >> 52) |
            (eppData[1] << 12) |
            (mode_part << 32) |
            (extra << 42);
    }
    else
    {
        // All fields dynamic - use parallel extraction for 2 endpoint pairs
        // Each endpoint pair is 42 bits (7 bits/channel * 3 channels * 2 endpoints)
        // 6-bit spacing provides room for all bits in interleaved pattern
        const uint64_t r0m = 0b000001000001000001000001000001000001000001;
        const uint64_t g0m = 0b000010000010000010000010000010000010000010;
        const uint64_t b0m = 0b000100000100000100000100000100000100000100;
        const uint64_t r1m = 0b001000001000001000001000001000001000001000;
        const uint64_t g1m = 0b010000010000010000010000010000010000010000;
        const uint64_t b1m = 0b100000100000100000100000100000100000100000;

        // Create both endpoint pairs using parallel bit deposit
        uint64_t epp0 =
            _pdep_u64(b->R0, r0m) |
            _pdep_u64(b->R1, r1m) |
            _pdep_u64(b->G0, g0m) |
            _pdep_u64(b->G1, g1m) |
            _pdep_u64(b->B0, b0m) |
            _pdep_u64(b->B1, b1m);

        uint64_t epp1 =
            _pdep_u64(b->R2, r0m) |
            _pdep_u64(b->R3, r1m) |
            _pdep_u64(b->G2, g0m) |
            _pdep_u64(b->G3(), g1m) |
            _pdep_u64(b->B2, b0m) |
            _pdep_u64(b->B3, b1m);

        // Position mode at byte boundary for optimal compression
        uint64_t extra = b->Raw[1] >> 30;                     // Extract P-bits and index (34 bits)
        uint64_t mode_part = (b->Raw[0] & 0x3ff);                     // Extract mode and partition (10 bits)

        // Pack endpoint pairs with gaps filled by index data for efficient layout
        d[0] = ((extra >> 22) & 0x3f) |
            (epp1 << 6) |
            ((extra >> 28) << 48) |
            (epp0 << 54);

        d[1] = ((epp0 >> 10) & 0xffffffff) |
            (mode_part << 32) |
            (extra << 42);
    }
}

void BC7_ModeJoin_Shuffle_Fast_Mode4(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = metrics.M[4].Statics;
    const BC7m4_Derotated* b = reinterpret_cast<const BC7m4_Derotated*>(src);
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    uint64_t eppData = 0;
    if (statics)
    {
        if (staticCount == 8) // All RGBA fields static - direct extraction optimized
        {
            eppData = (b->Raw[0] >> 8 & 0xffffffff);
            eppData |= (b->Raw[0] >> 39 & 0x1f) << 30;
            eppData |= (b->Raw[0] >> 45 & 0x1f) << 35;
        }
        else
        {
            // Mixed static/dynamic requiring field separation and reorganization
            const uint8_t nonStaticCount = 8 - staticCount;

            // Build processing lists for 8 RGBA fields (2 endpoints each)
            uint8_t staticFields[8] = {};
            uint8_t nonStaticFields[8] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Classify each RGBA field as static or dynamic
            for (uint8_t field = 0; field < 8; field++)
            {
                if (metrics.M[4].Statics & (1 << field))
                {
                    staticFields[staticIndex++] = field;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = field;
                }
            }

            // Extract 5-bit RGBA values from derotated block structure
            const uint64_t fieldValues[8] = { b->R0, b->R1, b->G0, b->G1, b->B0, b->B1, b->A0, b->A1 };

            // Optimal RGBA ordering for shuffled endpoint layout
            const uint8_t mapToShuffledOrder[8] = { 0, 4, 1, 5, 2, 6, 3, 7 };

            // Sort non-static fields by optimal shuffle positions
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Create interleaved pattern for non-static fields
            uint64_t nonStaticEpp = 0;
            if (nonStaticCount > 0)
            {
                const uint64_t mask = maskTable[nonStaticCount][0];
                for (uint8_t i = 0; i < nonStaticCount; i++)
                {
                    const uint8_t fieldIdx = nonStaticFields[i];
                    nonStaticEpp |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                }
            }

            // Pack static fields consecutively (5 bits each for Mode 4)
            uint64_t staticData = 0;
            for (uint8_t i = 0; i < staticCount; i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                staticData |= (uint64_t(fieldValues[fieldIdx]) << (i * 5));
            }

            // Combine interleaved and static data with proper bit positioning
            const uint8_t nonStaticBits = nonStaticCount * 5;

            eppData = nonStaticEpp | (staticData << nonStaticBits);
        }

        // Extract mode, rotation, and index data from derotated block
        uint64_t extra_start = b->Raw[1] >> 10;                      // Remaining index bits (54 bits)
        uint64_t extra_end = (b->Raw[0] >> 50) | ((b->Raw[1] & 0x3ff) << 14);  // First index bits (24 bits)
        uint64_t mode_rot_idx = (b->Raw[0] & 0xff);                  // Mode + rotation + index mode (8 bits)
        uint64_t ex_bits = ((b->Raw[0] >> 38) & 0x1) |
            (((b->Raw[0] >> 44) & 0x1) << 1);         // Extract ex0 and ex1 bits (2 bits)

        // Assemble with mode positioned at byte boundary and data optimally placed
        d[0] = extra_start |                    // Remaining index data fills low bits
            (ex_bits << 54) |                             // Position ex bits before endpoint data
            (eppData << 56);                                // First endpoint bits

        d[1] = (eppData >> 8) |                          // Remaining endpoint bits
            (mode_rot_idx << 32) |                       // Mode positioned at byte boundary
            (extra_end << 40);                       // First index bits in high section
    }
    else
    {
        // All fields dynamic - use parallel extraction for RGBA endpoint pair
        // Single endpoint pair is 40 bits (5 bits/channel * 4 channels * 2 endpoints)
        // 8-bit spacing accommodates all RGBA channel bits in interleaved pattern
        const uint64_t r0m = 0b0000000100000001000000010000000100000001;
        const uint64_t g0m = 0b0000001000000010000000100000001000000010;
        const uint64_t b0m = 0b0000010000000100000001000000010000000100;
        const uint64_t r1m = 0b0000100000001000000010000000100000001000;
        const uint64_t g1m = 0b0001000000010000000100000001000000010000;
        const uint64_t b1m = 0b0010000000100000001000000010000000100000;
        const uint64_t a0m = 0b0100000001000000010000000100000001000000;
        const uint64_t a1m = 0b1000000010000000100000001000000010000000;

        // Create RGBA endpoint pair using parallel bit deposit for optimal interleaving
        uint64_t epp0 =
            _pdep_u64(b->R0, r0m) |
            _pdep_u64(b->R1, r1m) |
            _pdep_u64(b->G0, g0m) |
            _pdep_u64(b->G1, g1m) |
            _pdep_u64(b->B0, b0m) |
            _pdep_u64(b->B1, b1m) |
            _pdep_u64(b->A0, a0m) |
            _pdep_u64(b->A1, a1m);

        // Extract and position all non-endpoint data for optimal layout
        uint64_t extra_start = b->Raw[1] >> 10;                      // Remaining index bits (54 bits)
        uint64_t extra_end = (b->Raw[0] >> 50) | ((b->Raw[1] & 0x3ff) << 14);  // First index bits (24 bits)
        uint64_t mode_rot_idx = (b->Raw[0] & 0xff);                  // Mode + rotation + index mode (8 bits)
        uint64_t ex_bits = ((b->Raw[0] >> 38) & 0x1) |
            (((b->Raw[0] >> 44) & 0x1) << 1);         // Extract ex0 and ex1 bits (2 bits)

        // Assemble final layout with mode at byte boundary for compression efficiency
        d[0] = extra_start |                    // Fill low bits with remaining index data
            (ex_bits << 54) |                             // Position extension bits
            (epp0 << 56);                                // Start endpoint pair data

        d[1] = (epp0 >> 8) |                          // Complete endpoint pair data
            (mode_rot_idx << 32) |                       // Mode at byte boundary
            (extra_end << 40);                       // Index data in high section
    }
}

void BC7_ModeJoin_Shuffle_Fast_Mode5(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = metrics.M[5].Statics;
    const BC7m5_Derotated* b = reinterpret_cast<const BC7m5_Derotated*>(src);
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    uint64_t eppData = 0;
    if (statics)
    {
        if (staticCount == 8) // All RGBA fields static - optimized direct extraction
        {
            eppData = (b->Raw[0] >> 8 & 0x3ffffffffff);
            eppData |= (b->A0) << 42;
            eppData |= (b->A1()) << 49;
        }
        else
        {
            // Mixed static/dynamic requiring complex field reorganization
            const uint8_t nonStaticCount = 8 - staticCount;

            // Build processing lists for 8 RGBA fields
            uint8_t staticFields[8] = {};
            uint8_t nonStaticFields[8] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Classify RGBA fields by static/dynamic status
            for (uint8_t field = 0; field < 8; field++)
            {
                if (metrics.M[5].Statics & (1 << field))
                {
                    staticFields[staticIndex++] = field;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = field;
                }
            }

            // Extract 7-bit RGBA values from derotated block
            const uint64_t fieldValues[8] = { b->R0, b->R1, b->G0, b->G1, b->B0, b->B1, b->A0, b->A1() };

            // Optimal RGBA ordering for shuffled endpoint layout
            const uint8_t mapToShuffledOrder[8] = { 0, 4, 1, 5, 2, 6, 3, 7 };

            // Sort non-static fields by target shuffle positions
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Create interleaved pattern for non-static fields
            uint64_t nonStaticEpp = 0;
            if (nonStaticCount > 0)
            {
                const uint64_t mask = maskTable[nonStaticCount][0];
                for (uint8_t i = 0; i < nonStaticCount; i++)
                {
                    const uint8_t fieldIdx = nonStaticFields[i];
                    nonStaticEpp |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                }
            }

            // Pack static fields consecutively (7 bits each for Mode 5)
            uint64_t staticData = 0;
            for (uint8_t i = 0; i < staticCount; i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                staticData |= (uint64_t(fieldValues[fieldIdx]) << (i * 7));
            }

            // Combine interleaved and static data with proper positioning
            const uint8_t nonStaticBits = nonStaticCount * 7;

            eppData = nonStaticEpp | (staticData << nonStaticBits);
        }

        // Extract mode, rotation, and dual index data from derotated block
        uint64_t extra_start = b->Raw[1] >> 26;                      // Remaining index bits (38 bits)
        uint64_t extra_end = (b->Raw[1] >> 2) & 0xffffff;           // First index bits (24 bits)
        uint64_t mode_rot = b->Raw[0] & 0xff;                      // Mode + rotation (8 bits)
        uint64_t ex_bits = ((b->Raw[0] >> 50) & 0x1) |
            (((b->Raw[0] >> 58) & 0x1) << 1);                       // Extract ex0 and ex1 bits (2 bits)

        // Assemble with endpoint data positioned optimally for 56-bit span
        d[0] = extra_start |                          // Remaining index data in low bits
            ((ex_bits & 0x3) << 38) |                             // Position extension bits
            (eppData << 40);                           // Start endpoint pair (24 bits here)

        d[1] = (eppData >> 24) |                         // Complete endpoint pair (32 bits)
            (mode_rot << 32) |                     // Mode + rotation at byte boundary
            (extra_end << 40);                     // First index bits in high section
    }
    else
    {
        // All fields dynamic - parallel extraction for RGBA endpoint pair
        // Single endpoint pair is 56 bits (7 bits/channel * 4 channels * 2 endpoints)
        // 8-bit spacing accommodates all RGBA bits in interleaved pattern
        const uint64_t r0m = 0b00000001000000010000000100000001000000010000000100000001;
        const uint64_t g0m = 0b00000010000000100000001000000010000000100000001000000010;
        const uint64_t b0m = 0b00000100000001000000010000000100000001000000010000000100;
        const uint64_t r1m = 0b00001000000010000000100000001000000010000000100000001000;
        const uint64_t g1m = 0b00010000000100000001000000010000000100000001000000010000;
        const uint64_t b1m = 0b00100000001000000010000000100000001000000010000000100000;
        const uint64_t a0m = 0b01000000010000000100000001000000010000000100000001000000;
        const uint64_t a1m = 0b10000000100000001000000010000000100000001000000010000000;


        // Create RGBA endpoint pair using parallel bit deposit for optimal interleaving
        uint64_t epp0 =
            _pdep_u64(b->R0, r0m) |
            _pdep_u64(b->R1, r1m) |
            _pdep_u64(b->G0, g0m) |
            _pdep_u64(b->G1, g1m) |
            _pdep_u64(b->B0, b0m) |
            _pdep_u64(b->B1, b1m) |
            _pdep_u64(b->A0, a0m) |
            _pdep_u64(b->A1(), a1m);

        // Extract mode, rotation, and dual index data for optimal positioning
        uint64_t extra_start = b->Raw[1] >> 26;                      // Remaining index bits (38 bits)
        uint64_t extra_end = (b->Raw[1] >> 2) & 0xffffff;           // First index bits (24 bits)
        uint64_t mode_rot = b->Raw[0] & 0xff;                      // Mode + rotation (8 bits)
        uint64_t ex_bits = ((b->Raw[0] >> 50) & 0x1) |
            (((b->Raw[0] >> 58) & 0x1) << 1);                       // Extract ex0 and ex1 bits (2 bits)

        // Assemble final layout with 56-bit endpoint data spanning both uint64s
        d[0] = extra_start |                          // Fill low bits with remaining index
            ((ex_bits & 0x3) << 38) |                             // Position extension bits
            (epp0 << 40);                           // First 24 bits of endpoint data

        d[1] = (epp0 >> 24) |                         // Remaining 32 bits of endpoint data
            (mode_rot << 32) |                     // Mode + rotation at byte boundary
            (extra_end << 40);                     // First index bits in high section
    }
}


void BC7_ModeJoin_Shuffle_Fast_Mode6(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    uint8_t statics = metrics.M[6].Statics;
    const BC7m6* b = reinterpret_cast<const BC7m6*>(src);
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);
    const uint8_t staticCount = uint8_t(_mm_popcnt_u64(statics));

    uint64_t eppData = 0;
    if (statics)
    {
        if (staticCount == 8) // All RGBA fields static - optimized direct extraction
        {
            eppData = (b->Raw[0] >> 7 & 0xffffffffffffff);
        }
        else
        {
            // Mixed static/dynamic requiring field separation and reorganization
            const uint8_t nonStaticCount = 8 - staticCount;

            // Build processing lists for 8 RGBA fields
            uint8_t staticFields[8] = {};
            uint8_t nonStaticFields[8] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Classify RGBA fields by static/dynamic status
            for (uint8_t field = 0; field < 8; field++)
            {
                if (metrics.M[6].Statics & (1 << field))
                {
                    staticFields[staticIndex++] = field;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = field;
                }
            }

            // Extract 7-bit RGBA values from source block
            const uint64_t fieldValues[8] = { b->R0, b->R1, b->G0, b->G1, b->B0, b->B1, b->A0, b->A1 };

            // Optimal RGBA ordering for shuffled endpoint layout
            const uint8_t mapToShuffledOrder[8] = { 0, 4, 1, 5, 2, 6, 3, 7 };

            // Sort non-static fields by target shuffled positions
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Create interleaved pattern for non-static fields
            uint64_t nonStaticEpp = 0;
            if (nonStaticCount > 0)
            {
                const uint64_t mask = maskTable[nonStaticCount][0];
                for (uint8_t i = 0; i < nonStaticCount; i++)
                {
                    const uint8_t fieldIdx = nonStaticFields[i];
                    nonStaticEpp |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                }
            }

            // Pack static fields consecutively (7 bits each for Mode 6)
            uint64_t staticData = 0;
            for (uint8_t i = 0; i < staticCount; i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                staticData |= (uint64_t(fieldValues[fieldIdx]) << (i * 7));
            }

            // Combine interleaved and static data with proper positioning
            const uint8_t nonStaticBits = nonStaticCount * 7;

            eppData = nonStaticEpp | (staticData << nonStaticBits);
        }

        // Extract mode and index data from source block
        uint64_t extra_start = b->Raw[1] >> 24;                      // Remaining index bits (40 bits)
        uint64_t extra_end = ((b->Raw[0] >> 63) | (b->Raw[1] << 1)) & 0x1ffffff;  // P-bits and first index bits (25 bits)
        uint64_t mode = b->Raw[0] & 0x7f;                          // Mode (7 bits)

        // Assemble with endpoint data positioned for 56-bit span
        d[0] = extra_start |                          // Remaining index data in low bits
            (eppData << 40);                           // Start endpoint pair (24 bits here)

        d[1] = (eppData >> 24) |                         // Complete endpoint pair (32 bits)
            (mode << 32) |                     // Mode at byte boundary
            (extra_end << 39);                     // P-bits and first index bits
    }
    else
    {
        // All fields dynamic - use parallel extraction for RGBA endpoint pair
        // Single endpoint pair is 56 bits (7 bits/channel * 4 channels * 2 endpoints)
        // 8-bit spacing accommodates all RGBA bits in interleaved pattern
        const uint64_t r0m = 0b00000001000000010000000100000001000000010000000100000001;
        const uint64_t g0m = 0b00000010000000100000001000000010000000100000001000000010;
        const uint64_t b0m = 0b00000100000001000000010000000100000001000000010000000100;
        const uint64_t r1m = 0b00001000000010000000100000001000000010000000100000001000;
        const uint64_t g1m = 0b00010000000100000001000000010000000100000001000000010000;
        const uint64_t b1m = 0b00100000001000000010000000100000001000000010000000100000;
        const uint64_t a0m = 0b01000000010000000100000001000000010000000100000001000000;
        const uint64_t a1m = 0b10000000100000001000000010000000100000001000000010000000;

        // Create RGBA endpoint pair using parallel bit deposit for optimal interleaving
        uint64_t epp0 =
            _pdep_u64(b->R0, r0m) |
            _pdep_u64(b->R1, r1m) |
            _pdep_u64(b->G0, g0m) |
            _pdep_u64(b->G1, g1m) |
            _pdep_u64(b->B0, b0m) |
            _pdep_u64(b->B1, b1m) |
            _pdep_u64(b->A0, a0m) |
            _pdep_u64(b->A1, a1m);

        // Extract mode and index data for optimal positioning
        uint64_t extra_start = b->Raw[1] >> 24;                      // Remaining index bits (40 bits)
        uint64_t extra_end = ((b->Raw[0] >> 63) | (b->Raw[1] << 1)) & 0x1ffffff;  // P-bits and first index bits (25 bits)
        uint64_t mode = b->Raw[0] & 0x7f;                          // Mode (7 bits)

        // Assemble final layout with 56-bit endpoint data spanning both uint64s
        d[0] = extra_start |                          // Fill low bits with remaining index
            (epp0 << 40);                           // First 24 bits of endpoint data

        d[1] = (epp0 >> 24) |                         // Remaining 32 bits of endpoint data
            (mode << 32) |                     // Mode at byte boundary
            (extra_end << 39);                     // P-bits and first index bits
    }
}

void BC7_ModeJoin_Shuffle_Fast_Mode7(const void* src, void* dest, const BC7TextureMetrics& metrics)
{
    const BC7m7* b = reinterpret_cast<const BC7m7*>(src);
    uint64_t* d = reinterpret_cast<uint64_t*>(dest);

    uint64_t eppData[2] = { 0, 0 };
    if (metrics.M[7].Statics)
    {
        if (metrics.M[7].Statics == 0xff) // All RGBA fields static - optimized direct extraction
        {
            eppData[0] = (b->Raw[0] >> 14) | (b->Raw[1] << 50);
            eppData[1] = ((b->Raw[1] >> 14) & 0xffff);
        }
        else
        {
            // Mixed static/dynamic requiring complex field reorganization
            const uint8_t staticCount = uint8_t(_mm_popcnt_u64(metrics.M[7].Statics));
            const uint8_t nonStaticCount = 8 - staticCount;

            // Build processing lists for static vs dynamic fields (16 total endpoints)
            uint8_t staticFields[16] = {};
            uint8_t nonStaticFields[16] = {};
            uint8_t staticIndex = 0, nonStaticIndex = 0;

            // Mode 7 has 2 endpoint pairs per color field grouping
            for (uint8_t field = 0; field < 8; field++)
            {
                uint8_t epp0Index = (field % 2 ? field * 2 - 1 : field * 2);
                uint8_t epp1Index = epp0Index + 2;
                if (metrics.M[7].Statics & (1 << field))
                {
                    staticFields[staticIndex++] = epp0Index;
                    staticFields[staticIndex++] = epp1Index;
                }
                else
                {
                    nonStaticFields[nonStaticIndex++] = epp0Index;
                    nonStaticFields[nonStaticIndex++] = epp1Index;
                }
            }

            // Extract 5-bit RGBA values from all endpoint positions
            const uint64_t fieldValues[16] = { b->R0, b->R1, b->R2, b->R3, b->G0, b->G1, b->G2, b->G3, b->B0, b->B1, b->B2, b->B3, b->A0, b->A1, b->A2, b->A3 };

            // Optimal endpoint ordering for shuffled layout
            const uint8_t mapToShuffledOrder[16] = { 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15 };

            // Sort non-static fields by target shuffled positions
            std::sort(nonStaticFields, nonStaticFields + nonStaticCount * 2,
                [&mapToShuffledOrder](uint8_t a, uint8_t b) {
                    return mapToShuffledOrder[a] < mapToShuffledOrder[b];
                });

            // Keep static fields in original order for contiguous placement
            std::sort(staticFields, staticFields + staticCount * 2);

            // Build interleaved pattern for non-static fields (may span two uint64s)
            uint64_t nonStaticEpp[2] = { 0, 0 };
            if (nonStaticCount > 0)
            {
                const uint8_t totalNonStaticFields = nonStaticCount * 2;

                if (totalNonStaticFields <= 12)  // Fits in single uint64 with interleaving
                {
                    const uint64_t mask = maskTable[totalNonStaticFields][0];
                    for (uint8_t i = 0; i < totalNonStaticFields; i++)
                    {
                        const uint8_t fieldIdx = nonStaticFields[i];
                        nonStaticEpp[0] |= _pdep_u64(fieldValues[fieldIdx], mask << i);
                    }
                }
                else  // More than 12 fields require spanning two uint64s
                {
                    const uint64_t mask14_0 = maskTable[14][0];
                    const uint64_t mask14_1 = maskTable[14][1];

                    // Process using split mask approach for >60 bit patterns
                    for (uint8_t i = 0; i < totalNonStaticFields; i++)
                    {
                        const uint8_t fieldIdx = nonStaticFields[i];
                        const uint64_t fieldValue = fieldValues[fieldIdx];

                        // Deposit first 4 bits using primary mask
                        nonStaticEpp[0] |= _pdep_u64(fieldValue, mask14_0 << i);

                        // Deposit 5th bit using secondary mask for overflow
                        if (i > 7)
                            nonStaticEpp[1] |= _pdep_u64((fieldValue >> 4u) & 0x1u, mask14_1 >> (14u - i));
                    }
                }
            }

            // Pack static fields consecutively (5 bits each for Mode 7)
            uint64_t staticData[2] = { 0 };
            for (uint8_t i = 0; i < uint8_t(staticCount * 2); i++)
            {
                const uint8_t fieldIdx = staticFields[i];
                if (i * 5 < 64)
                {
                    staticData[0] |= (uint64_t(fieldValues[fieldIdx]) << (i * 5));
                    if (i * 5 > 59)
                    {
                        staticData[1] |= (uint64_t(fieldValues[fieldIdx]) >> (64 - i * 5));
                    }
                }
                else
                {
                    staticData[1] |= (uint64_t(fieldValues[fieldIdx]) << (i * 5 - 64));
                }
            }

            // Combine interleaved and static data with proper alignment
            const uint8_t staticBits = staticCount * 2 * 5;
            const uint8_t nonStaticBits = nonStaticCount * 2 * 5;

            if (nonStaticBits)
            {
                if (nonStaticBits <= 64)
                {
                    eppData[0] = nonStaticEpp[0] | (staticData[0] << nonStaticBits);
                    if (nonStaticBits + staticBits > 64)
                    {
                        eppData[1] = (staticData[0] >> (64 - nonStaticBits)) | (staticData[1] << nonStaticBits);
                    }
                }
                else  // nonStaticBits > 64 for complex cases
                {
                    eppData[0] = nonStaticEpp[0];
                    eppData[1] = nonStaticEpp[1] | (staticData[0] << (nonStaticBits - 64));
                }
            }
            else
            {
                eppData[0] = staticData[0];
                eppData[1] = staticData[1];
            }
        }

        // Extract mode, partition, and index data from source block
        uint64_t extra_start = b->Raw[1] >> 48;                      // Remaining index bits (16 bits)
        uint64_t extra_end = (b->Raw[1] >> 30) & 0x3ffff;           // First index bits (18 bits)
        uint64_t mode_part = b->Raw[0] & 0x3fff;                      // Mode and partition (14 bits)

        // Assemble with endpoint pairs positioned to fill two 40-bit sections
        d[0] = (extra_start) |                           // Remaining index data (16 bits)
            eppData[0] << 16;                           // First endpoint data (48 bits)

        d[1] = (eppData[0] >> 48) |                         // Remaining endpoint data (16 bits)
            (eppData[1] << 16) |                           // Second endpoint data (48 bits)
            (mode_part << 32) |                     // Mode and partition at byte boundary
            (extra_end << 46);                     // First index bits (18 bits)
    }
    else
    {
        // All fields dynamic - use parallel extraction for dual RGBA endpoint pairs
        // Two endpoint pairs each 40 bits (5 bits/channel * 4 channels * 2 endpoints)
        // 8-bit spacing accommodates all RGBA channel bits in interleaved pattern
        const uint64_t r0m = 0b00000001000000010000000100000001000000010000000100000001;
        const uint64_t g0m = 0b00000010000000100000001000000010000000100000001000000010;
        const uint64_t b0m = 0b00000100000001000000010000000100000001000000010000000100;
        const uint64_t r1m = 0b00001000000010000000100000001000000010000000100000001000;
        const uint64_t g1m = 0b00010000000100000001000000010000000100000001000000010000;
        const uint64_t b1m = 0b00100000001000000010000000100000001000000010000000100000;
        const uint64_t a0m = 0b01000000010000000100000001000000010000000100000001000000;
        const uint64_t a1m = 0b10000000100000001000000010000000100000001000000010000000;


        // Create both RGBA endpoint pairs using parallel bit deposit
        uint64_t epp0 =
            _pdep_u64(b->R0, r0m) |
            _pdep_u64(b->R1, r1m) |
            _pdep_u64(b->G0, g0m) |
            _pdep_u64(b->G1, g1m) |
            _pdep_u64(b->B0, b0m) |
            _pdep_u64(b->B1, b1m) |
            _pdep_u64(b->A0, a0m) |
            _pdep_u64(b->A1, a1m);

        uint64_t epp1 =
            _pdep_u64(b->R2, r0m) |
            _pdep_u64(b->R3, r1m) |
            _pdep_u64(b->G2, g0m) |
            _pdep_u64(b->G3, g1m) |
            _pdep_u64(b->B2, b0m) |
            _pdep_u64(b->B3, b1m) |
            _pdep_u64(b->A2, a0m) |
            _pdep_u64(b->A3, a1m);

        // Extract mode, partition, and index data for optimal positioning
        uint64_t extra_start = b->Raw[1] >> 48;                      // Remaining index bits (16 bits)
        uint64_t extra_end = (b->Raw[1] >> 30) & 0x3ffff;           // First index bits (18 bits)
        uint64_t mode_part = b->Raw[0] & 0x3fff;                      // Mode and partition (14 bits)

        // Assemble final layout with dual endpoint pairs positioned optimally
        d[0] = epp1 |                          // Second endpoint pair (40 bits)
            (extra_start << 40) |                           // Remaining index data (16 bits)
            (epp0 << 56);						  // First bits of first endpoint pair (8 bits)

        d[1] = (epp0 >> 8) |                         // Remaining first endpoint pair (32 bits)
            (mode_part << 32) |                     // Mode and partition at byte boundary
            (extra_end << 46);                     // First index bits (18 bits)
    }
}

void (*BC7_ModeJoin_Shuffle_Fast[8][2])(const void* src, void* dest, const BC7TextureMetrics& metrics) =
{
    {&BC7_ModeJoin_Shuffle_Fast_Mode0, nullptr},
    {&BC7_ModeJoin_Shuffle_Fast_Mode1, nullptr},
    {&BC7_ModeJoin_Shuffle_Fast_Mode2, nullptr},
    {&BC7_ModeJoin_Shuffle_Fast_Mode3, nullptr},
    {&BC7_ModeJoin_Shuffle_Fast_Mode4, nullptr},
    {&BC7_ModeJoin_Shuffle_Fast_Mode5, nullptr},
    {&BC7_ModeJoin_Shuffle_Fast_Mode6, nullptr},
    {&BC7_ModeJoin_Shuffle_Fast_Mode7, nullptr},
};
static_assert(_countof(BC7_ModeJoin_Shuffle_Fast) == 8, "BC7_ModeJoin_Shuffle_Fast size mismatch");

void ComputeRotationToStableRange7bit_JoinMode(size_t(&e3cRGB)[6][128], size_t(&e4cRGBA)[8][128], size_t(&e4cRGB)[6][128], size_t(&e4cA)[2][128], size_t elements, uint8_t* rotationsDefault, uint8_t* rotationsAlt)
{
    /*      There are two different rotations computed, based on the scenario.
    *       Both work to maximize matches across multiple block modes by aligning endpoint pairs on byte boundaries.  To encourage matches across chunks of the texture that may have varying color,
    *       rotations for each "chunk" are computed, to encourage color fluctuations across different areas of the texture to match up more frequently.
    *
    *       Mode-Join - default, where modes 4\5\6\7 (includes alpha) make RGBA merged endpoints.  They might not match the mode 0\1\2\3 non-alpha endpoints
    *                   endpoint weighting  = e3cRGB + e4cRGBA
    *
    *       Mode-Join - ALT, where modes 4\5\6\7 (includes alpha make RGB endpoints, and alpha placed elsewhere
    *                   endpoint weighting  = e3cRGB + e4cRGB + e4cA

    */

    UNREFERENCED_PARAMETER(elements); // to be used in the future

    size_t countTotalsD[8] = {};
    size_t countTotalsA[8] = {};

    std::vector<size_t> countsDvec(8 * 128);
    std::vector<size_t> countsAvec(8 * 128);

    size_t(*countsD)[128] = reinterpret_cast<size_t(*)[128]>(countsDvec.data());    // default
    size_t(*countsA)[128] = reinterpret_cast<size_t(*)[128]>(countsAvec.data());    // Alt

    for (int e = 0; e < 8; e++)
    {
        for (int v = 0; v < 128; v++)
        {

            if (e < 6)
            {
                countsD[e][v] += e3cRGB[e][v];
                countsA[e][v] += e3cRGB[e][v];
                countsA[e][v] += e4cRGB[e][v];
            }
            else
            {
                countsA[e][v] += e4cA[e - 6][v];
            }

            countsD[e][v] += e4cRGBA[e][v];

            countTotalsD[e] += countsD[e][v];
            countTotalsA[e] += countsA[e][v];
        }
    }


    uint8_t bestGroupingStartD[8][6] = {};
    size_t bestRotationCountD[8][6] = {};

    uint8_t bestGroupingStartA[8][6] = {};
    size_t bestRotationCountA[8][6] = {};

    for (auto counts : { countsD , countsA })
    {
        for (int e = 0; e < 8; e++)
        {
            // rotated sums for each order 1-6
            size_t rotatedSums[128][6] = {};
            uint8_t(*bestGroupingStart)[6] = counts == countsD ? bestGroupingStartD : bestGroupingStartA;
            size_t(*bestGroupingCount)[6] = counts == countsD ? bestRotationCountD : bestRotationCountA;
            size_t* countTotals = counts == countsD ? countTotalsD : countTotalsA;

            for (uint8_t first = 0; first < 128; first++)
            {
                for (uint8_t i = 0; i < 64; i++)
                {
                    if (((first + i) % 128) == 0) continue;
                    if (i < 2)
                    {
                        rotatedSums[first][5] += counts[e][(first + i) % 128u];  // 5 - count within 1/64 the range
                    }
                    if (i < 4)
                    {
                        rotatedSums[first][4] += counts[e][(first + i) % 128u];  // 4 - count within 1/32 the range
                    }
                    if (i < 8)
                    {
                        rotatedSums[first][3] += counts[e][(first + i) % 128u];  // 3 - count within 1/16 the range
                    }
                    if (i < 16)
                    {
                        rotatedSums[first][2] += counts[e][(first + i) % 128u];  // 2 - count within 1/8 the range
                    }
                    if (i < 32)
                    {
                        rotatedSums[first][1] += counts[e][(first + i) % 128u];  // 1 - count within 1/4 the range
                    }
                    if (i < 64)
                    {
                        rotatedSums[first][0] += counts[e][(first + i) % 128u];  // 0 - count within 1/2 the range
                    }
                }
            }

            for (uint8_t i = 0; i < 128u; i++)
            {
                for (uint8_t o = 0; o < 6u; o++)
                {
                    if (rotatedSums[i][o] > bestGroupingCount[e][o])
                    {
                        bestGroupingCount[e][o] = rotatedSums[i][o];
                        bestGroupingStart[e][o] = i;
                    }
                }
            }

            // then decide on which "order" to keep, and compute final rotation
            const uint8_t rotDest[] = { 0, 0, 16, 16, 24, 26 };
            // 0 0 0 0 0 0 0    0
            // 0 0 0 0 0 0 1    1
            // 0 0 0 0 0 1 0    2
            // 0 0 0 0 0 1 1    3
            // 0 0 0 0 1 0 0    4
            // 0 0 0 0 1 0 1    5
            // 0 0 0 0 1 1 0    6
            // 0 0 0 0 1 1 1    7
            // 0 0 0 1 0 0 0    8
            // 0 0 0 1 0 0 1    9
            // 0 0 0 1 0 1 0    10
            // 0 0 0 1 0 1 1    11
            // 0 0 0 1 1 0 0    12
            // 0 0 0 1 1 0 1    13
            // 0 0 0 1 1 1 0    14
            // 0 0 0 1 1 1 1    15
            // 0 0 1 0 0 0 0    16       ]      ]
            // 0 0 1 0 0 0 1    17       ]      ]
            // 0 0 1 0 0 1 0    18       ]      ]
            // 0 0 1 0 0 1 1    19       ]      ]
            // 0 0 1 0 1 0 0    20       ]      ]- 4th order range
            // 0 0 1 0 1 0 1    21       ]      ]
            // 0 0 1 0 1 1 0    22       ]      ]
            // 0 0 1 0 1 1 1    23       ]      ]
            // 0 0 1 1 0 0 0    24       ]           ]- 5th order range
            // 0 0 1 1 0 0 1    25       ]           ]
            // 0 0 1 1 0 1 0    26       ]           ]    ]- 6th order range
            // 0 0 1 1 0 1 1    27       ]           ]    ]
            // 0 0 1 1 1 0 0    28       ]
            // 0 0 1 1 1 0 1    29       ]-3rd order range
            // 0 0 1 1 1 1 0    30       ]
            // 0 0 1 1 1 1 1    31       ]
            uint8_t* rotationsOut = counts == countsD ? rotationsDefault : rotationsAlt;
            for (int o = 0; o < 6; o++)
            {
                if (bestGroupingCount[e][o] > (1 * countTotals[e] / 2))
                {
                    //rotationsOut[e] = (bestGroupingStart[e][o] - rotDest[o]) % 128u;
                    rotationsOut[e] =
                        bestGroupingStart[e][o] <= rotDest[o] ?
                        rotDest[o] - bestGroupingStart[e][o] :
                        rotDest[o] + (128u - bestGroupingStart[e][o]);
                }
            }
        }
    }
}

void ComputeRotationToLeastSignificantEntropy7bit_JoinMode(size_t(&e3cRGB)[6][128], size_t(&e4cRGBA)[8][128], size_t(&e4cRGB)[6][128], size_t(&e4cA)[2][128], size_t elements, uint8_t* rotationsDefault, uint8_t* rotationsAlt)
{
    /*      There are two different rotations computed, based on the scenario.
    *       Both work to maximize matches across multiple block modes by aligning endpoint pairs on byte boundaries.
    *       Rather than trying to make colors match across multiple chunks, this version purely tries to minimize entropy in significant bits for each channel across one chunk
    *
    *       Mode-Join - default, where modes 4\5\6\7 (includes alpha) make RGBA merged endpoints.  They might not match the mode 0\1\2\3 non-alpha endpoints
    *                   endpoint weighting  = e3cRGB + e4cRGBA
    *
    *       Mode-Join - ALT, where modes 4\5\6\7 (includes alpha make RGB endpoints, and alpha placed elsewhere
    *                   endpoint weighting  = e3cRGB + e4cRGB + e4cA

    */
    
    UNREFERENCED_PARAMETER(elements); // to be used in the future

    size_t countTotalsD[8] = {};
    size_t countTotalsA[8] = {};

    std::vector<size_t> countsDvec(8 * 128);
    std::vector<size_t> countsAvec(8 * 128);

    size_t(*countsD)[128] = reinterpret_cast<size_t(*)[128]>(countsDvec.data());    // default
    size_t(*countsA)[128] = reinterpret_cast<size_t(*)[128]>(countsAvec.data());    // Alt

    for (int e = 0; e < 8; e++)
    {
        for (int v = 0; v < 128; v++)
        {

            if (e < 6)
            {
                countsD[e][v] += e3cRGB[e][v];
                countsA[e][v] += e3cRGB[e][v];
                countsA[e][v] += e4cRGB[e][v];
            }
            else
            {
                countsA[e][v] += e4cA[e - 6][v];
            }

            countsD[e][v] += e4cRGBA[e][v];

            countTotalsD[e] += countsD[e][v];
            countTotalsA[e] += countsA[e][v];
        }
    }

    for (auto counts : { countsD , countsA })
    {

        //     channel, rotation, bit
        std::vector<size_t> cbitsvec(8 * 128 * 7);
        size_t(*cbits)[128][7] = reinterpret_cast<size_t(*)[128][7]>(cbitsvec.data());    //size_t cbits[8][128][7] = {};
        uint8_t* rotationsOut = counts == countsD ? rotationsDefault : rotationsAlt;
        size_t* countTotals = counts == countsD ? countTotalsD : countTotalsA;

        for (int e = 0; e < 8; e++)
        {
            constexpr uint8_t fieldValues = 128;

            // compute ideal entropy shift for each color channel
            for (uint8_t rot = 0; rot < fieldValues; rot++)
            {
                // loop through the counts of each 128-bit color value
                for (uint8_t i = 0; i < fieldValues; i++)
                {
                    for (uint8_t b = 0; b < 7; b++)
                    {
                        // Say there's 217 instances of value 63....  i.e. counts[e][63] == 217
                        // for rotation 0, that would mean bit counts of (MSB)  0, 217, 217, 217, 217, 217, 217 (LSB)
                        // for rotation 1, that would mean bit counts of (MSB) 217,  0,   0,   0,   0,   0,   0 (LSB)
                        const uint8_t rotatedValue = (i + rot) % fieldValues;
                        const uint8_t rotatedValueBitB = (rotatedValue >> b) & 1u;
                        cbits[e][rot][b] += counts[e][i] * rotatedValueBitB;

                        // when rot == 1, and all those 63s become 64s, 
                        // when i = 63, those bits are found: counts[e][63] * ((64 >> b) & 1)
                        //                                  =    217 values of 64
                    }
                }
            }

            size_t bestEntropy = 0;
            uint8_t bestShift = 0;

            //size_t bitFlipWeights[] = { 1, 1, 1, 1, 1, 1, 1 };
            size_t bitFlipWeights[] = { 1, 2, 4, 8, 16, 32, 64 };
            //size_t bitFlipWeights[] = { 1, 10, 100, 1000, 10000, 100000, 1000000 };
            //size_t bitFlipWeights[] = { 1, 1ull << 2, 1ull << 4, 1ull << 6, 1ull << 8, 1ull << 10, 1ull << 12, 1u << 14 };
            //size_t bitFlipWeights[] = { 1, 1ull << 3, 1ull << 6, 1ull << 9, 1ull << 12, 1ull << 15, 1ull << 18 };

            for (uint8_t rot = 0; rot < fieldValues; rot++)
            {
                size_t bitFlips[7] = {};
                for (int b = 0; b < 7; b++)
                {
                    bitFlips[b] = std::min<size_t>(cbits[e][rot][b], countTotals[e] - cbits[e][rot][b]);
                }
                size_t entropy = bitFlipWeights[6] * bitFlips[6] + bitFlipWeights[5] * bitFlips[5] + bitFlipWeights[4] * bitFlips[4] + bitFlipWeights[3] * bitFlips[3] + bitFlipWeights[2] * bitFlips[2] + bitFlipWeights[1] * bitFlips[1] + bitFlipWeights[0] * bitFlips[0];
                if (rot == 0)
                {
                    bestEntropy = entropy;
                }
                else if (entropy < bestEntropy)
                {
                    bestEntropy = entropy;
                    bestShift = rot;
                }
            }
            rotationsOut[e] = bestShift;
        }
    }
}


void BC7_ModeJoin_Shuffle_Slow(uint8_t mode, InplaceShiftBitsSequence& sequence, const void* src, void* dest)
{
    UNREFERENCED_PARAMETER(mode); // useful for debugging
    uint64_t mask[2] = {};
    size_t bitsProcessed = 0;

    size_t nextReadBit = 0;

    uint64_t se[2] = { reinterpret_cast<const uint64_t*>(src)[0], reinterpret_cast<const uint64_t*>(src)[1] };
    uint64_t de[2] = {};

#if _DEBUG
    const BC7m0* src0 = reinterpret_cast<const BC7m0*>(src); UNREFERENCED_PARAMETER(src0);
    const BC7m1* src1 = reinterpret_cast<const BC7m1*>(src); UNREFERENCED_PARAMETER(src1);
    const BC7m2* src2 = reinterpret_cast<const BC7m2*>(src); UNREFERENCED_PARAMETER(src2);
    const BC7m3* src3 = reinterpret_cast<const BC7m3*>(src); UNREFERENCED_PARAMETER(src3);
    const BC7m4* src4 = reinterpret_cast<const BC7m4*>(src); UNREFERENCED_PARAMETER(src4);
    const BC7m5* src5 = reinterpret_cast<const BC7m5*>(src); UNREFERENCED_PARAMETER(src5);
    const BC7m6* src6 = reinterpret_cast<const BC7m6*>(src); UNREFERENCED_PARAMETER(src6);
    const BC7m7* src7 = reinterpret_cast<const BC7m7*>(src); UNREFERENCED_PARAMETER(src7);
#endif
    

    for (size_t o = 0; o < sequence.OpCount; o++)
    {
        const InplaceShiftBitsOrder& op = sequence.Ops[o];
        const size_t copyCount = op.CopyBitCount;
        assert(copyCount <= 64);

        uint64_t bits = 0;

        // collect bits
        {
            const size_t firstReadBit = nextReadBit;
            const size_t lastReadBit = firstReadBit + copyCount - 1;
            if (firstReadBit / 64 != lastReadBit / 64)
            {
                const size_t firstCopyCount = 64u - firstReadBit;
                const size_t secondCopyCount = lastReadBit - 63u;

                bits = (se[0] >> firstReadBit);                                               // high bits from lower uint64, in low bit area
                bits |= (se[1] & (~0ull >> (64u - secondCopyCount))) << firstCopyCount;     // low bits from high uint64
            }
            else
            {
                bits = (se[firstReadBit / 64u] >> (firstReadBit % 64u)) & (~0ull >> (64u - copyCount));
            }
        }

        // write bits
        {
            const size_t firstWriteBit = op.DestinationBitOffset;
            const size_t lastWriteBit = firstWriteBit + copyCount - 1;

            if (firstWriteBit / 64u != lastWriteBit / 64u)
            {
                const size_t firstCopyCount = 64u - firstWriteBit;
                const size_t secondCopyCount = lastWriteBit - 63u;

                de[0] |= (bits << firstWriteBit);
                mask[0] |= (~0ull << firstWriteBit);

                de[1] |= (bits >> firstCopyCount);
                mask[1] |= (~0ull >> (64 - secondCopyCount));
            }
            else
            {
                de[firstWriteBit / 64u] |= (bits << (firstWriteBit % 64u));
                mask[firstWriteBit / 64u] |= ((~0ull >> (64 - copyCount)) << (firstWriteBit % 64u));
            }
        }
        bitsProcessed += copyCount;
        nextReadBit += copyCount;
    }

    assert(mask[0] == ~0ull && mask[1] == ~0ull && bitsProcessed == 128);
    assert(__popcnt64(se[0]) + __popcnt64(se[1]) == __popcnt64(de[0]) + __popcnt64(de[1]));

    reinterpret_cast<uint64_t*>(dest)[0] = de[0];
    reinterpret_cast<uint64_t*>(dest)[1] = de[1];
}

void BC7_ModeJoin_Unshuffle_Slow(uint8_t mode, InplaceShiftBitsSequence& sequence, const void* src, void* dest)
{
    UNREFERENCED_PARAMETER(mode); // useful for debugging
    uint64_t mask[2] = {};
    size_t bitsProcessed = 0;

    size_t nextWriteBit = 0;

    uint64_t se[2] = { reinterpret_cast<const uint64_t*>(src)[0], reinterpret_cast<const uint64_t*>(src)[1] };
    uint64_t de[2] = {};

#if _DEBUG   /*  useful for debugging */
    const BC7m0* src0 = reinterpret_cast<const BC7m0*>(src); UNREFERENCED_PARAMETER(src0);
    const BC7m1* src1 = reinterpret_cast<const BC7m1*>(src); UNREFERENCED_PARAMETER(src1);
    const BC7m2* src2 = reinterpret_cast<const BC7m2*>(src); UNREFERENCED_PARAMETER(src2);
    const BC7m3* src3 = reinterpret_cast<const BC7m3*>(src); UNREFERENCED_PARAMETER(src3);
    const BC7m4* src4 = reinterpret_cast<const BC7m4*>(src); UNREFERENCED_PARAMETER(src4);
    const BC7m5* src5 = reinterpret_cast<const BC7m5*>(src); UNREFERENCED_PARAMETER(src5);
    const BC7m6* src6 = reinterpret_cast<const BC7m6*>(src); UNREFERENCED_PARAMETER(src6);
    const BC7m7* src7 = reinterpret_cast<const BC7m7*>(src); UNREFERENCED_PARAMETER(src7);
#endif

    for (size_t o = 0; o < sequence.OpCount; o++)
    {
        const InplaceShiftBitsOrder& op = sequence.Ops[o];
        const size_t copyCount = op.CopyBitCount;
        assert(copyCount <= 64);

        uint64_t bits = 0;

        // Read\collect bits
        {
            const size_t firstReadBit = op.DestinationBitOffset;
            const size_t lastReadBit = firstReadBit + copyCount - 1;

            if (firstReadBit / 64u != lastReadBit / 64u)
            {
                const size_t firstCopyCount = 64u - firstReadBit;
                const size_t secondCopyCount = lastReadBit - 63u;

                bits = (se[0] >> firstReadBit);                                               // high bits from lower uint64, in low bit area
                bits |= ((se[1] & (~0ull >> (64u - secondCopyCount))) << firstCopyCount);    // low bits from high uint64

            }
            else
            {
                bits = (se[firstReadBit / 64u] >> (firstReadBit % 64u)) & (~0ull >> (64u - copyCount));
            }
        }


        // write collected bits
        {
            const size_t firstWriteBit = nextWriteBit;
            const size_t lastWriteBit = firstWriteBit + copyCount - 1;
            if (firstWriteBit / 64 != lastWriteBit / 64)
            {
                const size_t firstCopyCount = 64u - firstWriteBit;
                const size_t secondCopyCount = lastWriteBit - 63u;

                de[0] |= (bits << firstWriteBit);                                   // low bits get placed in the high portion of the first block
                mask[0] |= (~0ull << firstWriteBit);

                de[1] |= (bits >> firstCopyCount);                                  // remainder go into the low portion of the second block
                mask[1] |= (~0ull >> (64 - secondCopyCount));
            }
            else
            {
                de[firstWriteBit / 64u] |= (bits << (firstWriteBit % 64u));
                mask[firstWriteBit / 64u] |= ((~0ull >> (64 - copyCount)) << (firstWriteBit % 64u));
            }
        }

        bitsProcessed += copyCount;
        nextWriteBit += copyCount;
    }

    assert(mask[0] == ~0ull && mask[1] == ~0ull && bitsProcessed == 128);
    assert(__popcnt64(se[0]) + __popcnt64(se[1]) == __popcnt64(de[0]) + __popcnt64(de[1]));

    memcpy_s(dest, 16, de, 16);
}
} // namespace


void BC7_ModeJoin_RotateColors(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, bool altEncoding, size_t regionSize)
{
    const size_t block_count = srcSize / 16;
    const size_t regions = (srcSize + regionSize - 1) / regionSize;

    // color point counts for modes with only RGB
    std::vector<size_t[6][128]> endpointValueCounts_3C_RGB(regions);

    // color point counts for mode with RGBA, but encoding A sepereate in "Alternate" mode
    std::vector<size_t[6][128]> endpointValueCounts_4C_RGB(regions);
    std::vector<size_t[2][128]> endpointValueCounts_4C_A(regions);

    // color point counts for modes with RGBA, encoded as a single mixed endpoint pair
    std::vector<size_t[8][128]> endpointValueCounts_4C_RGBA(regions);

    std::vector<size_t> elementCounts(regions);
    std::vector<uint8_t[8]> rotationDefault(regions);
    std::vector<uint8_t[8]> rotationAlt(regions);

    // first pass, record color points
    const uint8_t* pSrc = src;
    const bool weightEndpoints = false;                                         // TODO: investigate if there are any color rotation savings for join-mode
    for (size_t i = 0; i < block_count; ++i, pSrc += 16)
    {
        const size_t r = (i * 16) / regionSize;
        elementCounts[r]++;
        // Look for first set bit in [7:0].  If none set, return 8 (invalid).
        unsigned long mode;
        _BitScanForward(&mode, *pSrc | 0x100u);

        if (0 == mode && 0)
        {
            const BC7m0* b0 = reinterpret_cast<const BC7m0*>(pSrc);
            // Mode 0 is 4 bit endpoints, shift left 3 bits into 7 bit color space
            // endpaint pair is 24 bits, weighted 3 (bytes)
            endpointValueCounts_3C_RGB[r][0][b0->R0 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][1][b0->R1 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][0][b0->R2 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][1][b0->R3 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][0][b0->R4 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][1][b0->R5 << 3] += (weightEndpoints ? 3 : 1);

            endpointValueCounts_3C_RGB[r][2][b0->G0 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][3][b0->G1 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][2][b0->G2 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][3][b0->G3 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][2][b0->G4 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][3][b0->G5 << 3] += (weightEndpoints ? 3 : 1);

            endpointValueCounts_3C_RGB[r][4][b0->B0 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][5][b0->B1 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][4][b0->B2() << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][5][b0->B3 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][4][b0->B4 << 3] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][5][b0->B5 << 3] += (weightEndpoints ? 3 : 1);
        }
        else if (1 == mode && 0)
        {
            const BC7m1* b1 = reinterpret_cast<const BC7m1*>(pSrc);
            // Mode 1 is 6 bit endpoints, shift left 1 bit into 7 bit color space
            // endpoint pair is 36 bits, weight 4
            endpointValueCounts_3C_RGB[r][0][b1->R0 << 1] += (weightEndpoints ? 4 : 1);
            endpointValueCounts_3C_RGB[r][1][b1->R1 << 1] += (weightEndpoints ? 4 : 1);
            endpointValueCounts_3C_RGB[r][0][b1->R2 << 1] += (weightEndpoints ? 4 : 1);
            endpointValueCounts_3C_RGB[r][1][b1->R3 << 1] += (weightEndpoints ? 4 : 1);

            endpointValueCounts_3C_RGB[r][2][b1->G0 << 1] += (weightEndpoints ? 4 : 1);
            endpointValueCounts_3C_RGB[r][3][b1->G1 << 1] += (weightEndpoints ? 4 : 1);
            endpointValueCounts_3C_RGB[r][2][b1->G2 << 1] += (weightEndpoints ? 4 : 1);
            endpointValueCounts_3C_RGB[r][3][b1->G3 << 1] += (weightEndpoints ? 4 : 1);

            endpointValueCounts_3C_RGB[r][4][b1->B0 << 1] += (weightEndpoints ? 4 : 1);
            endpointValueCounts_3C_RGB[r][5][b1->B1() << 1] += (weightEndpoints ? 4 : 1);
            endpointValueCounts_3C_RGB[r][4][b1->B2 << 1] += (weightEndpoints ? 4 : 1);
            endpointValueCounts_3C_RGB[r][5][b1->B3 << 1] += (weightEndpoints ? 4 : 1);
        }
        else if (2 == mode && 0)
        {
            const BC7m2* b2 = reinterpret_cast<const BC7m2*>(pSrc);
            // Mode 2 is 5 bit endpoints, shift left 2 bit into 7 bit color space
            // endpoint pair is 30 bits, weight 3
            endpointValueCounts_3C_RGB[r][0][b2->R0 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][1][b2->R1 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][0][b2->R2 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][1][b2->R3 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][0][b2->R4 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][1][b2->R5 << 2] += (weightEndpoints ? 3 : 1);

            endpointValueCounts_3C_RGB[r][2][b2->G0 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][3][b2->G1 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][2][b2->G2 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][3][b2->G3 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][2][b2->G4 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][3][b2->G5 << 2] += (weightEndpoints ? 3 : 1);

            endpointValueCounts_3C_RGB[r][4][b2->B0 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][5][b2->B1 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][4][b2->B2 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][5][b2->B3 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][4][b2->B4 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_3C_RGB[r][5][b2->B5 << 2] += (weightEndpoints ? 3 : 1);
        }
        else if (3 == mode && 0)
        {
            const BC7m3* b3 = reinterpret_cast<const BC7m3*>(pSrc);
            // Mode 3 is 7 bit endpoints, shift left 0 bits into 7 bit color space
            // endpoint pair is 42 bits, weight 5
            endpointValueCounts_3C_RGB[r][0][b3->R0 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_3C_RGB[r][1][b3->R1 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_3C_RGB[r][0][b3->R2 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_3C_RGB[r][1][b3->R3 << 0] += (weightEndpoints ? 5 : 1);

            endpointValueCounts_3C_RGB[r][2][b3->G0 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_3C_RGB[r][3][b3->G1 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_3C_RGB[r][2][b3->G2 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_3C_RGB[r][3][b3->G3() << 0] += (weightEndpoints ? 5 : 1);

            endpointValueCounts_3C_RGB[r][4][b3->B0 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_3C_RGB[r][5][b3->B1 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_3C_RGB[r][4][b3->B2 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_3C_RGB[r][5][b3->B3 << 0] += (weightEndpoints ? 5 : 1);
        }
        else if (4 == mode && 0)
        {
            const BC7m4_Derotated* b4 = reinterpret_cast<const BC7m4_Derotated*>(pSrc);
            // Mode 4 encodes as 5 bit color values (after deroation), 4 channels, shift 2 required
            // endpoint pair is 40 bits for 4 channel combination, weight 5
            // endpoint pair is 30/14 bits for 4/1 channel combination, weight 3/1
            endpointValueCounts_4C_RGBA[r][0][b4->R0 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][1][b4->R1 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][2][b4->G0 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][3][b4->G1 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][4][b4->B0 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][5][b4->B1 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][6][b4->A0 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][7][b4->A1 << 2] += (weightEndpoints ? 5 : 1);

            endpointValueCounts_4C_RGB[r][0][b4->R0 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][1][b4->R1 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][2][b4->G0 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][3][b4->G1 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][4][b4->B0 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][5][b4->B1 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_A[r][0][b4->A0 << 2] += 1;
            endpointValueCounts_4C_A[r][1][b4->A1 << 2] += 1;
        }
        else if (5 == mode)
        {
            const BC7m5_Derotated* b5 = reinterpret_cast<const BC7m5_Derotated*>(pSrc);
            // Mode 5 has 7 bit color points, no shift required
            // 4 channel   : endpoint pair is 56 bits, weight 7
            // 3+1 channel : endpoint pair is 42/14+4 bits, weight 5/2
            endpointValueCounts_4C_RGBA[r][0][b5->R0 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][1][b5->R1 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][2][b5->G0 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][3][b5->G1 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][4][b5->B0 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][5][b5->B1 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][6][b5->A0 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][7][b5->A1() << 0] += (weightEndpoints ? 7 : 1);

            endpointValueCounts_4C_RGB[r][0][b5->R0 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGB[r][1][b5->R1 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGB[r][2][b5->G0 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGB[r][3][b5->G1 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGB[r][4][b5->B0 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGB[r][5][b5->B1 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_A[r][0][b5->A0 << 0] += 2;
            endpointValueCounts_4C_A[r][1][b5->A1() << 0] += 2;
        }
        else if (6 == mode)
        {
            const BC7m6* b6 = reinterpret_cast<const BC7m6*>(pSrc);

            // Mode 6 has 7 bit color points, no shift required
            // 4 channel   : endpoint pair is 56 bits, weight 7
            // 3+1 channel : endpoint pair is 42/14+4 bits, weight 4/2
            endpointValueCounts_4C_RGBA[r][0][b6->R0 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][1][b6->R1 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][2][b6->G0 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][3][b6->G1 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][4][b6->B0 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][5][b6->B1 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][6][b6->A0 << 0] += (weightEndpoints ? 7 : 1);
            endpointValueCounts_4C_RGBA[r][7][b6->A1 << 0] += (weightEndpoints ? 7 : 1);

            endpointValueCounts_4C_RGB[r][0][b6->R0 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGB[r][1][b6->R1 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGB[r][2][b6->G0 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGB[r][3][b6->G1 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGB[r][4][b6->B0 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGB[r][5][b6->B1 << 0] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_A[r][0][b6->A0 << 0] += 2;
            endpointValueCounts_4C_A[r][1][b6->A1 << 0] += 2;
        }
        else if (7 == mode && 0)
        {
            const BC7m7* b7 = reinterpret_cast<const BC7m7*>(pSrc);

            // Mode 7 has 5 bit color points, shift 2 required into 7 bit color space
            // 4 channel   : endpoint pair is 40 bits, weight 5
            // 3+1 channel : endpoint pair is 30/10+4 bits, weight 3/1
            endpointValueCounts_4C_RGBA[r][0][b7->R0 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][1][b7->R1 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][0][b7->R2 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][1][b7->R3 << 2] += (weightEndpoints ? 5 : 1);

            endpointValueCounts_4C_RGBA[r][2][b7->G0 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][3][b7->G1 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][2][b7->G2 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][3][b7->G3 << 2] += (weightEndpoints ? 5 : 1);

            endpointValueCounts_4C_RGBA[r][4][b7->B0 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][5][b7->B1 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][4][b7->B2 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][5][b7->B3 << 2] += (weightEndpoints ? 5 : 1);

            endpointValueCounts_4C_RGBA[r][6][b7->A0 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][7][b7->A1 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][6][b7->A2 << 2] += (weightEndpoints ? 5 : 1);
            endpointValueCounts_4C_RGBA[r][7][b7->A3 << 2] += (weightEndpoints ? 5 : 1);


            endpointValueCounts_4C_RGB[r][0][b7->R0 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][1][b7->R1 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][0][b7->R2 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][1][b7->R3 << 2] += (weightEndpoints ? 3 : 1);

            endpointValueCounts_4C_RGB[r][2][b7->G0 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][3][b7->G1 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][2][b7->G2 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][3][b7->G3 << 2] += (weightEndpoints ? 3 : 1);

            endpointValueCounts_4C_RGB[r][4][b7->B0 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][5][b7->B1 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][4][b7->B2 << 2] += (weightEndpoints ? 3 : 1);
            endpointValueCounts_4C_RGB[r][5][b7->B3 << 2] += (weightEndpoints ? 3 : 1);

            endpointValueCounts_4C_A[r][0][b7->A0 << 2] += 1;
            endpointValueCounts_4C_A[r][1][b7->A1 << 2] += 1;
            endpointValueCounts_4C_A[r][0][b7->A2 << 2] += 1;
            endpointValueCounts_4C_A[r][1][b7->A3 << 2] += 1;
        }
    }


    if (regions > 1)
    {
        for (uint32_t r = 0; r < regions; r++)
        {
            // window size cap
            if (regionSize < size_t(256 * 1024))
            {
                ComputeRotationToStableRange7bit_JoinMode(endpointValueCounts_3C_RGB[r], endpointValueCounts_4C_RGBA[r], endpointValueCounts_4C_RGB[r], endpointValueCounts_4C_A[r], elementCounts[r], rotationDefault[r], rotationAlt[r]);
            }
            else
            {
                ComputeRotationToLeastSignificantEntropy7bit_JoinMode(endpointValueCounts_3C_RGB[r], endpointValueCounts_4C_RGBA[r], endpointValueCounts_4C_RGB[r], endpointValueCounts_4C_A[r], elementCounts[r], rotationDefault[r], rotationAlt[r]);
            }
        }
    }
    else
    {
        ComputeRotationToLeastSignificantEntropy7bit_JoinMode(endpointValueCounts_3C_RGB[0], endpointValueCounts_4C_RGBA[0], endpointValueCounts_4C_RGB[0], endpointValueCounts_4C_A[0], elementCounts[0], rotationDefault[0], rotationAlt[0]);
    }



    // second pass, apply rotations
    dest.resize(srcSize);
    uint8_t* pDest = dest.data();
    pSrc = src;
    for (size_t i = 0; i < block_count; ++i, pSrc += 16, pDest += 16)
    {
        const size_t r = (i * 16) / regionSize;
        // Look for first set bit in [7:0].  If none set, return 8 (invalid).
        unsigned long mode;
        _BitScanForward(&mode, *pSrc | 0x100u);

        switch (mode) {
        case 0:
            BC7m0 b0 = *reinterpret_cast<const BC7m0*>(pSrc);
            b0.ApplyRotation7bit(altEncoding ? rotationAlt[r] : rotationDefault[r]);        // TODO consider adjusting to non 7bit rotations
            *reinterpret_cast<BC7m0*>(pDest) = b0;
            break;

        case 1:
            BC7m1 b1 = *reinterpret_cast<const BC7m1*>(pSrc);
            b1.ApplyRotation7bit(altEncoding ? rotationAlt[r] : rotationDefault[r]);
            *reinterpret_cast<BC7m1*>(pDest) = b1;
            break;


        case 2:
            BC7m2 b2 = *reinterpret_cast<const BC7m2*>(pSrc);
            b2.ApplyRotation7bit(altEncoding ? rotationAlt[r] : rotationDefault[r]);
            *reinterpret_cast<BC7m2*>(pDest) = b2;
            break;

        case 3:
            BC7m3 b3 = *reinterpret_cast<const BC7m3*>(pSrc);
            b3.ApplyRotation7bit(altEncoding ? rotationAlt[r] : rotationDefault[r]);
            *reinterpret_cast<BC7m3*>(pDest) = b3;
            break;


        case 4:
            BC7m4_Derotated b4 = *reinterpret_cast<const BC7m4_Derotated*>(pSrc);
            b4.ApplyRotation7bit(altEncoding ? rotationAlt[r] : rotationDefault[r]);
            *reinterpret_cast<BC7m4_Derotated*>(pDest) = b4;
            break;

        case 5:
            BC7m5_Derotated b5 = *reinterpret_cast<const BC7m5_Derotated*>(pSrc);
            b5.ApplyRotation7bit(altEncoding ? rotationAlt[r] : rotationDefault[r]);
            *reinterpret_cast<BC7m5_Derotated*>(pDest) = b5;
            break;

        case 6:
            BC7m6 b6 = *reinterpret_cast<const BC7m6*>(pSrc);
            b6.ApplyRotation7bit(altEncoding ? rotationAlt[r] : rotationDefault[r]);
            *reinterpret_cast<BC7m6*>(pDest) = b6;
            break;

        case 7:
            BC7m7 b7 = *reinterpret_cast<const BC7m7*>(pSrc);
            b7.ApplyRotation7bit(altEncoding ? rotationAlt[r] : rotationDefault[r]);
            *reinterpret_cast<BC7m7*>(pDest) = b7;
            break;

        default:
            *reinterpret_cast<__m128i*>(pDest) = *reinterpret_cast<const __m128i*>(pSrc);

        }
    }
}


void BC7_ModeJoin_Transform(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics, std::vector<uint8_t>& controlBytes)
{
    UNREFERENCED_PARAMETER(controlBytes); // eventually used for exporting transform parameters
    const size_t block_count = srcSize / 16;

    dest.resize(srcSize);

    uint8_t* pDest = dest.data();
    uint8_t fastFunc = opt.AltEncode ? 1 : 0;

    InplaceShiftBitsSequence patterns[8] = {};
    for (size_t m = 0; m < 8; m++)
    {
        if (opt.AltEncode)
        {
            BC7_ModeJoinAlt_Shuffle_OpLists[m](patterns[m], opt, metrics);
        }
        else
        {
            BC7_ModeJoin_Shuffle_OpLists[m](patterns[m], opt, metrics);
        }
    }

    for (size_t i = 0; i < block_count; ++i, src += 16, pDest += 16)
    {
        uint64_t d[2] = {};

        // Look for first set bit in [7:0].  If none set, return 8 (invalid).
        unsigned long mode;
        _BitScanForward(&mode, *src | 0x100u); 

        if (mode == 8)
        {
            // direct copy, shifting by 12 bytes
            const uint64_t* s64 = reinterpret_cast<const uint64_t*>(src);
            d[0] = (s64[0] >> 32) | (s64[1] << 32);
            d[1] = (s64[1] >> 32) | (s64[0] << 32);
        }
        else if (BC7_ModeJoin_Shuffle_Fast[mode][fastFunc])
        {
            BC7_ModeJoin_Shuffle_Fast[mode][fastFunc](src, d, metrics);
        }
        else
        {
            BC7_ModeJoin_Shuffle_Slow((uint8_t)mode, patterns[mode], src, d);
        }

        // This to to shift all bit patterns 24 bits, which improves compression ~1.3%
		// TODO: revisit after measuring on a larger texture corpus
        uint64_t sh = 24;
        uint64_t ds[2] = { (d[0] << sh) | (d[1] >> (64 - sh)) , (d[1] << sh) | (d[0] >> (64 - sh)) };
        memcpy_s(pDest, 16, ds, 16);
    }
}

void BC7_ModeJoin_Reverse(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics, std::vector<uint8_t>& controlBytes)
{
    UNREFERENCED_PARAMETER(controlBytes); // eventually used for exporting transform parameters
    const size_t block_count = srcSize / 16;

    dest.resize(srcSize);
    uint8_t* pDest = dest.data();

    InplaceShiftBitsSequence patterns[8] = {};
    for (size_t m = 0; m < 8; m++)
    {
        if (opt.AltEncode)
        {
            BC7_ModeJoinAlt_Shuffle_OpLists[m](patterns[m], opt, metrics);
        }
        else
        {
            BC7_ModeJoin_Shuffle_OpLists[m](patterns[m], opt, metrics);
        }
    }

    for (size_t i = 0; i < block_count; ++i, src += 16, pDest += 16)
    {
        // This to to shift all bit patterns 24 bits, which improves compression ~1.3%
		// TODO: revisit after measuring on a larger texture corpus
        uint64_t s[2] = { }; 
		memcpy_s(s, 16, src, 16);
        uint64_t sh = 24;
        uint64_t ss[2] = { (s[0] >> sh) | (s[1] << (64 - sh)) , (s[1] >> sh) | (s[0] << (64 - sh)) };
        uint8_t* srcd = (uint8_t*)ss;

        unsigned long mode;
        if (opt.AltEncode)
        {
            //  in alt pattern, the first 4 mode bits are at 96b, byte 12, and the next four are at 48b, byte 6
            uint8_t m = (srcd[12] & 0x0f) | (srcd[6] << 4);
            _BitScanForward(&mode, m | 0x100u);
        }
        else
        {
            //  in default pattern, mode starts at bit 96, byte 12
            _BitScanForward(&mode, srcd[12] | 0x100u);
        }

        if (mode == 8)
        {
            for (size_t b = 0; b < 16; b++) 
            {
                pDest[b] = srcd[(b + 12) % 16];
            }
        }
        else
        {
            BC7_ModeJoin_Unshuffle_Slow((uint8_t)mode, patterns[mode], srcd, pDest);
        }
    }
}


