//--------------------------------------------------------------------------------------
// EndpointOrderingHelpers.hlsli
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

void reorderEndpointFields0(uint scrap, uint endpointOrderBytes, inout uint4 rawOut)
{
    // Mode 0
    // 1 bit mode                                                               =>  1 bit
    // 4 bit partition                                                          =>  4 bits
    // 4 bits R0 - 4 bits R1 - 4 bits R2 - 4 bits R3 - 4 bits R4 - 4 bits R5    => 24 bits
    // 4 bits G0 - 4 bits G1 - 4 bits G2 - 4 bits G3 - 4 bits G4 - 4 bits G5    => 24 bits
    // 4 bits B0 - 4 bits B1 - 4 bits B2 - 4 bits B3 - 4 bits B4 - 4 bits B5    => 24 bits
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    uint raw2 = 0u;
    
    // Write the first bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0x1Fu, 0 /*5 bit(s)*/, raw0);
    
    // R0-1-2-3-4-5
    uint R0 = (rawOut[0] >> 5) & 0xFu;
    uint R1 = (rawOut[0] >> 9) & 0xFu;
    uint R2 = (rawOut[0] >> 13) & 0xFu;
    uint R3 = (rawOut[0] >> 17) & 0xFu;
    uint R4 = (rawOut[0] >> 21) & 0xFu;
    uint R5 = (rawOut[0] >> 25) & 0xFu;
    
    // G0-1-2-3-4-5
    uint G0 = ((rawOut[1] & 0x1u) << 3) | (rawOut[0] >> 29);
    uint G1 = (rawOut[1] >> 1) & 0xFu;
    uint G2 = (rawOut[1] >> 5) & 0xFu;
    uint G3 = (rawOut[1] >> 9) & 0xFu;
    uint G4 = (rawOut[1] >> 13) & 0xFu;
    uint G5 = (rawOut[1] >> 17) & 0xFu;
    
    // B0-1-2-3-4-5
    uint B0 = (rawOut[1] >> 21) & 0xFu;
    uint B1 = (rawOut[1] >> 25) & 0xFu;
    uint B2 = ((rawOut[2] & 0x1u) << 3) | (rawOut[1] >> 29);
    uint B3 = (rawOut[2] >> 1) & 0xFu;
    uint B4 = (rawOut[2] >> 5) & 0xFu;
    uint B5 = (rawOut[2] >> 9) & 0xFu;
    
    uint quad0 = (scrap & 0x1u) ? (endpointOrderBytes & 0x000Fu) : 0u;
    uint quad1 = (scrap & 0x2u) ? (endpointOrderBytes & 0x00F0u) >> 4 : 0u;
    uint quad2 = (scrap & 0x4u) ? (endpointOrderBytes & 0x0F00u) >> 8 : 0u;
    uint quad3 = (scrap & 0x8u) ? (endpointOrderBytes & 0xF000u) >> 12 : 0u;
    uint mask = ((quad0 ^ quad1) ^ quad2) ^ quad3;
    
    if (mask & 0x1u)
    {
        WriteBitsToDword(R1, 5  /*4 bit(s)*/, raw0);
        WriteBitsToDword(R0, 9  /*4 bit(s)*/, raw0);
        WriteBitsToDword(R3, 13 /*4 bit(s)*/, raw0);
        WriteBitsToDword(R2, 17 /*4 bit(s)*/, raw0);
        WriteBitsToDword(R5, 21 /*4 bit(s)*/, raw0);
        WriteBitsToDword(R4, 25 /*4 bit(s)*/, raw0);
    }
    else
    {
        WriteBitsToDword(R0, 5  /*4 bit(s)*/, raw0);
        WriteBitsToDword(R1, 9  /*4 bit(s)*/, raw0);
        WriteBitsToDword(R2, 13 /*4 bit(s)*/, raw0);
        WriteBitsToDword(R3, 17 /*4 bit(s)*/, raw0);
        WriteBitsToDword(R4, 21 /*4 bit(s)*/, raw0);
        WriteBitsToDword(R5, 25 /*4 bit(s)*/, raw0);
    }
    
    if (mask & 0x2u)
    {
        WriteBitsToDword((G1 & 0x7u),   29 /*3 bit(s)*/, raw0);
        WriteBitsToDword((G1 >> 3),     0  /*1 bit(s)*/, raw1);
        WriteBitsToDword(G0,            1  /*4 bit(s)*/, raw1);
        WriteBitsToDword(G3,            5  /*4 bit(s)*/, raw1);
        WriteBitsToDword(G2,            9  /*4 bit(s)*/, raw1);
        WriteBitsToDword(G5,            13 /*4 bit(s)*/, raw1);
        WriteBitsToDword(G4,            17 /*4 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword((G0 & 0x7u),   29 /*3 bit(s)*/, raw0);
        WriteBitsToDword((G0 >> 3),     0  /*1 bit(s)*/, raw1);
        WriteBitsToDword(G1,            1  /*4 bit(s)*/, raw1);
        WriteBitsToDword(G2,            5  /*4 bit(s)*/, raw1);
        WriteBitsToDword(G3,            9  /*4 bit(s)*/, raw1);
        WriteBitsToDword(G4,            13 /*4 bit(s)*/, raw1);
        WriteBitsToDword(G5,            17 /*4 bit(s)*/, raw1);
    }
    
    if (mask & 0x4u)
    {
        WriteBitsToDword(B1,            21 /*4 bit(s)*/, raw1);
        WriteBitsToDword(B0,            25 /*4 bit(s)*/, raw1);
        WriteBitsToDword((B3 & 0x7u),   29 /*3 bit(s)*/, raw1);
        WriteBitsToDword((B3 >> 3),     0  /*1 bit(s)*/, raw2);
        WriteBitsToDword(B2,            1  /*4 bit(s)*/, raw2);
        WriteBitsToDword(B5,            5  /*4 bit(s)*/, raw2);
        WriteBitsToDword(B4,            9  /*4 bit(s)*/, raw2);
    }
    else
    {
        WriteBitsToDword(B0,            21 /*4 bit(s)*/, raw1);
        WriteBitsToDword(B1,            25 /*4 bit(s)*/, raw1);
        WriteBitsToDword((B2 & 0x7u),   29 /*3 bit(s)*/, raw1);
        WriteBitsToDword((B2 >> 3),     0  /*1 bit(s)*/, raw2);
        WriteBitsToDword(B3,            1  /*4 bit(s)*/, raw2);
        WriteBitsToDword(B4,            5  /*4 bit(s)*/, raw2);
        WriteBitsToDword(B5,            9  /*4 bit(s)*/, raw2);
    }

    // Copy the last bits (after color) of rawOut[2] to raw2
    WriteBitsToDword((rawOut[2] >> 13) & 0x7u,  13 /*2 bit(s)*/, raw2);
    WriteBitsToDword(rawOut[2] >> 16,           16 /*8 bit(s)*/, raw2);
    WriteBitsToDword(rawOut[2] >> 24,           24 /*8 bit(s)*/, raw2);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
    rawOut[2] = raw2;
}

void reorderEndpointFields1(uint scrap, uint endpointOrderBytes, inout uint4 rawOut)
{
    // Mode 1
    // 2 bit mode                                       =>  2 bits
    // 6 bit partition                                  =>  6 bits
    // 6 bits R0 - 6 bits R1 - 6 bits R2 - 6 bits R3    => 24 bits
    // 6 bits B0 - 6 bits G1 - 6 bits R2 - 6 bits G3    => 24 bits
    // 6 bits G0 - 6 bits B1 - 6 bits R2 - 6 bits B3    => 24 bits
    // 1 bit  P0 - 1 bit  P1                            =>  2 bits
    // 46 bits index                                    => 46 bits
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    uint raw2 = 0u;
    
    // Write the first bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0xFFu, 0 /*8 bit(s)*/, raw0);
    
    // De-Rotate
    // R0-1-2-3-4-5
    uint R0 = (rawOut[0] >> 8) & 0x3Fu;
    uint R1 = (rawOut[0] >> 14) & 0x3Fu;
    uint R2 = (rawOut[0] >> 20) & 0x3Fu;
    uint R3 = (rawOut[0] >> 26) & 0x3Fu;
    
    // G0-1-2-3-4-5
    uint G0 = rawOut[1] & 0x3Fu;
    uint G1 = (rawOut[1] >> 6) & 0x3Fu;
    uint G2 = (rawOut[1] >> 12) & 0x3Fu;
    uint G3 = (rawOut[1] >> 18) & 0x3Fu;
    
    // B0-1-2-3-4-5
    uint B0 = (rawOut[1] >> 24) & 0x3Fu;
    uint B1 = ((rawOut[2] & 0xFu) << 2) | (rawOut[1] >> 30);
    uint B2 = (rawOut[2] >> 4) & 0x3Fu;
    uint B3 = (rawOut[2] >> 10) & 0x3Fu;
    
    uint quad0 = (scrap & 0x1u) ? (endpointOrderBytes & 0x000Fu) : 0u;
    uint quad1 = (scrap & 0x2u) ? (endpointOrderBytes & 0x00F0u) >> 4 : 0u;
    uint quad2 = (scrap & 0x4u) ? (endpointOrderBytes & 0x0F00u) >> 8 : 0u;
    uint quad3 = (scrap & 0x8u) ? (endpointOrderBytes & 0xF000u) >> 12 : 0u;
    uint mask = ((quad0 ^ quad1) ^ quad2) ^ quad3;
    
    if (mask & 0x1u)
    {
        WriteBitsToDword(R1, 8  /*6 bit(s)*/, raw0);
        WriteBitsToDword(R0, 14 /*6 bit(s)*/, raw0);
        WriteBitsToDword(R3, 20 /*6 bit(s)*/, raw0);
        WriteBitsToDword(R2, 26 /*6 bit(s)*/, raw0);
    }
    else
    {
        WriteBitsToDword(R0, 8  /*6 bit(s)*/, raw0);
        WriteBitsToDword(R1, 14 /*6 bit(s)*/, raw0);
        WriteBitsToDword(R2, 20 /*6 bit(s)*/, raw0);
        WriteBitsToDword(R3, 26 /*6 bit(s)*/, raw0);
    }
    
    if (mask & 0x2u)
    {
        WriteBitsToDword(G1, 0  /*6 bit(s)*/, raw1);
        WriteBitsToDword(G0, 6  /*6 bit(s)*/, raw1);
        WriteBitsToDword(G3, 12 /*6 bit(s)*/, raw1);
        WriteBitsToDword(G2, 18 /*6 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword(G0, 0  /*6 bit(s)*/, raw1);
        WriteBitsToDword(G1, 6  /*6 bit(s)*/, raw1);
        WriteBitsToDword(G2, 12 /*6 bit(s)*/, raw1);
        WriteBitsToDword(G3, 18 /*6 bit(s)*/, raw1);
    }
    
    if (mask & 0x4u)
    {
        WriteBitsToDword(B1,        24 /*6 bit(s)*/, raw1);
        WriteBitsToDword(B0 & 0x3u, 30 /*2 bit(s)*/, raw1);
        WriteBitsToDword(B0 >> 2,   0  /*4 bit(s)*/, raw2);
        WriteBitsToDword(B3,        4  /*6 bit(s)*/, raw2);
        WriteBitsToDword(B2,        10 /*6 bit(s)*/, raw2);
    }
    else
    {
        WriteBitsToDword(B0,        24 /*6 bit(s)*/, raw1);
        WriteBitsToDword(B1 & 0x3u, 30 /*2 bit(s)*/, raw1);
        WriteBitsToDword(B1 >> 2,   0  /*4 bit(s)*/, raw2);
        WriteBitsToDword(B2,        4  /*6 bit(s)*/, raw2);
        WriteBitsToDword(B3,        10 /*6 bit(s)*/, raw2);
    }
   
    // Copy the last bits (after color) of rawOut[2] to raw2
    WriteBitsToDword(rawOut[2] >> 16, 16 /*8 bit(s)*/, raw2);
    WriteBitsToDword(rawOut[2] >> 24, 24 /*8 bit(s)*/, raw2);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
    rawOut[2] = raw2;
}

void reorderEndpointFields2(uint scrap, uint endpointOrderBytes, inout uint4 rawOut)
{
    // Mode 2
    // 3 bit mode                                                               =>  3 bits
    // 6 bit partition                                                          =>  6 bits
    // 5 bits R0 - 5 bits R1 - 5 bits R2 - 5 bits R3 - 5 bits R4 - 5 bits R5    => 30 bits
    // 5 bits B0 - 5 bits G1 - 5 bits R2 - 5 bits G3 - 5 bits G4 - 5 bits G5    => 30 bits
    // 5 bits G0 - 5 bits B1 - 5 bits R2 - 5 bits B3 - 5 bits B4 - 5 bits B5    => 30 bits
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    uint raw2 = 0u;
    uint raw3 = 0u;
    
    // Write the first bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0xFFu,         0 /*8 bit(s)*/, raw0);
    WriteBitsToDword((rawOut[0] >> 8) & 0x1u,   8 /*1 bit(s)*/, raw0);
    
    // R0-1-2-3-4-5
    uint R0 = (rawOut[0] >> 9) & 0x1Fu;
    uint R1 = (rawOut[0] >> 14) & 0x1Fu;
    uint R2 = (rawOut[0] >> 19) & 0x1Fu;
    uint R3 = (rawOut[0] >> 24) & 0x1Fu;
    uint R4 = ((rawOut[1] & 0x3f) << 3) | (rawOut[0] >> 29);
    uint R5 = (rawOut[1] >> 2) & 0x1Fu;
    
    // G0-1-2-3-4-5
    uint G0 = (rawOut[1] >> 7) & 0x1Fu;
    uint G1 = (rawOut[1] >> 12) & 0x1Fu;
    uint G2 = (rawOut[1] >> 17) & 0x1Fu;
    uint G3 = (rawOut[1] >> 22) & 0x1Fu;
    uint G4 = (rawOut[1] >> 27) & 0x1Fu;
    uint G5 = rawOut[2] & 0x1Fu;
    
    // B0-1-2-3-4-5
    uint B0 = (rawOut[2] >> 5) & 0x1Fu;
    uint B1 = (rawOut[2] >> 10) & 0x1Fu;
    uint B2 = (rawOut[2] >> 15) & 0x1Fu;
    uint B3 = (rawOut[2] >> 20) & 0x1Fu;
    uint B4 = (rawOut[2] >> 25) & 0x1Fu;
    uint B5 = ((rawOut[3] & 0x7u) << 2) | (rawOut[2] >> 30);
    
    uint quad0 = (scrap & 0x1u) ? (endpointOrderBytes & 0x000Fu) : 0u;
    uint quad1 = (scrap & 0x2u) ? (endpointOrderBytes & 0x00F0u) >> 4 : 0u;
    uint quad2 = (scrap & 0x4u) ? (endpointOrderBytes & 0x0F00u) >> 8 : 0u;
    uint quad3 = (scrap & 0x8u) ? (endpointOrderBytes & 0xF000u) >> 12 : 0u;
    uint mask = ((quad0 ^ quad1) ^ quad2) ^ quad3;
    
    if (mask & 0x1u)
    {
        WriteBitsToDword(R1,        9  /*5 bit(s)*/, raw0);
        WriteBitsToDword(R0,        14 /*5 bit(s)*/, raw0);
        WriteBitsToDword(R3,        19 /*5 bit(s)*/, raw0);
        WriteBitsToDword(R2,        24 /*5 bit(s)*/, raw0);
        WriteBitsToDword(R5 & 0x7u, 29 /*3 bit(s)*/, raw0);
        WriteBitsToDword(R5 >> 3,   0  /*2 bit(s)*/, raw1);
        WriteBitsToDword(R4,        2  /*5 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword(R0,        9  /*5 bit(s)*/, raw0);
        WriteBitsToDword(R1,        14 /*5 bit(s)*/, raw0);
        WriteBitsToDword(R2,        19 /*5 bit(s)*/, raw0);
        WriteBitsToDword(R3,        24 /*5 bit(s)*/, raw0);
        WriteBitsToDword(R4 & 0x7u, 29 /*3 bit(s)*/, raw0);
        WriteBitsToDword(R4 >> 3,   0  /*2 bit(s)*/, raw1);
        WriteBitsToDword(R5,        2  /*5 bit(s)*/, raw1);
    }
    
    if (mask & 0x2u)
    {
        WriteBitsToDword(G1, 7  /*5 bit(s)*/, raw1);
        WriteBitsToDword(G0, 12 /*5 bit(s)*/, raw1);
        WriteBitsToDword(G3, 17 /*5 bit(s)*/, raw1);
        WriteBitsToDword(G2, 22 /*5 bit(s)*/, raw1);
        WriteBitsToDword(G5, 27 /*5 bit(s)*/, raw1);
        WriteBitsToDword(G4, 0  /*5 bit(s)*/, raw2);
    }
    else
    {
        WriteBitsToDword(G0, 7  /*5 bit(s)*/, raw1);
        WriteBitsToDword(G1, 12 /*5 bit(s)*/, raw1);
        WriteBitsToDword(G2, 17 /*5 bit(s)*/, raw1);
        WriteBitsToDword(G3, 22 /*5 bit(s)*/, raw1);
        WriteBitsToDword(G4, 27 /*5 bit(s)*/, raw1);
        WriteBitsToDword(G5, 0  /*5 bit(s)*/, raw2);
    }
    
    if (mask & 0x4u)
    {
        WriteBitsToDword(B1,        5  /*5 bit(s)*/, raw2);
        WriteBitsToDword(B0,        10 /*5 bit(s)*/, raw2);
        WriteBitsToDword(B3,        15 /*5 bit(s)*/, raw2);
        WriteBitsToDword(B2,        20 /*5 bit(s)*/, raw2);
        WriteBitsToDword(B5,        25 /*5 bit(s)*/, raw2);
        WriteBitsToDword(B4 & 0x3u, 30 /*2 bit(s)*/, raw2);
        WriteBitsToDword(B4 >> 2,   0  /*3 bit(s)*/, raw3);
    }
    else
    {
        WriteBitsToDword(B0,        5  /*5 bit(s)*/, raw2);
        WriteBitsToDword(B1,        10 /*5 bit(s)*/, raw2);
        WriteBitsToDword(B2,        15 /*5 bit(s)*/, raw2);
        WriteBitsToDword(B3,        20 /*5 bit(s)*/, raw2);
        WriteBitsToDword(B4,        25 /*5 bit(s)*/, raw2);
        WriteBitsToDword(B5 & 0x3u, 30 /*2 bit(s)*/, raw2);
        WriteBitsToDword(B5 >> 2,   0  /*3 bit(s)*/, raw3);
    }
    
    // Copy the last bits (after color) of rawOut[3] to raw3
    WriteBitsToDword((rawOut[3] >> 3) & 0x1Fu,  3  /*5 bit(s)*/, raw3);
    WriteBitsToDword(rawOut[3] >> 8,            8  /*8 bit(s)*/, raw3);
    WriteBitsToDword(rawOut[3] >> 16,           16 /*8 bit(s)*/, raw3);
    WriteBitsToDword(rawOut[3] >> 24,           24 /*8 bit(s)*/, raw3);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
    rawOut[2] = raw2;
    rawOut[3] = raw3;
}

void reorderEndpointFields3(uint scrap, uint endpointOrderBytes, inout uint4 rawOut)
{
    // Mode 3
    // 4 bit mode                                       =>  4 bits
    // 6 bit partition                                  =>  6 bits
    // 7 bits R0 - 7 bits R1 - 7 bits R2 - 7 bits R3    => 28 bits
    // 7 bits B0 - 7 bits G1 - 7 bits R2 - 7 bits G3    => 28 bits
    // 7 bits G0 - 7 bits B1 - 7 bits R2 - 7 bits B3    => 28 bits
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    uint raw2 = 0u;
    
    // Write the first bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0xFFu,         0 /*8 bit(s)*/, raw0);
    WriteBitsToDword((rawOut[0] >> 8) & 0x3u,   8 /*2 bit(s)*/, raw0);
    
    // De-Rotate
    // R0-1-2-3-4-5
    uint R0 = (rawOut[0] >> 10) & 0x7Fu;
    uint R1 = (rawOut[0] >> 17) & 0x7Fu;
    uint R2 = (rawOut[0] >> 24) & 0x7Fu;
    uint R3 = ((rawOut[1] & 0x3Fu) << 1) | (rawOut[0] >> 31);
    
    // G0-1-2-3-4-5
    uint G0 = (rawOut[1] >> 6) & 0x7Fu;
    uint G1 = (rawOut[1] >> 13) & 0x7Fu;
    uint G2 = (rawOut[1] >> 20) & 0x7Fu;
    uint G3 = ((rawOut[2] & 0x3u) << 5) | (rawOut[1] >> 27);
    
    // B0-1-2-3-4-5
    uint B0 = (rawOut[2] >> 2) & 0x7Fu;
    uint B1 = (rawOut[2] >> 9) & 0x7Fu;
    uint B2 = (rawOut[2] >> 16) & 0x7Fu;
    uint B3 = (rawOut[2] >> 23) & 0x7Fu;
    
    uint quad0 = (scrap & 0x1u) ? (endpointOrderBytes & 0x000Fu) : 0u;
    uint quad1 = (scrap & 0x2u) ? (endpointOrderBytes & 0x00F0u) >> 4 : 0u;
    uint quad2 = (scrap & 0x4u) ? (endpointOrderBytes & 0x0F00u) >> 8 : 0u;
    uint quad3 = (scrap & 0x8u) ? (endpointOrderBytes & 0xF000u) >> 12 : 0u;
    uint mask = ((quad0 ^ quad1) ^ quad2) ^ quad3;
    
    if (mask & 0x1u)
    {
        WriteBitsToDword(R1,        10 /*7 bit(s)*/, raw0);
        WriteBitsToDword(R0,        17 /*7 bit(s)*/, raw0);
        WriteBitsToDword(R3,        24 /*7 bit(s)*/, raw0);
        WriteBitsToDword(R2 & 0x1u, 31 /*1 bit(s)*/, raw0);
        WriteBitsToDword(R2 >> 1,   0  /*6 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword(R0,        10 /*7 bit(s)*/, raw0);
        WriteBitsToDword(R1,        17 /*7 bit(s)*/, raw0);
        WriteBitsToDword(R2,        24 /*7 bit(s)*/, raw0);
        WriteBitsToDword(R3 & 0x1u, 31 /*1 bit(s)*/, raw0);
        WriteBitsToDword(R3 >> 1,   0  /*6 bit(s)*/, raw1);
    }
    
    if (mask & 0x2u)
    {
        WriteBitsToDword(G1,            6  /*7 bit(s)*/, raw1);
        WriteBitsToDword(G0,            13 /*7 bit(s)*/, raw1);
        WriteBitsToDword(G3,            20 /*7 bit(s)*/, raw1);
        WriteBitsToDword(G2 & 0x1Fu,    27 /*5 bit(s)*/, raw1);
        WriteBitsToDword(G2 >> 5,       0  /*2 bit(s)*/, raw2);
    }
    else
    {
        WriteBitsToDword(G0,            6  /*7 bit(s)*/, raw1);
        WriteBitsToDword(G1,            13 /*7 bit(s)*/, raw1);
        WriteBitsToDword(G2,            20 /*7 bit(s)*/, raw1);
        WriteBitsToDword(G3 & 0x1Fu,    27 /*5 bit(s)*/, raw1);
        WriteBitsToDword(G3 >> 5,       0  /*2 bit(s)*/, raw2);
    }
    
    if (mask & 0x4u)
    {
        WriteBitsToDword(B1, 2  /*7 bit(s)*/, raw2);
        WriteBitsToDword(B0, 9  /*7 bit(s)*/, raw2);
        WriteBitsToDword(B3, 16 /*7 bit(s)*/, raw2);
        WriteBitsToDword(B2, 23 /*7 bit(s)*/, raw2);
    }
    else
    {
        WriteBitsToDword(B0, 2  /*7 bit(s)*/, raw2);
        WriteBitsToDword(B1, 9  /*7 bit(s)*/, raw2);
        WriteBitsToDword(B2, 16 /*7 bit(s)*/, raw2);
        WriteBitsToDword(B3, 23 /*7 bit(s)*/, raw2);
    }
    
    // Copy the last bits (after color) of rawOut[2] to raw2
    WriteBitsToDword(rawOut[2] >> 30, 30 /*2 bit(s)*/, raw2);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
    rawOut[2] = raw2;
}

void reorderEndpointFields4(uint scrap, uint endpointOrderBytes, inout uint4 rawOut)
{
    // Mode 4
    // 5 bits mode                          =>  5 bits
    // 2 bits rotation                      =>  2 bits
    // 1 bit idxMode                        =>  1 bit
    // 5 bits R0 - 5 bits R1                => 10 bits
    // 5 bits B0 - 5 bits G1                => 10 bits
    // 5 bits G0 - 5 bits B1                => 10 bits
    // 6 bits A0 - 6 bits A1                => 12 bits
    // 31 bits index (16 2 bit indices)     => 31 bits
    // 47 bits index (16 3 bit indices)     => 47 bits
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    
    // Write the first 8 bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0xFFu, 0 /*8 bit(s)*/, raw0);
    
    // De-Rotate
    // R0-1
    uint R0 = (rawOut[0] >> 8) & 0x1Fu;
    uint R1 = (rawOut[0] >> 13) & 0x1Fu;
    
    // G0-1
    uint G0 = (rawOut[0] >> 18) & 0x1Fu;
    uint G1 = (rawOut[0] >> 23) & 0x1Fu;
    
    // B0-1
    uint B0 = ((rawOut[1] & 0x1u) << 4) | (rawOut[0] >> 28);
    uint B1 = (rawOut[1] >> 1) & 0x1Fu;
    
    // A0-1
    uint A0 = (rawOut[1] >> 6) & 0x1Fu;
    uint A1 = (rawOut[1] >> 11) & 0x1Fu;
    
    uint quad0 = (scrap & 0x1u) ? (endpointOrderBytes & 0x000Fu) : 0u;
    uint quad1 = (scrap & 0x2u) ? (endpointOrderBytes & 0x00F0u) >> 4 : 0u;
    uint quad2 = (scrap & 0x4u) ? (endpointOrderBytes & 0x0F00u) >> 8 : 0u;
    uint quad3 = (scrap & 0x8u) ? (endpointOrderBytes & 0xF000u) >> 12 : 0u;
    uint mask = ((quad0 ^ quad1) ^ quad2) ^ quad3;
    
    if (mask & 0x1u)
    {
        WriteBitsToDword(R1, 8  /*5 bit(s)*/, raw0);
        WriteBitsToDword(R0, 13 /*5 bit(s)*/, raw0);
    }
    else
    {
        WriteBitsToDword(R0, 8  /*5 bit(s)*/, raw0);
        WriteBitsToDword(R1, 13 /*5 bit(s)*/, raw0);
    }
    
    if (mask & 0x2u)
    {
        WriteBitsToDword(G1, 18 /*5 bit(s)*/, raw0);
        WriteBitsToDword(G0, 23 /*5 bit(s)*/, raw0);
    }
    else
    {
        WriteBitsToDword(G0, 18 /*5 bit(s)*/, raw0);
        WriteBitsToDword(G1, 23 /*5 bit(s)*/, raw0);
    }
    
    if (mask & 0x4u)
    {
        WriteBitsToDword(B1 & 0xFu, 28 /*4 bit(s)*/, raw0);
        WriteBitsToDword(B1 >> 4,   0  /*1 bit(s)*/, raw1);
        WriteBitsToDword(B0,        1  /*5 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword(B0 & 0xFu, 28 /*4 bit(s)*/, raw0);
        WriteBitsToDword(B0 >> 4,   0  /*1 bit(s)*/, raw1);
        WriteBitsToDword(B1,        1  /*5 bit(s)*/, raw1);
    }
    
    if (mask & 0x8u)
    {
        WriteBitsToDword(A1, 6  /*5 bit(s)*/, raw1);
        WriteBitsToDword(A0, 11 /*5 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword(A0, 6  /*5 bit(s)*/, raw1);
        WriteBitsToDword(A1, 11 /*5 bit(s)*/, raw1);
    }
    
    // Copy the last bits (after color) of rawOut[2] to raw2
    WriteBitsToDword(rawOut[1] >> 16, 16 /*8 bit(s)*/, raw1);
    WriteBitsToDword(rawOut[1] >> 24, 24 /*8 bit(s)*/, raw1);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
}

void reorderEndpointFields5(uint scrap, uint endpointOrderBytes, inout uint4 rawOut)
{
    // Mode 5
    // 6 bit mode               =>  6 bits
    // 2 bits rotation          =>  2 bits
    // 7 bits R0 - 7 bits R1    => 14 bits
    // 7 bits B0 - 7 bits G1    => 14 bits
    // 7 bits G0 - 7 bits B1    => 14 bits
    // 8 bits A0 - 8 bits A1    => 16 bits
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    uint raw2 = 0u;
    
    // Write the first 8 bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0xFFu, 0 /*8 bit(s)*/, raw0);
    
    // De-Rotate
    // R0-R1
    uint R0 = (rawOut[0] >> 8) & 0x7Fu;
    uint R1 = (rawOut[0] >> 15) & 0x7Fu;
    
    // G0-G1
    uint G0 = (rawOut[0] >> 22) & 0x7Fu;
    uint G1 = ((rawOut[0] >> 29) & 0x7u) | ((rawOut[1] & 0xFu) << 3);
    
    // B0-B1
    uint B0 = (rawOut[1] >> 4) & 0x7Fu;
    uint B1 = (rawOut[1] >> 11) & 0x7Fu;
    
    // A0-A1
    uint A0 = (rawOut[1] >> 18) & 0x7Fu;
    uint A1 = (rawOut[1] >> 25) & 0x7Fu;
    
    uint quad0 = (scrap & 0x1u) ? (endpointOrderBytes & 0x000Fu) : 0u;
    uint quad1 = (scrap & 0x2u) ? (endpointOrderBytes & 0x00F0u) >> 4 : 0u;
    uint quad2 = (scrap & 0x4u) ? (endpointOrderBytes & 0x0F00u) >> 8 : 0u;
    uint quad3 = (scrap & 0x8u) ? (endpointOrderBytes & 0xF000u) >> 12 : 0u;
    uint mask = ((quad0 ^ quad1) ^ quad2) ^ quad3;
    
    if (mask & 0x1u)
    {
        WriteBitsToDword(R1, 8  /*7 bit(s)*/, raw0);
        WriteBitsToDword(R0, 15 /*7 bit(s)*/, raw0);
    }
    else
    {
        WriteBitsToDword(R0, 8  /*7 bit(s)*/, raw0);
        WriteBitsToDword(R1, 15 /*7 bit(s)*/, raw0);
    }
    
    if (mask & 0x2u)
    {
        WriteBitsToDword(G1,        22 /*7 bit(s)*/, raw0);
        WriteBitsToDword(G0 & 0x7u, 29 /*4 bit(s)*/, raw0);
        WriteBitsToDword(G0 >> 3,   0  /*3 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword(G0,        22 /*7 bit(s)*/, raw0);
        WriteBitsToDword(G1 & 0x7u, 29 /*4 bit(s)*/, raw0);
        WriteBitsToDword(G1 >> 3,   0  /*3 bit(s)*/, raw1);
    }
    
    if (mask & 0x4u)
    {
        WriteBitsToDword(B1, 4  /*7 bit(s)*/, raw1);
        WriteBitsToDword(B0, 11 /*7 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword(B0, 4  /*7 bit(s)*/, raw1);
        WriteBitsToDword(B1, 11 /*7 bit(s)*/, raw1);
    }
    
    if (mask & 0x8u)
    {
        WriteBitsToDword(A1, 18 /*7 bit(s)*/, raw1);
        WriteBitsToDword(A0, 25 /*7 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword(A0, 18 /*7 bit(s)*/, raw1);
        WriteBitsToDword(A1, 25 /*7 bit(s)*/, raw1);
    }
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
}

void reorderEndpointFields6(uint scrap, uint endpointOrderBytes, inout uint4 rawOut)
{ 
    // Mode 6
    // 7 bit mode               =>  7 bits
    // 7 bits R0 - 7 bits R1    => 14 bits
    // 7 bits B0 - 7 bits G1    => 14 bits
    // 7 bits G0 - 7 bits B1    => 14 bits
    // 7 bits A0 - 7 bits A1    => 14 bits
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    
    // Write the first 8 bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0x7Fu, 0 /*7 bit(s)*/, raw0);
    
    // De-Rotate
    // R0-1
    uint R0 = (rawOut[0] >> 7) & 0x7Fu;
    uint R1 = (rawOut[0] >> 14) & 0x7Fu;
    
    // G0-1
    uint G0 = (rawOut[0] >> 21) & 0x7Fu;
    uint G1 = ((rawOut[0] >> 28) & 0xFu) | ((rawOut[1] & 0x7u) << 4);
    
    // B0-1
    uint B0 = (rawOut[1] >> 3) & 0x7Fu;
    uint B1 = (rawOut[1] >> 10) & 0x7Fu;
    
    // A0-1
    uint A0 = (rawOut[1] >> 17) & 0x7Fu;
    uint A1 = (rawOut[1] >> 24) & 0x7Fu;
    
    uint quad0 = (scrap & 0x1u) ? (endpointOrderBytes & 0x000Fu) : 0u;
    uint quad1 = (scrap & 0x2u) ? (endpointOrderBytes & 0x00F0u) >> 4 : 0u;
    uint quad2 = (scrap & 0x4u) ? (endpointOrderBytes & 0x0F00u) >> 8 : 0u;
    uint quad3 = (scrap & 0x8u) ? (endpointOrderBytes & 0xF000u) >> 12 : 0u;
    uint mask = ((quad0 ^ quad1) ^ quad2) ^ quad3;
    
    if (mask & 0x1u)
    {
        WriteBitsToDword(R1, 7  /*7 bit(s)*/, raw0);
        WriteBitsToDword(R0, 14 /*7 bit(s)*/, raw0);
    }
    else
    {
        WriteBitsToDword(R0, 7  /*7 bit(s)*/, raw0);
        WriteBitsToDword(R1, 14 /*7 bit(s)*/, raw0);
    }
    
    if (mask & 0x2u)
    {
        WriteBitsToDword(G1,        21 /*7 bit(s)*/, raw0);
        WriteBitsToDword(G0 & 0xFu, 28 /*4 bit(s)*/, raw0);
        WriteBitsToDword(G0 >> 4,   0  /*3 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword(G0,        21 /*7 bit(s)*/, raw0);
        WriteBitsToDword(G1 & 0xFu, 28 /*4 bit(s)*/, raw0);
        WriteBitsToDword(G1 >> 4,   0  /*3 bit(s)*/, raw1);
    }
    
    if (mask & 0x4u)
    {
        WriteBitsToDword(B1, 3  /*7 bit(s)*/, raw1);
        WriteBitsToDword(B0, 10 /*7 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword(B0, 3  /*7 bit(s)*/, raw1);
        WriteBitsToDword(B1, 10 /*7 bit(s)*/, raw1);
    }
    
    if (mask & 0x8u)
    {
        WriteBitsToDword(A1, 17 /*7 bit(s)*/, raw1);
        WriteBitsToDword(A0, 24 /*7 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword(A0, 17 /*7 bit(s)*/, raw1);
        WriteBitsToDword(A1, 24 /*7 bit(s)*/, raw1);
    }
    
    // Copy the last bits (after color) of rawOut[1] to raw1
    WriteBitsToDword((rawOut[1] >> 31) & 0x1u, 31 /*1 bit(s)*/, raw1);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
}

void reorderEndpointFields7(uint scrap, uint endpointOrderBytes, inout uint4 rawOut)
{
    // Mode 7
    // 8 bit mode                                       =>  8 bits
    // 6 bit partition                                  =>  6 bits
    // 5 bits R0 - 5 bits R1 - 5 bits R2 - 5 bits R3    => 20 bits
    // 5 bits B0 - 5 bits G1 - 5 bits R2 - 5 bits G3    => 20 bits
    // 5 bits G0 - 5 bits B1 - 5 bits R2 - 5 bits B3    => 20 bits
    // 5 bits A0 - 5 bits A1 - 5 bits A2 - 5 bits A3    => 20 bits
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    uint raw2 = 0u;
    
    // Write the first 14 bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0xFFu,         0 /*8 bit(s)*/, raw0);
    WriteBitsToDword((rawOut[0] >> 8) & 0x3Fu,  8 /*6 bit(s)*/, raw0);
    
    // R0-1-2-3
    uint R0 = (rawOut[0] >> 14) & 0x1Fu;
    uint R1 = (rawOut[0] >> 19) & 0x1Fu;
    uint R2 = (rawOut[0] >> 24) & 0x1Fu;
    uint R3 = ((rawOut[1] & 0x3u) << 3) | (rawOut[0] >> 29);
    
    // G0-1-2-3
    uint G0 = (rawOut[1] >> 2) & 0x1Fu;
    uint G1 = (rawOut[1] >> 7) & 0x1Fu;
    uint G2 = (rawOut[1] >> 12) & 0x1Fu;
    uint G3 = (rawOut[1] >> 17) & 0x1Fu;
    
    // B0-1-2-3
    uint B0 = (rawOut[1] >> 22) & 0x1Fu;
    uint B1 = (rawOut[1] >> 27) & 0x1Fu;
    uint B2 = rawOut[2] & 0x1Fu;
    uint B3 = (rawOut[2] >> 5) & 0x1Fu;
    
    // A0-1-2-3
    uint A0 = (rawOut[2] >> 10) & 0x1Fu;
    uint A1 = (rawOut[2] >> 15) & 0x1Fu;
    uint A2 = (rawOut[2] >> 20) & 0x1Fu;
    uint A3 = (rawOut[2] >> 25) & 0x1Fu;
    
    uint quad0 = (scrap & 0x1u) ? (endpointOrderBytes & 0x000Fu) : 0u;
    uint quad1 = (scrap & 0x2u) ? (endpointOrderBytes & 0x00F0u) >> 4 : 0u;
    uint quad2 = (scrap & 0x4u) ? (endpointOrderBytes & 0x0F00u) >> 8 : 0u;
    uint quad3 = (scrap & 0x8u) ? (endpointOrderBytes & 0xF000u) >> 12 : 0u;
    uint mask = ((quad0 ^ quad1) ^ quad2) ^ quad3;
    
    if (mask & 0x1u)
    {
        WriteBitsToDword(R1,        14 /*5 bit(s)*/, raw0);
        WriteBitsToDword(R0,        19 /*5 bit(s)*/, raw0);
        WriteBitsToDword(R3,        24 /*5 bit(s)*/, raw0);
        WriteBitsToDword(R2 & 0x7u, 29 /*3 bit(s)*/, raw0);
        WriteBitsToDword(R2 >> 3,   0  /*2 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword(R0,        14 /*5 bit(s)*/, raw0);
        WriteBitsToDword(R1,        19 /*5 bit(s)*/, raw0);
        WriteBitsToDword(R2,        24 /*5 bit(s)*/, raw0);
        WriteBitsToDword(R3 & 0x7u, 29 /*3 bit(s)*/, raw0);
        WriteBitsToDword(R3 >> 3,   0  /*2 bit(s)*/, raw1);
    }
    
    if (mask & 0x2u)
    {
        WriteBitsToDword(G1, 2  /*5 bit(s)*/, raw1);
        WriteBitsToDword(G0, 7  /*5 bit(s)*/, raw1);
        WriteBitsToDword(G3, 12 /*5 bit(s)*/, raw1);
        WriteBitsToDword(G2, 17 /*5 bit(s)*/, raw1);
    }
    else
    {
        WriteBitsToDword(G0, 2  /*5 bit(s)*/, raw1);
        WriteBitsToDword(G1, 7  /*5 bit(s)*/, raw1);
        WriteBitsToDword(G2, 12 /*5 bit(s)*/, raw1);
        WriteBitsToDword(G3, 17 /*5 bit(s)*/, raw1);
    }
    
    if (mask & 0x4u)
    {
        WriteBitsToDword(B1, 22 /*5 bit(s)*/, raw1);
        WriteBitsToDword(B0, 27 /*5 bit(s)*/, raw1);
        WriteBitsToDword(B3, 0  /*5 bit(s)*/, raw2);
        WriteBitsToDword(B2, 5  /*5 bit(s)*/, raw2);
    }
    else
    {
        WriteBitsToDword(B0, 22 /*5 bit(s)*/, raw1);
        WriteBitsToDword(B1, 27 /*5 bit(s)*/, raw1);
        WriteBitsToDword(B2, 0  /*5 bit(s)*/, raw2);
        WriteBitsToDword(B3, 5  /*5 bit(s)*/, raw2);
    }
    
    if (mask & 0x8u)
    {
        WriteBitsToDword(A1, 10 /*5 bit(s)*/, raw2);
        WriteBitsToDword(A0, 15 /*5 bit(s)*/, raw2);
        WriteBitsToDword(A3, 20 /*5 bit(s)*/, raw2);
        WriteBitsToDword(A2, 25 /*5 bit(s)*/, raw2);
    }
    else
    {
        WriteBitsToDword(A0, 10 /*5 bit(s)*/, raw2);
        WriteBitsToDword(A1, 15 /*5 bit(s)*/, raw2);
        WriteBitsToDword(A2, 20 /*5 bit(s)*/, raw2);
        WriteBitsToDword(A3, 25 /*5 bit(s)*/, raw2);
    }
    
    // Copy the last bits (after color) of rawOut[2] to raw2
    WriteBitsToDword((rawOut[2] >> 30) & 0x3u, 30 /*2 bit(s)*/, raw2);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
    rawOut[2] = raw2;
}