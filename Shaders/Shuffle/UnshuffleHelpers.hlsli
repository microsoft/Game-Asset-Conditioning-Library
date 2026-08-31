//--------------------------------------------------------------------------------------
// UnshuffleHelpers.hlsli
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "SharedDefinitions.hlsli"

void unshuffleMode0(uint colorAddress, uint miscAddress, uint ScrapData, uint modePattern, in ByteAddressBuffer buffer, inout uint4 raw)
{
    // Mode 0
    // 1 bit mode                                                               =>  1 bit
    // 4 bit partition                                                          =>  4 bits
    // 4 bits R0 - 4 bits R1 - 4 bits R2 - 4 bits R3 - 4 bits R4 - 4 bits R5    => 24 bits
    // 4 bits R0 - 4 bits R1 - 4 bits R2 - 4 bits R3 - 4 bits R4 - 4 bits R5    => 24 bits
    // 4 bits R0 - 4 bits R1 - 4 bits R2 - 4 bits R3 - 4 bits R4 - 4 bits R5    => 24 bits
    // 1 bit  P0 - 1 bit  P1 - 1 bit  P2 - 1 bit  P3 - 1 bit  P4 - 1 bit  P5    =>  6 bits
    // 45 bits index                                                            => 45 bits
    
    // 9 / 9 bytes color
    // 6 / 6 bytes misc
    // 7 bits scrap 
    
    // Color Data
    uint colorByte0 = fetchNextByte(colorAddress, buffer);
    uint colorByte1 = fetchNextByte(colorAddress + 1, buffer);
    uint colorByte2 = fetchNextByte(colorAddress + 2, buffer);
    uint colorByte3 = fetchNextByte(colorAddress + 3, buffer);
    uint colorByte4 = fetchNextByte(colorAddress + 4, buffer);
    uint colorByte5 = fetchNextByte(colorAddress + 5, buffer);
    uint colorByte6 = fetchNextByte(colorAddress + 6, buffer);
    uint colorByte7 = fetchNextByte(colorAddress + 7, buffer);
    uint colorByte8 = fetchNextByte(colorAddress + 8, buffer);
    
    // Misc Data (6 bytes)
    uint miscByte0 = fetchNextByte(miscAddress, buffer);
    uint miscByte1 = fetchNextByte(miscAddress + 1, buffer);
    uint miscByte2 = fetchNextByte(miscAddress + 2, buffer);
    uint miscByte3 = fetchNextByte(miscAddress + 3, buffer);
    uint miscByte4 = fetchNextByte(miscAddress + 4, buffer);
    uint miscByte5 = fetchNextByte(miscAddress + 5, buffer);
    
    // Write the mode
    WriteBitsToDword(1u,                0 /*1 bit(s)*/, raw[0]);
                                                            
    // 4 Partition bits are misc first 4 bits.              
    WriteBitsToDword(miscByte0 & 0xFu,  1 /*4 bit(s)*/, raw[0]);
    
    // Color data depends on the pattern
    if (modePattern == EndpointPair4bit)
    {
        WriteBitsToDword(colorByte0 & 0xFu,         5  /*4 bit(s)*/, raw[0]); // R0
        WriteBitsToDword(colorByte0 >> 4u,          9  /*4 bit(s)*/, raw[0]); // R1
        WriteBitsToDword(colorByte3 & 0xFu,         13 /*4 bit(s)*/, raw[0]); // R2
        WriteBitsToDword(colorByte3 >> 4u,          17 /*4 bit(s)*/, raw[0]); // R3
        WriteBitsToDword(colorByte6 & 0xFu,         21 /*4 bit(s)*/, raw[0]); // R4
        WriteBitsToDword(colorByte6 >> 4u,          25 /*4 bit(s)*/, raw[0]); // R5
                                                
        WriteBitsToDword(colorByte1 & 0x7u,         29 /*3 bit(s)*/, raw[0]); // G0
        WriteBitsToDword((colorByte1 >> 3) & 0x1u,  0  /*1 bit(s)*/, raw[1]); // G0
        WriteBitsToDword(colorByte1 >> 4u,          1  /*4 bit(s)*/, raw[1]); // G1
        WriteBitsToDword(colorByte4 & 0xFu,         5  /*4 bit(s)*/, raw[1]); // G2
        WriteBitsToDword(colorByte4 >> 4u,          9  /*4 bit(s)*/, raw[1]); // G3
        WriteBitsToDword(colorByte7 & 0xFu,         13 /*4 bit(s)*/, raw[1]); // G4
        WriteBitsToDword(colorByte7 >> 4u,          17 /*4 bit(s)*/, raw[1]); // G5
                                                
        WriteBitsToDword(colorByte2 & 0xFu,         21 /*4 bit(s)*/, raw[1]); // B0
        WriteBitsToDword(colorByte2 >> 4u,          25 /*4 bit(s)*/, raw[1]); // B1
        WriteBitsToDword(colorByte5 & 0x7u,         29 /*3 bit(s)*/, raw[1]); // B2
        WriteBitsToDword((colorByte5 >> 3) & 0x1u,  0  /*1 bit(s)*/, raw[2]); // B2
        WriteBitsToDword(colorByte5 >> 4u,          1  /*4 bit(s)*/, raw[2]); // B3
        WriteBitsToDword(colorByte8 & 0xFu,         5  /*4 bit(s)*/, raw[2]); // B4
        WriteBitsToDword(colorByte8 >> 4u,          9  /*4 bit(s)*/, raw[2]); // B5
    }
    else if (modePattern == ColorPlane4bit)
    {
        WriteBitsToDword(colorByte0 & 0xFu,         5  /*4 bit(s)*/, raw[0]); // R0
        WriteBitsToDword(colorByte0 >> 4u,          9  /*4 bit(s)*/, raw[0]); // R1
        WriteBitsToDword(colorByte1 & 0xFu,         13 /*4 bit(s)*/, raw[0]); // R2
        WriteBitsToDword(colorByte1 >> 4u,          17 /*4 bit(s)*/, raw[0]); // R3
        WriteBitsToDword(colorByte2 & 0xFu,         21 /*4 bit(s)*/, raw[0]); // R4
        WriteBitsToDword(colorByte2 >> 4u,          25 /*4 bit(s)*/, raw[0]); // R5
                                                
        WriteBitsToDword(colorByte3 & 0x7u,         29 /*3 bit(s)*/, raw[0]); // G0 
        WriteBitsToDword((colorByte3 >> 3) & 0x1u,  0  /*1 bit(s)*/, raw[1]); // G0  
        WriteBitsToDword(colorByte3 >> 4u,          1  /*4 bit(s)*/, raw[1]); // G1
        WriteBitsToDword(colorByte4 & 0xFu,         5  /*4 bit(s)*/, raw[1]); // G2
        WriteBitsToDword(colorByte4 >> 4u,          9  /*4 bit(s)*/, raw[1]); // G3
        WriteBitsToDword(colorByte5 & 0xFu,         13 /*4 bit(s)*/, raw[1]); // G4
        WriteBitsToDword(colorByte5 >> 4u,          17 /*4 bit(s)*/, raw[1]); // G5
                                                
        WriteBitsToDword(colorByte6 & 0xFu,         21 /*4 bit(s)*/, raw[1]); // B0
        WriteBitsToDword(colorByte6 >> 4u,          25 /*4 bit(s)*/, raw[1]); // B1
        WriteBitsToDword(colorByte7 & 0x7u,         29 /*3 bit(s)*/, raw[1]); // B2 
        WriteBitsToDword((colorByte7 >> 3) & 0x1u,  0  /*1 bit(s)*/, raw[2]); // B2 
        WriteBitsToDword(colorByte7 >> 4u,          1  /*4 bit(s)*/, raw[2]); // B3
        WriteBitsToDword(colorByte8 & 0xFu,         5  /*4 bit(s)*/, raw[2]); // B4
        WriteBitsToDword(colorByte8 >> 4,           9  /*4 bit(s)*/, raw[2]); // B5
    }
    else if (modePattern == EndpointPairSignificantBitInderleaved)
    {
        WriteBitsToDword(colorByte0 >> 0 & 0x1, 5  /*1 bit(s)*/, raw[0]); // R0 
        WriteBitsToDword(colorByte0 >> 6 & 0x1, 6  /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte1 >> 4 & 0x1, 7  /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte2 >> 2 & 0x1, 8  /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte0 >> 1 & 0x1, 9  /*1 bit(s)*/, raw[0]); // R1 
        WriteBitsToDword(colorByte0 >> 7 & 0x1, 10 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte1 >> 5 & 0x1, 11 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte2 >> 3 & 0x1, 12 /*1 bit(s)*/, raw[0]);
                                
        WriteBitsToDword(colorByte3 >> 0 & 0x1, 13 /*1 bit(s)*/, raw[0]); // R2 
        WriteBitsToDword(colorByte3 >> 6 & 0x1, 14 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte4 >> 4 & 0x1, 15 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte5 >> 2 & 0x1, 16 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte3 >> 1 & 0x1, 17 /*1 bit(s)*/, raw[0]); // R3 
        WriteBitsToDword(colorByte3 >> 7 & 0x1, 18 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte4 >> 5 & 0x1, 19 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte5 >> 3 & 0x1, 20 /*1 bit(s)*/, raw[0]);
                              
        WriteBitsToDword(colorByte6 >> 0 & 0x1, 21 /*1 bit(s)*/, raw[0]); // R4 
        WriteBitsToDword(colorByte6 >> 6 & 0x1, 22 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte7 >> 4 & 0x1, 23 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte8 >> 2 & 0x1, 24 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte6 >> 1 & 0x1, 25 /*1 bit(s)*/, raw[0]); // R5 
        WriteBitsToDword(colorByte6 >> 7 & 0x1, 26 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte7 >> 5 & 0x1, 27 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte8 >> 3 & 0x1, 28 /*1 bit(s)*/, raw[0]);
                                
        WriteBitsToDword(colorByte0 >> 2 & 0x1, 29 /*1 bit(s)*/, raw[0]); // G0 
        WriteBitsToDword(colorByte1 >> 0 & 0x1, 30 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte1 >> 6 & 0x1, 31 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte2 >> 4 & 0x1, 0  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte0 >> 3 & 0x1, 1  /*1 bit(s)*/, raw[1]); // G1 
        WriteBitsToDword(colorByte1 >> 1 & 0x1, 2  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte1 >> 7 & 0x1, 3  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 5 & 0x1, 4  /*1 bit(s)*/, raw[1]);
    
        WriteBitsToDword(colorByte3 >> 2 & 0x1, 5  /*1 bit(s)*/, raw[1]); // G2 
        WriteBitsToDword(colorByte4 >> 0 & 0x1, 6  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte4 >> 6 & 0x1, 7  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte5 >> 4 & 0x1, 8  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte3 >> 3 & 0x1, 9  /*1 bit(s)*/, raw[1]); // G3 
        WriteBitsToDword(colorByte4 >> 1 & 0x1, 10 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte4 >> 7 & 0x1, 11 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte5 >> 5 & 0x1, 12 /*1 bit(s)*/, raw[1]);
                              
        WriteBitsToDword(colorByte6 >> 2 & 0x1, 13 /*1 bit(s)*/, raw[1]); // G4 
        WriteBitsToDword(colorByte7 >> 0 & 0x1, 14 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte7 >> 6 & 0x1, 15 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte8 >> 4 & 0x1, 16 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte6 >> 3 & 0x1, 17 /*1 bit(s)*/, raw[1]); // G5 
        WriteBitsToDword(colorByte7 >> 1 & 0x1, 18 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte7 >> 7 & 0x1, 19 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte8 >> 5 & 0x1, 20 /*1 bit(s)*/, raw[1]);
    
        WriteBitsToDword(colorByte0 >> 4 & 0x1, 21 /*1 bit(s)*/, raw[1]); // B0 
        WriteBitsToDword(colorByte1 >> 2 & 0x1, 22 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 0 & 0x1, 23 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 6 & 0x1, 24 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte0 >> 5 & 0x1, 25 /*1 bit(s)*/, raw[1]); // B1 
        WriteBitsToDword(colorByte1 >> 3 & 0x1, 26 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 1 & 0x1, 27 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 7 & 0x1, 28 /*1 bit(s)*/, raw[1]);
    
        WriteBitsToDword(colorByte3 >> 4 & 0x1, 29 /*1 bit(s)*/, raw[1]); // B2 
        WriteBitsToDword(colorByte4 >> 2 & 0x1, 30 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte5 >> 0 & 0x1, 31 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte5 >> 6 & 0x1, 0  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte3 >> 5 & 0x1, 1  /*1 bit(s)*/, raw[2]); // B3 
        WriteBitsToDword(colorByte4 >> 3 & 0x1, 2  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte5 >> 1 & 0x1, 3  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte5 >> 7 & 0x1, 4  /*1 bit(s)*/, raw[2]);
                              
        WriteBitsToDword(colorByte6 >> 4 & 0x1, 5  /*1 bit(s)*/, raw[2]); // B4
        WriteBitsToDword(colorByte7 >> 2 & 0x1, 6  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte8 >> 0 & 0x1, 7  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte8 >> 6 & 0x1, 8  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte6 >> 5 & 0x1, 9  /*1 bit(s)*/, raw[2]); // B5
        WriteBitsToDword(colorByte7 >> 3 & 0x1, 10 /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte8 >> 1 & 0x1, 11 /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte8 >> 7 & 0x1, 12 /*1 bit(s)*/, raw[2]);
    }
    
    // P bits are taken from misc (P0-P5)
    uint PBits = (miscByte0 >> 4) | ((miscByte1 & 0x3u) << 4);
    WriteBitsToDword(PBits,                 13 /*6 bit(s)*/, raw[2]);

    // Index data from misc
    WriteBitsToDword(miscByte1 >> 2,        19 /*6 bit(s)*/, raw[2]);
    WriteBitsToDword(miscByte2 & 0x7Fu,     25 /*7 bit(s)*/, raw[2]);
    WriteBitsToDword(miscByte2 >> 7,        0  /*1 bit(s)*/, raw[3]);
    WriteBitsToDword(miscByte3,             1  /*8 bit(s)*/, raw[3]);
    WriteBitsToDword(miscByte4,             9  /*8 bit(s)*/, raw[3]);
    WriteBitsToDword(miscByte5,             17 /*8 bit(s)*/, raw[3]);

    // Last index from scrap's 7
    WriteBitsToDword(ScrapData,             25 /*7 bit(s)*/, raw[3]);
}


void unshuffleMode1(uint colorAddress, uint miscAddress, uint ScrapData, uint modePattern, in ByteAddressBuffer buffer, inout uint4 raw)
{
    // Mode 1
    // 2 bit mode                                       =>  2 bits
    // 6 bit partition                                  =>  6 bits
    // 6 bits R0 - 6 bits R1 - 6 bits R2 - 6 bits R3    => 24 bits
    // 6 bits B0 - 6 bits G1 - 6 bits R2 - 6 bits G3    => 24 bits
    // 6 bits G0 - 6 bits B1 - 6 bits R2 - 6 bits B3    => 24 bits
    // 1 bit  P0 - 1 bit  P1                            =>  2 bits
    // 46 bits index                                    => 46 bits
    
    // 9 / 10 bytes color
    // 6 / 5  bytes misc
    // 6 bits scrap

    // Color Data
    uint colorByte0 = fetchNextByte(colorAddress, buffer);
    uint colorByte1 = fetchNextByte(colorAddress + 1, buffer);
    uint colorByte2 = fetchNextByte(colorAddress + 2, buffer);
    uint colorByte3 = fetchNextByte(colorAddress + 3, buffer);
    uint colorByte4 = fetchNextByte(colorAddress + 4, buffer);
    uint colorByte5 = fetchNextByte(colorAddress + 5, buffer);
    uint colorByte6 = fetchNextByte(colorAddress + 6, buffer);
    uint colorByte7 = fetchNextByte(colorAddress + 7, buffer);
    uint colorByte8 = fetchNextByte(colorAddress + 8, buffer);
    uint colorByte9 = fetchNextByte(colorAddress + 9, buffer);
    
    // Misc Data (6 bytes)
    uint miscByte0 = fetchNextByte(miscAddress, buffer);
    uint miscByte1 = fetchNextByte(miscAddress + 1, buffer);
    uint miscByte2 = fetchNextByte(miscAddress + 2, buffer);
    uint miscByte3 = fetchNextByte(miscAddress + 3, buffer);
    uint miscByte4 = fetchNextByte(miscAddress + 4, buffer);
    uint miscByte5 = fetchNextByte(miscAddress + 5, buffer);
    
    // Write the mode
    WriteBitsToDword(2u, 0 /*2 bit(s)*/, raw[0]);
    
    if (modePattern == EndpointPairSignificantBitInderleaved)
    {
        // first 4 partition bits from colorbit 0 (first 4 bits)
        WriteBitsToDword(colorByte0 & 0xF, 2 /*4 bit(s)*/, raw[0]);
    
        // last 2 partition bits from colorbit 5 (first 2 bits)
        WriteBitsToDword(colorByte5 & 0x3, 6 /*2 bit(s)*/, raw[0]);
    
        // Color 
        WriteBitsToDword(colorByte0 >> 4 & 0x1, 8  /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword(colorByte1 >> 2 & 0x1, 9  /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte2 >> 0 & 0x1, 10 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte2 >> 6 & 0x1, 11 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte3 >> 4 & 0x1, 12 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte4 >> 2 & 0x1, 13 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte0 >> 5 & 0x1, 14 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword(colorByte1 >> 3 & 0x1, 15 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte2 >> 1 & 0x1, 16 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte2 >> 7 & 0x1, 17 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte3 >> 5 & 0x1, 18 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte4 >> 3 & 0x1, 19 /*1 bit(s)*/, raw[0]);
                                
        WriteBitsToDword(colorByte5 >> 4 & 0x1, 20 /*1 bit(s)*/, raw[0]); // R2
        WriteBitsToDword(colorByte6 >> 2 & 0x1, 21 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte7 >> 0 & 0x1, 22 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte7 >> 6 & 0x1, 23 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte8 >> 4 & 0x1, 24 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte9 >> 2 & 0x1, 25 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte5 >> 5 & 0x1, 26 /*1 bit(s)*/, raw[0]); // R3
        WriteBitsToDword(colorByte6 >> 3 & 0x1, 27 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte7 >> 1 & 0x1, 28 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte7 >> 7 & 0x1, 29 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte8 >> 5 & 0x1, 30 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte9 >> 3 & 0x1, 31 /*1 bit(s)*/, raw[0]);
    
        WriteBitsToDword(colorByte0 >> 6 & 0x1, 0  /*1 bit(s)*/, raw[1]); // G0
        WriteBitsToDword(colorByte1 >> 4 & 0x1, 1  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 2 & 0x1, 2  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte3 >> 0 & 0x1, 3  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte3 >> 6 & 0x1, 4  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte4 >> 4 & 0x1, 5  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte0 >> 7 & 0x1, 6  /*1 bit(s)*/, raw[1]); // G1
        WriteBitsToDword(colorByte1 >> 5 & 0x1, 7  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 3 & 0x1, 8  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte3 >> 1 & 0x1, 9  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte3 >> 7 & 0x1, 10 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte4 >> 5 & 0x1, 11 /*1 bit(s)*/, raw[1]);
    
        WriteBitsToDword(colorByte5 >> 6 & 0x1, 12 /*1 bit(s)*/, raw[1]); // G2
        WriteBitsToDword(colorByte6 >> 4 & 0x1, 13 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte7 >> 2 & 0x1, 14 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte8 >> 0 & 0x1, 15 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte8 >> 6 & 0x1, 16 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte9 >> 4 & 0x1, 17 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte5 >> 7 & 0x1, 18 /*1 bit(s)*/, raw[1]); // G3
        WriteBitsToDword(colorByte6 >> 5 & 0x1, 19 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte7 >> 3 & 0x1, 20 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte8 >> 1 & 0x1, 21 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte8 >> 7 & 0x1, 22 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte9 >> 5 & 0x1, 23 /*1 bit(s)*/, raw[1]);
    
        WriteBitsToDword(colorByte1 >> 0 & 0x1, 24 /*1 bit(s)*/, raw[1]); // B0
        WriteBitsToDword(colorByte1 >> 6 & 0x1, 25 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 4 & 0x1, 26 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte3 >> 2 & 0x1, 27 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte4 >> 0 & 0x1, 28 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte4 >> 6 & 0x1, 29 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte1 >> 1 & 0x1, 30 /*1 bit(s)*/, raw[1]); // B1
        WriteBitsToDword(colorByte1 >> 7 & 0x1, 31 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 5 & 0x1, 0  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte3 >> 3 & 0x1, 1  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte4 >> 1 & 0x1, 2  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte4 >> 7 & 0x1, 3  /*1 bit(s)*/, raw[2]);
    
        WriteBitsToDword(colorByte6 >> 0 & 0x1, 4  /*1 bit(s)*/, raw[2]); // B2
        WriteBitsToDword(colorByte6 >> 6 & 0x1, 5  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte7 >> 4 & 0x1, 6  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte8 >> 2 & 0x1, 7  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte9 >> 0 & 0x1, 8  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte9 >> 6 & 0x1, 9  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte6 >> 1 & 0x1, 10 /*1 bit(s)*/, raw[2]); // B3
        WriteBitsToDword(colorByte6 >> 7 & 0x1, 11 /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte7 >> 5 & 0x1, 12 /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte8 >> 3 & 0x1, 13 /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte9 >> 1 & 0x1, 14 /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte9 >> 7 & 0x1, 15 /*1 bit(s)*/, raw[2]);
    
        // Misc last 42 bits are pbits plus indices
        WriteBitsToDword(colorByte5 >> 2 & 0x3, 16 /*2 bit(s)*/, raw[2]);
        WriteBitsToDword(miscByte0,             18 /*8 bit(s)*/, raw[2]);
        WriteBitsToDword(miscByte1 & 0x3Fu,     26 /*6 bit(s)*/, raw[2]);
        WriteBitsToDword(miscByte1 >> 6,        0  /*2 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte2,             2  /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte3,             10 /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte4,             18 /*8 bit(s)*/, raw[3]);
    }
    else if (modePattern == EndpointQuadSignificantBitInderleaved)
    {
        // Partition are the 6 first bits in misc
        WriteBitsToDword(miscByte0 & 0x3F, 2 /*6 bit(s)*/, raw[0]);
    
        // Color 
        WriteBitsToDword(colorByte0 >> 0 & 0x1, 8  /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword(colorByte0 >> 6 & 0x1, 9  /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte1 >> 4 & 0x1, 10 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte2 >> 2 & 0x1, 11 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte3 >> 0 & 0x1, 12 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte3 >> 6 & 0x1, 13 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte0 >> 1 & 0x1, 14 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword(colorByte0 >> 7 & 0x1, 15 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte1 >> 5 & 0x1, 16 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte2 >> 3 & 0x1, 17 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte3 >> 1 & 0x1, 18 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte3 >> 7 & 0x1, 19 /*1 bit(s)*/, raw[0]);
                                
        WriteBitsToDword(colorByte8 >> 6 & 0x1, 20 /*1 bit(s)*/, raw[0]); // R2
        WriteBitsToDword(colorByte8 >> 0 & 0x1, 21 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte7 >> 2 & 0x1, 22 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte6 >> 4 & 0x1, 23 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte5 >> 6 & 0x1, 24 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte5 >> 0 & 0x1, 25 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte8 >> 7 & 0x1, 26 /*1 bit(s)*/, raw[0]); // R3
        WriteBitsToDword(colorByte8 >> 1 & 0x1, 27 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte7 >> 3 & 0x1, 28 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte6 >> 5 & 0x1, 29 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte5 >> 7 & 0x1, 30 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword(colorByte5 >> 1 & 0x1, 31 /*1 bit(s)*/, raw[0]);
                                
        WriteBitsToDword(colorByte0 >> 2 & 0x1, 0  /*1 bit(s)*/, raw[1]); // G0
        WriteBitsToDword(colorByte1 >> 0 & 0x1, 1  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte1 >> 6 & 0x1, 2  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 4 & 0x1, 3  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte3 >> 2 & 0x1, 4  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte4 >> 0 & 0x1, 5  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte0 >> 3 & 0x1, 6  /*1 bit(s)*/, raw[1]); // G1
        WriteBitsToDword(colorByte1 >> 1 & 0x1, 7  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte1 >> 7 & 0x1, 8  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 5 & 0x1, 9  /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte3 >> 3 & 0x1, 10 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte4 >> 1 & 0x1, 11 /*1 bit(s)*/, raw[1]);
                                
        WriteBitsToDword(colorByte8 >> 4 & 0x1, 12 /*1 bit(s)*/, raw[1]); // G2
        WriteBitsToDword(colorByte7 >> 6 & 0x1, 13 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte7 >> 0 & 0x1, 14 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte6 >> 2 & 0x1, 15 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte5 >> 4 & 0x1, 16 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte4 >> 6 & 0x1, 17 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte8 >> 5 & 0x1, 18 /*1 bit(s)*/, raw[1]); // G3
        WriteBitsToDword(colorByte7 >> 7 & 0x1, 19 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte7 >> 1 & 0x1, 20 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte6 >> 3 & 0x1, 21 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte5 >> 5 & 0x1, 22 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte4 >> 7 & 0x1, 23 /*1 bit(s)*/, raw[1]);
    
        WriteBitsToDword(colorByte0 >> 4 & 0x1, 24 /*1 bit(s)*/, raw[1]); // B0
        WriteBitsToDword(colorByte1 >> 2 & 0x1, 25 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 0 & 0x1, 26 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 6 & 0x1, 27 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte3 >> 4 & 0x1, 28 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte4 >> 2 & 0x1, 29 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte0 >> 5 & 0x1, 30 /*1 bit(s)*/, raw[1]); // B1
        WriteBitsToDword(colorByte1 >> 3 & 0x1, 31 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword(colorByte2 >> 1 & 0x1, 0  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte2 >> 7 & 0x1, 1  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte3 >> 5 & 0x1, 2  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte4 >> 3 & 0x1, 3  /*1 bit(s)*/, raw[2]);
    
        WriteBitsToDword(colorByte8 >> 2 & 0x1, 4  /*1 bit(s)*/, raw[2]); // B2
        WriteBitsToDword(colorByte7 >> 4 & 0x1, 5  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte6 >> 6 & 0x1, 6  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte6 >> 0 & 0x1, 7  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte5 >> 2 & 0x1, 8  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte4 >> 4 & 0x1, 9  /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte8 >> 3 & 0x1, 10 /*1 bit(s)*/, raw[2]); // B3
        WriteBitsToDword(colorByte7 >> 5 & 0x1, 11 /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte6 >> 7 & 0x1, 12 /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte6 >> 1 & 0x1, 13 /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte5 >> 3 & 0x1, 14 /*1 bit(s)*/, raw[2]);
        WriteBitsToDword(colorByte4 >> 5 & 0x1, 15 /*1 bit(s)*/, raw[2]);
    
        // Misc last 42 bits are pbits plus indices
        WriteBitsToDword(miscByte0 >> 6,        16 /*2 bit(s)*/, raw[2]);
        WriteBitsToDword(miscByte1,             18 /*8 bit(s)*/, raw[2]);
        WriteBitsToDword(miscByte2 & 0x3Fu,     26 /*6 bit(s)*/, raw[2]);
        WriteBitsToDword(miscByte2 >> 6,        0  /*2 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte3,             2  /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte4,             10 /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte5,             18 /*8 bit(s)*/, raw[3]);
    }
    
    // Scrap's 6 bits are the remaining indices
    WriteBitsToDword(ScrapData, 26 /*6 bit(s)*/, raw[3]);
}


void unshuffleMode2(uint colorAddress, uint miscAddress, uint ScrapData, uint modePattern, in ByteAddressBuffer buffer, inout uint4 raw)
{
    // Mode 2
    // 3 bit mode                                                               =>  3 bits
    // 6 bit partition                                                          =>  6 bits
    // 5 bits R0 - 5 bits R1 - 5 bits R2 - 5 bits R3 - 5 bits R4 - 5 bits R5    => 30 bits
    // 5 bits B0 - 5 bits G1 - 5 bits R2 - 5 bits G3 - 5 bits G4 - 5 bits G5    => 30 bits
    // 5 bits G0 - 5 bits B1 - 5 bits R2 - 5 bits B3 - 5 bits B4 - 5 bits B5    => 30 bits
    // 29 bits index                                                            => 29 bits
    
    // 9 / 12 bytes color ?
    // 6 / 3  bytes misc ?
    // 5 bits scrap ?

    // Color Data
    uint colorByte0 = fetchNextByte(colorAddress, buffer);
    uint colorByte1 = fetchNextByte(colorAddress + 1, buffer);
    uint colorByte2 = fetchNextByte(colorAddress + 2, buffer);
    uint colorByte3 = fetchNextByte(colorAddress + 3, buffer);
    uint colorByte4 = fetchNextByte(colorAddress + 4, buffer);
    uint colorByte5 = fetchNextByte(colorAddress + 5, buffer);
    uint colorByte6 = fetchNextByte(colorAddress + 6, buffer);
    uint colorByte7 = fetchNextByte(colorAddress + 7, buffer);
    uint colorByte8 = fetchNextByte(colorAddress + 8, buffer);
    uint colorByte9 = fetchNextByte(colorAddress + 9, buffer);
    uint colorByte10 = fetchNextByte(colorAddress + 10, buffer);
    uint colorByte11 = fetchNextByte(colorAddress + 11, buffer);
    
    // Misc Data (6 bytes)
    uint miscByte0 = fetchNextByte(miscAddress, buffer);
    uint miscByte1 = fetchNextByte(miscAddress + 1, buffer);
    uint miscByte2 = fetchNextByte(miscAddress + 2, buffer);
    uint miscByte3 = fetchNextByte(miscAddress + 3, buffer);
    uint miscByte4 = fetchNextByte(miscAddress + 4, buffer);
    uint miscByte5 = fetchNextByte(miscAddress + 5, buffer);
    
    // Write the mode
    WriteBitsToDword(4u, 0 /*3 bit(s)*/, raw[0]);
    
    if (modePattern == ColorPlane4bit || modePattern == EndpointPair4bit)
    {
        // Partition now come from misc bits [18:23] - miscByte2 last 6 bits.
        WriteBitsToDword(miscByte2 >> 2, 3 /*6 bit(s)*/, raw[0]);
        
        if (modePattern == EndpointPair4bit)
        {
            // Color data in stored in color + misc
            WriteBitsToDword((miscByte0) & 0x1,         9  /*1 bit(s)*/, raw[0]); // R0
            WriteBitsToDword(colorByte0 & 0xF,          10 /*4 bit(s)*/, raw[0]); // --
            WriteBitsToDword((miscByte0 >> 1) & 0x1,    14 /*1 bit(s)*/, raw[0]); // R1
            WriteBitsToDword(colorByte0 >> 4,           15 /*4 bit(s)*/, raw[0]); // --
            WriteBitsToDword((miscByte0 >> 2) & 0x1,    19 /*1 bit(s)*/, raw[0]); // R2
            WriteBitsToDword(colorByte3 & 0xF,          20 /*4 bit(s)*/, raw[0]); // --
            WriteBitsToDword((miscByte0 >> 3) & 0x1,    24 /*1 bit(s)*/, raw[0]); // R3
            WriteBitsToDword(colorByte3 >> 4,           25 /*4 bit(s)*/, raw[0]); // --
            WriteBitsToDword((miscByte0 >> 4) & 0x1,    29 /*1 bit(s)*/, raw[0]); // R4
            WriteBitsToDword(colorByte6 & 0x3,          30 /*2 bit(s)*/, raw[0]); // --
            WriteBitsToDword((colorByte6 >> 2) & 0x3,   0  /*2 bit(s)*/, raw[1]); // --
            WriteBitsToDword((miscByte0 >> 5) & 0x1,    2  /*1 bit(s)*/, raw[1]); // R5
            WriteBitsToDword(colorByte6 >> 4,           3  /*4 bit(s)*/, raw[1]); // --
        
            WriteBitsToDword((miscByte0 >> 6) & 0x1,    7  /*1 bit(s)*/, raw[1]); // G0
            WriteBitsToDword(colorByte1 & 0xF,          8  /*4 bit(s)*/, raw[1]); // --
            WriteBitsToDword((miscByte0 >> 7) & 0x1,    12 /*1 bit(s)*/, raw[1]); // G1
            WriteBitsToDword(colorByte1 >> 4,           13 /*4 bit(s)*/, raw[1]); // --
            WriteBitsToDword((miscByte1) & 0x1,         17 /*1 bit(s)*/, raw[1]); // G2
            WriteBitsToDword(colorByte4 & 0xF,          18 /*4 bit(s)*/, raw[1]); // --
            WriteBitsToDword((miscByte1 >> 1) & 0x1,    22 /*1 bit(s)*/, raw[1]); // G3
            WriteBitsToDword(colorByte4 >> 4,           23 /*4 bit(s)*/, raw[1]); // --
            WriteBitsToDword((miscByte1 >> 2) & 0x1,    27 /*1 bit(s)*/, raw[1]); // G4
            WriteBitsToDword(colorByte7 & 0xF,          28 /*4 bit(s)*/, raw[1]); // --
            WriteBitsToDword((miscByte1 >> 3) & 0x1,    0  /*1 bit(s)*/, raw[2]); // G5
            WriteBitsToDword(colorByte7 >> 4,           1  /*4 bit(s)*/, raw[2]); // --
        
            WriteBitsToDword((miscByte1 >> 4) & 0x1,    5  /*1 bit(s)*/, raw[2]); // B0
            WriteBitsToDword(colorByte2 & 0xF,          6  /*4 bit(s)*/, raw[2]); // --
            WriteBitsToDword((miscByte1 >> 5) & 0x1,    10 /*1 bit(s)*/, raw[2]); // B1
            WriteBitsToDword(colorByte2 >> 4,           11 /*4 bit(s)*/, raw[2]); // --
            WriteBitsToDword((miscByte1 >> 6) & 0x1,    15 /*1 bit(s)*/, raw[2]); // B2
            WriteBitsToDword(colorByte5 & 0xF,          16 /*4 bit(s)*/, raw[2]); // --
            WriteBitsToDword((miscByte1 >> 7) & 0x1,    20 /*1 bit(s)*/, raw[2]); // B3
            WriteBitsToDword(colorByte5 >> 4,           21 /*4 bit(s)*/, raw[2]); // --
            WriteBitsToDword((miscByte2) & 0x1,         25 /*1 bit(s)*/, raw[2]); // B4
            WriteBitsToDword(colorByte8 & 0xF,          26 /*4 bit(s)*/, raw[2]); // --
            WriteBitsToDword((miscByte2 >> 1) & 0x1,    30 /*1 bit(s)*/, raw[2]); // B5
            WriteBitsToDword((colorByte8 >> 4) & 0x1,   31 /*1 bit(s)*/, raw[2]); // --
            WriteBitsToDword((colorByte8 >> 5) & 0xF,   0  /*3 bit(s)*/, raw[3]); // --
        }
        else if (modePattern == ColorPlane4bit)
        {
            // Color data in stored in color + misc
            WriteBitsToDword((miscByte0) & 0x1,         9  /*1 bit(s)*/, raw[0]); // R0
            WriteBitsToDword(colorByte0 & 0xF,          10 /*4 bit(s)*/, raw[0]); // --
            WriteBitsToDword((miscByte0 >> 1) & 0x1,    14 /*1 bit(s)*/, raw[0]); // R1
            WriteBitsToDword(colorByte0 >> 4,           15 /*4 bit(s)*/, raw[0]); // --
            WriteBitsToDword((miscByte0 >> 2) & 0x1,    19 /*1 bit(s)*/, raw[0]); // R2
            WriteBitsToDword(colorByte1 & 0xF,          20 /*4 bit(s)*/, raw[0]); // --
            WriteBitsToDword((miscByte0 >> 3) & 0x1,    24 /*1 bit(s)*/, raw[0]); // R3
            WriteBitsToDword(colorByte1 >> 4,           25 /*4 bit(s)*/, raw[0]); // --
            WriteBitsToDword((miscByte0 >> 4) & 0x1,    29 /*1 bit(s)*/, raw[0]); // R4
            WriteBitsToDword(colorByte2 & 0x3,          30 /*2 bit(s)*/, raw[0]); // --
            WriteBitsToDword((colorByte2 >> 2) & 0x3,   0  /*2 bit(s)*/, raw[1]); // --
            WriteBitsToDword((miscByte0 >> 5) & 0x1,    2  /*1 bit(s)*/, raw[1]); // R5
            WriteBitsToDword(colorByte2 >> 4,           3  /*4 bit(s)*/, raw[1]); // --
        
            WriteBitsToDword((miscByte0 >> 6) & 0x1,    7  /*1 bit(s)*/, raw[1]); // G0
            WriteBitsToDword(colorByte3 & 0xF,          8  /*4 bit(s)*/, raw[1]); // --
            WriteBitsToDword((miscByte0 >> 7) & 0x1,    12 /*1 bit(s)*/, raw[1]); // G1
            WriteBitsToDword(colorByte3 >> 4,           13 /*4 bit(s)*/, raw[1]); // --
            WriteBitsToDword((miscByte1) & 0x1,         17 /*1 bit(s)*/, raw[1]); // G2
            WriteBitsToDword(colorByte4 & 0xF,          18 /*4 bit(s)*/, raw[1]); // --
            WriteBitsToDword((miscByte1 >> 1) & 0x1,    22 /*1 bit(s)*/, raw[1]); // G3
            WriteBitsToDword(colorByte4 >> 4,           23 /*4 bit(s)*/, raw[1]); // --
            WriteBitsToDword((miscByte1 >> 2) & 0x1,    27 /*1 bit(s)*/, raw[1]); // G4
            WriteBitsToDword(colorByte5 & 0xF,          28 /*4 bit(s)*/, raw[1]); // --
            WriteBitsToDword((miscByte1 >> 3) & 0x1,    0  /*1 bit(s)*/, raw[2]); // G5
            WriteBitsToDword(colorByte5 >> 4,           1  /*4 bit(s)*/, raw[2]); // --
        
            WriteBitsToDword((miscByte1 >> 4) & 0x1,    5  /*1 bit(s)*/, raw[2]); // B0
            WriteBitsToDword(colorByte6 & 0xF,          6  /*4 bit(s)*/, raw[2]); // --
            WriteBitsToDword((miscByte1 >> 5) & 0x1,    10 /*1 bit(s)*/, raw[2]); // B1
            WriteBitsToDword(colorByte6 >> 4,           11 /*4 bit(s)*/, raw[2]); // --
            WriteBitsToDword((miscByte1 >> 6) & 0x1,    15 /*1 bit(s)*/, raw[2]); // B2
            WriteBitsToDword(colorByte7 & 0xF,          16 /*4 bit(s)*/, raw[2]); // --
            WriteBitsToDword((miscByte1 >> 7) & 0x1,    20 /*1 bit(s)*/, raw[2]); // B3
            WriteBitsToDword(colorByte7 >> 4,           21 /*4 bit(s)*/, raw[2]); // --
            WriteBitsToDword((miscByte2) & 0x1,         25 /*1 bit(s)*/, raw[2]); // B4
            WriteBitsToDword(colorByte8 & 0xF,          26 /*4 bit(s)*/, raw[2]); // --
            WriteBitsToDword((miscByte2 >> 1) & 0x1,    30 /*1 bit(s)*/, raw[2]); // B5
            WriteBitsToDword((colorByte8 >> 4) & 0x1,   31 /*1 bit(s)*/, raw[2]); // --
            WriteBitsToDword((colorByte8 >> 5) & 0xF,   0  /*3 bit(s)*/, raw[3]); // --
        }
        
        // Misc 24 bits 
        WriteBitsToDword(miscByte3, 3  /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte4, 11 /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte5, 19 /*8 bit(s)*/, raw[3]);
    }
    else if (modePattern == EndpointPairSignificantBitInderleaved)
    {   
        // Partition bits 0-1
        WriteBitsToDword(colorByte0 & 0x3, 3 /*2 bit(s)*/, raw[0]);
        
        // Partition bits 2-3
        WriteBitsToDword(colorByte4 & 0x3, 5 /*2 bit(s)*/, raw[0]);
        
        // Partition bits 4-5
        WriteBitsToDword(colorByte8 & 0x3, 7 /*2 bit(s)*/, raw[0]);

        // Color data in stored in color + misc
        WriteBitsToDword((colorByte0 >> 2) & 0x1,   9  /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorByte1 >> 0) & 0x1,   10 /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorByte1 >> 6) & 0x1,   11 /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorByte2 >> 4) & 0x1,   12 /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorByte3 >> 2) & 0x1,   13 /*1 bit(s)*/, raw[0]); // R0
         
        WriteBitsToDword((colorByte0 >> 3) & 0x1,   14 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorByte1 >> 1) & 0x1,   15 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorByte1 >> 7) & 0x1,   16 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorByte2 >> 5) & 0x1,   17 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorByte3 >> 3) & 0x1,   18 /*1 bit(s)*/, raw[0]); // R1
        
        WriteBitsToDword((colorByte4 >> 2) & 0x1,   19 /*1 bit(s)*/, raw[0]); // R2
        WriteBitsToDword((colorByte5 >> 0) & 0x1,   20 /*1 bit(s)*/, raw[0]); // R2
        WriteBitsToDword((colorByte5 >> 6) & 0x1,   21 /*1 bit(s)*/, raw[0]); // R2
        WriteBitsToDword((colorByte6 >> 4) & 0x1,   22 /*1 bit(s)*/, raw[0]); // R2
        WriteBitsToDword((colorByte7 >> 2) & 0x1,   23 /*1 bit(s)*/, raw[0]); // R2
        
        WriteBitsToDword((colorByte4 >> 3) & 0x1,   24 /*1 bit(s)*/, raw[0]); // R3
        WriteBitsToDword((colorByte5 >> 1) & 0x1,   25 /*1 bit(s)*/, raw[0]); // R3
        WriteBitsToDword((colorByte5 >> 7) & 0x1,   26 /*1 bit(s)*/, raw[0]); // R3
        WriteBitsToDword((colorByte6 >> 5) & 0x1,   27 /*1 bit(s)*/, raw[0]); // R3
        WriteBitsToDword((colorByte7 >> 3) & 0x1,   28 /*1 bit(s)*/, raw[0]); // R3
        
        WriteBitsToDword((colorByte8 >> 2) & 0x1,   29 /*1 bit(s)*/, raw[0]); // R4
        WriteBitsToDword((colorByte9 >> 0) & 0x1,   30 /*1 bit(s)*/, raw[0]); // R4
        WriteBitsToDword((colorByte9 >> 6) & 0x1,   31 /*1 bit(s)*/, raw[0]); // R4
        WriteBitsToDword((colorByte10 >> 4) & 0x1,  0  /*1 bit(s)*/, raw[1]); // R4
        WriteBitsToDword((colorByte11 >> 2) & 0x1,  1  /*1 bit(s)*/, raw[1]); // R4
 
        WriteBitsToDword((colorByte8 >> 3) & 0x1,   2  /*1 bit(s)*/, raw[1]); // R5
        WriteBitsToDword((colorByte9 >> 1) & 0x1,   3  /*1 bit(s)*/, raw[1]); // R5
        WriteBitsToDword((colorByte9 >> 7) & 0x1,   4  /*1 bit(s)*/, raw[1]); // R5
        WriteBitsToDword((colorByte10 >> 5) & 0x1,  5  /*1 bit(s)*/, raw[1]); // R5
        WriteBitsToDword((colorByte11 >> 3) & 0x1,  6  /*1 bit(s)*/, raw[1]); // R5
        
        
        WriteBitsToDword((colorByte0 >> 4) & 0x1,   7  /*1 bit(s)*/, raw[1]); // G0
        WriteBitsToDword((colorByte1 >> 2) & 0x1,   8  /*1 bit(s)*/, raw[1]); // G0
        WriteBitsToDword((colorByte2 >> 0) & 0x1,   9  /*1 bit(s)*/, raw[1]); // G0
        WriteBitsToDword((colorByte2 >> 6) & 0x1,   10 /*1 bit(s)*/, raw[1]); // G0
        WriteBitsToDword((colorByte3 >> 4) & 0x1,   11 /*1 bit(s)*/, raw[1]); // G0
                                                                       
        WriteBitsToDword((colorByte0 >> 5) & 0x1,   12 /*1 bit(s)*/, raw[1]); // G1
        WriteBitsToDword((colorByte1 >> 3) & 0x1,   13 /*1 bit(s)*/, raw[1]); // G1
        WriteBitsToDword((colorByte2 >> 1) & 0x1,   14 /*1 bit(s)*/, raw[1]); // G1
        WriteBitsToDword((colorByte2 >> 7) & 0x1,   15 /*1 bit(s)*/, raw[1]); // G1
        WriteBitsToDword((colorByte3 >> 5) & 0x1,   16 /*1 bit(s)*/, raw[1]); // G1
                                                                       
        WriteBitsToDword((colorByte4 >> 4) & 0x1,   17 /*1 bit(s)*/, raw[1]); // G2
        WriteBitsToDword((colorByte5 >> 2) & 0x1,   18 /*1 bit(s)*/, raw[1]); // G2
        WriteBitsToDword((colorByte6 >> 0) & 0x1,   19 /*1 bit(s)*/, raw[1]); // G2
        WriteBitsToDword((colorByte6 >> 6) & 0x1,   20 /*1 bit(s)*/, raw[1]); // G2
        WriteBitsToDword((colorByte7 >> 4) & 0x1,   21 /*1 bit(s)*/, raw[1]); // G2
                                                                      
        WriteBitsToDword((colorByte4 >> 5) & 0x1,   22 /*1 bit(s)*/, raw[1]); // G3
        WriteBitsToDword((colorByte5 >> 3) & 0x1,   23 /*1 bit(s)*/, raw[1]); // G3
        WriteBitsToDword((colorByte6 >> 1) & 0x1,   24 /*1 bit(s)*/, raw[1]); // G3
        WriteBitsToDword((colorByte6 >> 7) & 0x1,   25 /*1 bit(s)*/, raw[1]); // G3
        WriteBitsToDword((colorByte7 >> 5) & 0x1,   26 /*1 bit(s)*/, raw[1]); // G3
                                                                    
        WriteBitsToDword((colorByte8 >> 4) & 0x1,   27 /*1 bit(s)*/, raw[1]); // G4
        WriteBitsToDword((colorByte9 >> 2) & 0x1,   28 /*1 bit(s)*/, raw[1]); // G4
        WriteBitsToDword((colorByte10 >> 0) & 0x1,  29 /*1 bit(s)*/, raw[1]); // G4
        WriteBitsToDword((colorByte10 >> 6) & 0x1,  30 /*1 bit(s)*/, raw[1]); // G4
        WriteBitsToDword((colorByte11 >> 4) & 0x1,  31 /*1 bit(s)*/, raw[1]); // G4
                                                                     
        WriteBitsToDword((colorByte8 >> 5) & 0x1,   0 /*1 bit(s)*/, raw[2]); // G5
        WriteBitsToDword((colorByte9 >> 3) & 0x1,   1 /*1 bit(s)*/, raw[2]); // G5
        WriteBitsToDword((colorByte10 >> 1) & 0x1,  2 /*1 bit(s)*/, raw[2]); // G5
        WriteBitsToDword((colorByte10 >> 7) & 0x1,  3 /*1 bit(s)*/, raw[2]); // G5
        WriteBitsToDword((colorByte11 >> 5) & 0x1,  4 /*1 bit(s)*/, raw[2]); // G5
        
        
        WriteBitsToDword((colorByte0 >> 6) & 0x1,   5  /*1 bit(s)*/, raw[2]); // B0 
        WriteBitsToDword((colorByte1 >> 4) & 0x1,   6  /*1 bit(s)*/, raw[2]); // B0
        WriteBitsToDword((colorByte2 >> 2) & 0x1,   7  /*1 bit(s)*/, raw[2]); // B0
        WriteBitsToDword((colorByte3 >> 0) & 0x1,   8  /*1 bit(s)*/, raw[2]); // B0
        WriteBitsToDword((colorByte3 >> 6) & 0x1,   9  /*1 bit(s)*/, raw[2]); // B0
                                                                      
        WriteBitsToDword((colorByte0 >> 7) & 0x1,   10 /*1 bit(s)*/, raw[2]); // B1 
        WriteBitsToDword((colorByte1 >> 5) & 0x1,   11 /*1 bit(s)*/, raw[2]); // B1
        WriteBitsToDword((colorByte2 >> 3) & 0x1,   12 /*1 bit(s)*/, raw[2]); // B1
        WriteBitsToDword((colorByte3 >> 1) & 0x1,   13 /*1 bit(s)*/, raw[2]); // B1
        WriteBitsToDword((colorByte3 >> 7) & 0x1,   14 /*1 bit(s)*/, raw[2]); // B1
                                                                       
        WriteBitsToDword((colorByte4 >> 6) & 0x1,   15 /*1 bit(s)*/, raw[2]); // B2 
        WriteBitsToDword((colorByte5 >> 4) & 0x1,   16 /*1 bit(s)*/, raw[2]); // B2
        WriteBitsToDword((colorByte6 >> 2) & 0x1,   17 /*1 bit(s)*/, raw[2]); // B2
        WriteBitsToDword((colorByte7 >> 0) & 0x1,   18 /*1 bit(s)*/, raw[2]); // B2
        WriteBitsToDword((colorByte7 >> 6) & 0x1,   19 /*1 bit(s)*/, raw[2]); // B2
                                                                    
        WriteBitsToDword((colorByte4 >> 7) & 0x1,   20 /*1 bit(s)*/, raw[2]); // B3 
        WriteBitsToDword((colorByte5 >> 5) & 0x1,   21 /*1 bit(s)*/, raw[2]); // B3
        WriteBitsToDword((colorByte6 >> 3) & 0x1,   22 /*1 bit(s)*/, raw[2]); // B3
        WriteBitsToDword((colorByte7 >> 1) & 0x1,   23 /*1 bit(s)*/, raw[2]); // B3
        WriteBitsToDword((colorByte7 >> 7) & 0x1,   24 /*1 bit(s)*/, raw[2]); // B3
                                                                  
        WriteBitsToDword((colorByte8 >> 6) & 0x1,   25 /*1 bit(s)*/, raw[2]); // B4
        WriteBitsToDword((colorByte9 >> 4) & 0x1,   26 /*1 bit(s)*/, raw[2]); // B4
        WriteBitsToDword((colorByte10 >> 2) & 0x1,  27 /*1 bit(s)*/, raw[2]); // B4
        WriteBitsToDword((colorByte11 >> 0) & 0x1,  28 /*1 bit(s)*/, raw[2]); // B4
        WriteBitsToDword((colorByte11 >> 6) & 0x1,  29 /*1 bit(s)*/, raw[2]); // B4
                                                                   
        WriteBitsToDword((colorByte8 >> 7) & 0x1,   30 /*1 bit(s)*/, raw[2]); // B5
        WriteBitsToDword((colorByte9 >> 5) & 0x1,   31 /*1 bit(s)*/, raw[2]); // B5
        WriteBitsToDword((colorByte10 >> 3) & 0x1,  0  /*1 bit(s)*/, raw[3]); // B5
        WriteBitsToDword((colorByte11 >> 1) & 0x1,  1  /*1 bit(s)*/, raw[3]); // B5
        WriteBitsToDword((colorByte11 >> 7) & 0x1,  2  /*1 bit(s)*/, raw[3]); // B5
        
        // Misc 24 bits
        WriteBitsToDword(miscByte0 & 0x7,   3  /*3 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte0 >> 3,    6  /*5 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte1,         11 /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte2,         19 /*8 bit(s)*/, raw[3]);
    }
    
    // remainder: 47 - 42 = 5 spare bits for the scrap stream
    WriteBitsToDword(ScrapData, 27 /*5 bit(s)*/, raw[3]);
}


void unshuffleMode3(uint colorAddress, uint miscAddress, uint ScrapData, uint modePattern, in ByteAddressBuffer buffer, inout uint4 raw)
{
    // Mode 3
    // 4 bit mode                                       =>  4 bits
    // 6 bit partition                                  =>  6 bits
    // 7 bits R0 - 7 bits R1 - 7 bits R2 - 7 bits R3    => 28 bits
    // 7 bits B0 - 7 bits G1 - 7 bits R2 - 7 bits G3    => 28 bits
    // 7 bits G0 - 7 bits B1 - 7 bits R2 - 7 bits B3    => 28 bits
    // 1 bit  P0 - 1 bit  P1 - 1 bit  P2 - 1 bit  P3    =>  4 bits
    // 30 bits index                                    => 30 bits
    
    // 11 / 12 bytes color ?
    // 4  / 3  bytes misc
    // 4 bits scrap

    // Color Data
    uint colorBytes[12];
    colorBytes[0] = fetchNextByte(colorAddress, buffer);
    colorBytes[1] = fetchNextByte(colorAddress + 1, buffer);
    colorBytes[2] = fetchNextByte(colorAddress + 2, buffer);
    colorBytes[3] = fetchNextByte(colorAddress + 3, buffer);
    colorBytes[4] = fetchNextByte(colorAddress + 4, buffer);
    colorBytes[5] = fetchNextByte(colorAddress + 5, buffer);
    colorBytes[6] = fetchNextByte(colorAddress + 6, buffer);
    colorBytes[7] = fetchNextByte(colorAddress + 7, buffer);
    colorBytes[8] = fetchNextByte(colorAddress + 8, buffer);
    colorBytes[9] = fetchNextByte(colorAddress + 9, buffer);
    colorBytes[10] = fetchNextByte(colorAddress + 10, buffer);
    colorBytes[11] = fetchNextByte(colorAddress + 11, buffer);
    
    // Misc Data
    uint miscByte0 = fetchNextByte(miscAddress, buffer);
    uint miscByte1 = fetchNextByte(miscAddress + 1, buffer);
    uint miscByte2 = fetchNextByte(miscAddress + 2, buffer);
    uint miscByte3 = fetchNextByte(miscAddress + 3, buffer);
    
    const uint kChannels = 6;
    uint dwordIndex = 0;
    uint dwordPosition = 10;
    
    // Write the mode
    WriteBitsToDword(8u, 0 /*4 bit(s)*/, raw[0]);
    
    if (modePattern == EndpointQuadSignificantBitInderleaved) // matches experimental ID 11 
    {
        //  This shuffle treats 0/1 endpoints as a group and 2/3 endpoints as a different group that vary independenty.
        //  the most significant bits are grouped in the middle, specifically at a 4-bit offset into a byte.

        //  42 bits of RGB 0\1:  padded with P0+P1   bits 0 - 43
        //                                                                                                                3                               4                                                     Most significant 7
        //  P0 P1   R0'0 R1'0 G0'0 G1'0 B0'0 B1'0   R0'1 R1'1 G0'1 G1'1 B0'1 B1'1   R0'2 R1'2 G0'2 G1'2 B0'2 B1'2   R0'3 R1'3 G0'3 G1'3 B0'3 B1'3   R0'4 R1'4 G0'4 G1'4 B0'4 B1'4   R0'5 R1'5 G0'5 G1'5 B0'5 B1'5   R0'6 R1'6 G0'6 G1'6 B0'6 B1'6   
        //
        //  followed by reversed 42 bits of RGB 2\3:    bits 44 - 87 
        //    
        //       Most significant 7                                    4                                         3
        //  B2'6 B3'6 G2'6 G3'6 R2'6 R3'6   B2'5 B3'5 G2'5 G3'5 R2'5 R3'5   B2'4 B3'4 G2'4 G3'4 R2'4 R3'4   B2'3 B3'3 G2'3 G3'3 R2'3 R3'3   B2'2 B3'2 G2'2 G3'2 R2'2 R3'2   B2'1 B3'1 G2'1 G3'1 R2'1 R3'1   B2'0 B3'0 G2'0 G3'0 R2'0 R3'0   P2 P3
        //
    
        // 6 bits of partition from misc
        WriteBitsToDword(miscByte0 & 0x3F, 4 /*6 bit(s)*/, raw[0]);
        
        // R0, R1
        uint base = 2;
        for (uint i = 0; i < 2; ++i)
        {
            for (uint j = 0; j < 7; ++j) // Each Component has 7 bits
            {
                if (dwordPosition == 32)
                {
                    dwordPosition = 0;
                    dwordIndex++;
                }
                uint colorTotalIndex = (base + i) + (kChannels * j);
                WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            }
        }
    
        // R2, R3 
        base = 84;
        for (uint i = 0; i < 2; ++i)
        {
            for (uint j = 0; j < 7; ++j) // Each Component has 7 bits
            {
                if (dwordPosition == 32)
                {
                    dwordPosition = 0;
                    dwordIndex++;
                }
                uint colorTotalIndex = (base + i) - (kChannels * j);
                WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            }
        }
    
        // G0, G1 
        base = 4;
        for (uint i = 0; i < 2; ++i)
        {
            for (uint j = 0; j < 7; ++j) // Each Component has 7 bits
            {
                if (dwordPosition == 32)
                {
                    dwordPosition = 0;
                    dwordIndex++;
                }
                uint colorTotalIndex = (base + i) + (kChannels * j);
                WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            }
        }
    
        // G2, G3 
        base = 82;
        for (uint i = 0; i < 2; ++i)
        {
            for (uint j = 0; j < 7; ++j) // Each Component has 7 bits
            {
                if (dwordPosition == 32)
                {
                    dwordPosition = 0;
                    dwordIndex++;
                }
                uint colorTotalIndex = (base + i) - (kChannels * j);
                WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            }
        }
    
        // B0, B1 
        base = 6;
        for (uint i = 0; i < 2; ++i)
        {
            for (uint j = 0; j < 7; ++j) // Each Component has 7 bits
            {
                if (dwordPosition == 32)
                {
                    dwordPosition = 0;
                    dwordIndex++;
                }
                uint colorTotalIndex = (base + i) + (kChannels * j);
                WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            }
        }
    
        // B2, B3 
        base = 80;
        for (uint i = 0; i < 2; ++i)
        {
            for (uint j = 0; j < 7; ++j) // Each Component has 7 bits
            {
                if (dwordPosition == 32)
                {
                    dwordPosition = 0;
                    dwordIndex++;
                }
                uint colorTotalIndex = (base + i) - (kChannels * j);
                WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            }
        }
        
        // PBits use 4 bits from color. Two from colorByte0 [ ______** ] and two from colorByte10 [ **______ ]
        WriteBitsToDword(colorBytes[0] & 0x3, 30 /*2 bit(s)*/, raw[2]);
        WriteBitsToDword(colorBytes[10] >> 6, 0  /*2 bit(s)*/, raw[3]);
    
        // Misc last 30 bits are index data
        WriteBitsToDword(miscByte0 >> 6,    2  /*2 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte1,         4  /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte2,         12 /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte3,         20 /*8 bit(s)*/, raw[3]);
    }
    else if (modePattern == EndpointPairSignificantBitInderleaved)
    {
        //  This shuffle interleaves bits in significance order from each endpoint pair
        //  
        //  42 bits of RGB 0\1:    bits 6 - 47, padded with 6 other bits to make 6 bytes
        //                                                                                                                                                                                                                             (Most significant)
        //  [ 6 bits partition ]  R0'0 R1'0 G0'0 G1'0 B0'0 B1'0    R0'1 R1'1 G0'1 G1'1 B0'1 B1'1    R0'2 R1'2 G0'2 G1'2 B0'2 B1'2    R0'3 R1'3 G0'3 G1'3 B0'3 B1'3    R0'4 R1'4 G0'4 G1'4 B0'4 B1'4   R0'5 R1'5 G0'5 G1'5 B0'5 B1'5    R0'6 R1'6 G0'6 G1'6 B0'6 B1'6
        //
        //  42 bits of RGB 2\3:    bits 54 - 95, padded with 6 other bits to make 5 bytes
        //                                                                                                                                                                                                                             (Most significant)
        //  [ 4 pbits, 2 idx ]    R2'0 R3'0 G2'0 G3'0 B2'0 B3'0    R2'1 R3'1 G2'1 G3'1 B2'1 B3'1    R2'2 R3'2 G2'2 G3'2 B2'2 B3'2    R2'3 R3'3 G2'3 G3'3 B2'3 B3'3    R2'4 R3'4 G2'4 G3'4 B2'4 B3'4   R2'5 R3'5 G2'5 G3'5 B2'5 B3'5    R2'6 R3'6 G2'6 G3'6 B2'6 B3'6 
        //
    
        // 6 bits of partition from color
        WriteBitsToDword(colorBytes[0] & 0x3F, 4 /*6 bit(s)*/, raw[0]);
        
        // R0, R1
        uint base = 6;
        for (uint i = 0; i < 2; ++i)
        {
            for (uint j = 0; j < 7; ++j) // Each Component has 7 bits
            {
                if (dwordPosition == 32)
                {
                    dwordPosition = 0;
                    dwordIndex++;
                }
                uint colorTotalIndex = (base + i) + (kChannels * j);
                WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            }
        }
        
         // R2, R3 
        base = 54;
        for (uint i = 0; i < 2; ++i)
        {
            for (uint j = 0; j < 7; ++j) // Each Component has 7 bits
            {
                if (dwordPosition == 32)
                {
                    dwordPosition = 0;
                    dwordIndex++;
                }
                uint colorTotalIndex = (base + i) + (kChannels * j);
                WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            }
        }
        
         // G0, G1 
        base = 8;
        for (uint i = 0; i < 2; ++i)
        {
            for (uint j = 0; j < 7; ++j) // Each Component has 7 bits
            {
                if (dwordPosition == 32)
                {
                    dwordPosition = 0;
                    dwordIndex++;
                }
                uint colorTotalIndex = (base + i) + (kChannels * j);
                WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            }
        }
    
        // G2, G3 
        base = 56;
        for (uint i = 0; i < 2; ++i)
        {
            for (uint j = 0; j < 7; ++j) // Each Component has 7 bits
            {
                if (dwordPosition == 32)
                {
                    dwordPosition = 0;
                    dwordIndex++;
                }
                uint colorTotalIndex = (base + i) + (kChannels * j);
                WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            }
        }
    
        // B0, B1 
        base = 10;
        for (uint i = 0; i < 2; ++i)
        {
            for (uint j = 0; j < 7; ++j) // Each Component has 7 bits
            {
                if (dwordPosition == 32)
                {
                    dwordPosition = 0;
                    dwordIndex++;
                }
                uint colorTotalIndex = (base + i) + (kChannels * j);
                WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            }
        }
    
        // B2, B3 
        base = 58;
        for (uint i = 0; i < 2; ++i)
        {
            for (uint j = 0; j < 7; ++j) // Each Component has 7 bits
            {
                if (dwordPosition == 32)
                {
                    dwordPosition = 0;
                    dwordIndex++;
                }
                uint colorTotalIndex = (base + i) + (kChannels * j);
                WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            }
        }
        
        // PBits use 4 bits from color. Two from colorByte0 [ ______** ] and two from colorByte10 [ **______ ]
        WriteBitsToDword(colorBytes[6] & 0x3,           30 /*2 bit(s)*/, raw[2]);
        WriteBitsToDword((colorBytes[6] >> 2) & 0xF,    0  /*4 bit(s)*/, raw[3]);
    
        // Misc last 30 bits are index data
        WriteBitsToDword(miscByte0, 4  /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte1, 12 /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte2, 20 /*8 bit(s)*/, raw[3]);
    }
        
    WriteBitsToDword(ScrapData, 28 /*4 bit(s)*/, raw[3]);
}

// For mode 4
static uint M4StaticBitStarts[9] =
{
    16, // 0 statics
    16, // 1 statics
    16, // 2 statics
    16, // 3 statics 
    8, // 4 statics
    8, // 5 statics
    8, // 6 statics
    0, // 7 statics
    0 // 8 statics
};
            
// For mode 4
static uint M4OtherByteOrder[9][5] =
{
    { 2, 3, 1, 4, 0 }, // 0 statics
    { 2, 3, 1, 4, 0 }, // 1 statics
    { 2, 3, 1, 4, 0 }, // 2 statics
    { 2, 3, 1, 4, 0 }, // 3 statics
    { 1, 2, 3, 0, 4 }, // 4 statics
    { 1, 2, 3, 4, 0 }, // 5 statics
    { 1, 2, 3, 4, 0 }, // 6 statics
    { 0, 1, 2, 3, 4 }, // 7 statics
    { 0, 1, 2, 3, 4 }, // 8 statics
};

void unshuffleMode4(uint colorAddress, uint miscAddress, uint ScrapData, uint modePattern, uint modeStatics,
    uint lowEntropy, in ByteAddressBuffer buffer, inout uint4 raw)
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
    
    // 5  / 5  bytes color
    // 10 / 10 bytes misc
    // 3 bits scrap
    
    // Color Data
    uint colorBytes[5];
    colorBytes[0] = fetchNextByte(colorAddress, buffer);
    colorBytes[1] = fetchNextByte(colorAddress + 1, buffer);
    colorBytes[2] = fetchNextByte(colorAddress + 2, buffer);
    colorBytes[3] = fetchNextByte(colorAddress + 3, buffer);
    colorBytes[4] = fetchNextByte(colorAddress + 4, buffer);
    
    // Misc Data (6 bytes)
    uint miscByte0 = fetchNextByte(miscAddress, buffer);
    uint miscByte1 = fetchNextByte(miscAddress + 1, buffer);
    uint miscByte2 = fetchNextByte(miscAddress + 2, buffer);
    uint miscByte3 = fetchNextByte(miscAddress + 3, buffer);
    uint miscByte4 = fetchNextByte(miscAddress + 4, buffer);
    uint miscByte5 = fetchNextByte(miscAddress + 5, buffer);
    uint miscByte6 = fetchNextByte(miscAddress + 6, buffer);
    uint miscByte7 = fetchNextByte(miscAddress + 7, buffer);
    uint miscByte8 = fetchNextByte(miscAddress + 8, buffer);
    uint miscByte9 = fetchNextByte(miscAddress + 9, buffer);
        
    // Write the mode
    WriteBitsToDword(16u, 0 /*5 bit(s)*/, raw[0]);
    
    // Rotation 1st bit use Misc's 31st bit ( byte3 *_______ )
    WriteBitsToDword(miscByte3 >> 7, 5 /*1 bit(s)*/, raw[0]);
    
    // Rotation 2nd bit is from scrap bit [0]
    WriteBitsToDword(ScrapData & 0x1, 6 /*1 bit(s)*/, raw[0]);
    
    // 1 bit index mode from Misc 79th ( byte9 *_______ )
    WriteBitsToDword(miscByte9 >> 7, 7 /*1 bit(s)*/, raw[0]);
    
    if (modePattern == StableIsland) // matches experimental ID 13
    {
        // This mode is the "stable island" dynamic mode that depends on including a "statics" header to mark channels.  
        // Bits are then arranged around this stable island within the color stream
        // the pattern looks different based on the number of static fields identified.  
            
        // ENDPOINT ORDER:  A0  A1  B0  B1  G0  G1  R0  R1 
        uint order[8] = { 6, 7, 4, 5, 2, 3, 0, 1 };
            
        // For loop that mofidies order[] 
        // for each static, it decreases every index greater than it, and then zeroes itself
        // So if static count is zero, order remains as is.
        for (uint ep = 0; ep < 8; ++ep)
        {
            if ((modeStatics | lowEntropy) & (1u << ep))
            {
                // move other endpoints up in the order, to account for the 
                // removal of this static field from the normal order
                for (uint ee = 0; ee < 8; ee++)
                {
                    if (order[ee] > order[ep]) order[ee]--;
                }
                order[ep] = 0;
            }
        }
            
        uint staticFields = countbits(modeStatics | lowEntropy);
        uint staticBits = staticFields * 5;
        uint otherFields = 8 - staticFields;
            
        uint nextStaticBit = M4StaticBitStarts[staticFields];
        uint nextScrapBit = 1;
            
        uint dwordIndex = 0;
        uint dwordPosition = 8;
            
        // Iterate through each endpoint { R0, R1, G0, G1, B0, B1, A0, A1 }
        // Check if the endpoint is (static|lowEntropy), or if it belongs to "others"
        // If its (static|lowEntropy) -> Pick the 5 bits from its location
        // If its not, pick 1 bit at a time by reverting the destRemapped 
        for (uint ep = 0; ep < 8; ep++)
        {
            // For statics, copy all 5 bits at once
            if ((modeStatics | lowEntropy) & (1u << ep))
            {
                // Find the offset into the colorstream data where the 5 bits of static endpoint data were written
                uint colorByteIndex = nextStaticBit / 8;
                uint colorBitIndex = nextStaticBit % 8;
                    
                uint intermediateEndpoint = 0u;
                    
                if (colorBitIndex > 3)
                {
                    uint lowBits = 8 - colorBitIndex;
                    uint highBits = 5 - lowBits;
                        
                    uint mask = (~0u) >> (32 - lowBits);
                    WriteBitsToDword((colorBytes[colorByteIndex] >> colorBitIndex) & mask, 0, intermediateEndpoint);
                        
                    mask = (~0u) >> (32 - highBits);
                    WriteBitsToDword(colorBytes[colorByteIndex + 1] & mask, lowBits, intermediateEndpoint);
                }
                else
                {
                    WriteBitsToDword((colorBytes[colorByteIndex] >> colorBitIndex) & 0x1Fu, 0 /*5 bit(s)*/, intermediateEndpoint);
                }
                    
                // Now write the 5 bits to the final position
                // Careful with the case where part goes to Raw0 and part to Raw1
                if (32 - dwordPosition < 5)
                {
                    uint bitsToRaw0 = 32 - dwordPosition;
                    uint bitsToRaw1 = 5 - bitsToRaw0;
                        
                    uint mask = (~0u) >> (32 - bitsToRaw0);
                    WriteBitsToDword(intermediateEndpoint & mask, dwordPosition, raw[dwordIndex]);
                        
                    dwordIndex += 1;
                    dwordPosition = 0;
                        
                    WriteBitsToDword(intermediateEndpoint >> bitsToRaw0, dwordPosition, raw[dwordIndex]);
                    dwordPosition += bitsToRaw1;
                }
                else
                {
                    WriteBitsToDword(intermediateEndpoint & 0x1Fu, dwordPosition /*5 bit(s)*/, raw[dwordIndex]);
                    dwordPosition += 5;
                }
                    
                nextStaticBit += 5;
            }
            // Copy 1 bit at a time, the bits are not contiguous
            else
            {
                uint intermediateEndpoint = 0u;
                uint intPosition = 0;
                for (uint b = 0; b < 5; ++b)
                {
                    const uint bi = 4 - b;
                    const uint destLinear = staticBits + order[ep] + (bi * otherFields);
                    const uint destLinearByte = destLinear / 8;
                    const uint destLinearBit = destLinear % 8;
                    const uint destRemappedByte = M4OtherByteOrder[staticFields][destLinearByte];
                    const uint destRemapped = 8 * destRemappedByte + destLinearBit;
                        
                    // Find the offset into color where the single bit was written
                    uint colorByteIndex = destRemapped / 8;
                    uint colorBitIndex = destRemapped % 8;
                        
                    WriteBitsToDword((colorBytes[colorByteIndex] >> colorBitIndex) & 0x1u, intPosition /*1 bit(s)*/, intermediateEndpoint);
                    intPosition += 1;
                }
                    
                if (32 - dwordPosition < 5)
                {
                    uint firstHalf = 32 - dwordPosition;
                    uint secondHalf = 5 - firstHalf;
                        
                    uint mask = (~0u) >> (32 - firstHalf);;
                    WriteBitsToDword(intermediateEndpoint & mask, dwordPosition, raw[dwordIndex]);
                    dwordIndex++;
                    dwordPosition = 0;
                        
                    WriteBitsToDword(intermediateEndpoint >> firstHalf, dwordPosition, raw[dwordIndex]);
                    dwordPosition += secondHalf;
                }
                else
                {
                    WriteBitsToDword(intermediateEndpoint & 0x1Fu, dwordPosition /*5 bit(s)*/, raw[dwordIndex]);
                    dwordPosition += 5;
                }
            }
        }
    }
    else if (modePattern == EndpointPairSignificantBitInderleaved) // matches experimental ID 3
    {
        WriteBitsToDword((colorBytes[0]) & 0x1, 8  /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[1]) & 0x1, 9  /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[2]) & 0x1, 10 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[3]) & 0x1, 11 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[4]) & 0x1, 12 /*1 bit(s)*/, raw[0]);
    
        WriteBitsToDword((colorBytes[0] >> 1) & 0x1, 13 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[1] >> 1) & 0x1, 14 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[2] >> 1) & 0x1, 15 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[3] >> 1) & 0x1, 16 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[4] >> 1) & 0x1, 17 /*1 bit(s)*/, raw[0]);
    
        WriteBitsToDword((colorBytes[0] >> 2) & 0x1, 18 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[1] >> 2) & 0x1, 19 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[2] >> 2) & 0x1, 20 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[3] >> 2) & 0x1, 21 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[4] >> 2) & 0x1, 22 /*1 bit(s)*/, raw[0]);
                                   
        WriteBitsToDword((colorBytes[0] >> 3) & 0x1, 23 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[1] >> 3) & 0x1, 24 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[2] >> 3) & 0x1, 25 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[3] >> 3) & 0x1, 26 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[4] >> 3) & 0x1, 27 /*1 bit(s)*/, raw[0]);
                                   
        WriteBitsToDword((colorBytes[0] >> 4) & 0x1, 28 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[1] >> 4) & 0x1, 29 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[2] >> 4) & 0x1, 30 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[3] >> 4) & 0x1, 31 /*1 bit(s)*/, raw[0]);
        WriteBitsToDword((colorBytes[4] >> 4) & 0x1, 0  /*1 bit(s)*/, raw[1]);
                                   
        WriteBitsToDword((colorBytes[0] >> 5) & 0x1, 1 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword((colorBytes[1] >> 5) & 0x1, 2 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword((colorBytes[2] >> 5) & 0x1, 3 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword((colorBytes[3] >> 5) & 0x1, 4 /*1 bit(s)*/, raw[1]);
        WriteBitsToDword((colorBytes[4] >> 5) & 0x1, 5 /*1 bit(s)*/, raw[1]);
              
        WriteBitsToDword((colorBytes[0] >> 6) & 0x1, 6  /*1 bit(s)*/, raw[1]); // A0'0
        WriteBitsToDword((colorBytes[1] >> 6) & 0x1, 7  /*1 bit(s)*/, raw[1]); // A0'1
        WriteBitsToDword((colorBytes[2] >> 6) & 0x1, 8  /*1 bit(s)*/, raw[1]); // A0'2
        WriteBitsToDword((colorBytes[3] >> 6) & 0x1, 9  /*1 bit(s)*/, raw[1]); // A0'3
        WriteBitsToDword((colorBytes[4] >> 6) & 0x1, 10 /*1 bit(s)*/, raw[1]); // A0'4
                                   
        WriteBitsToDword((colorBytes[0] >> 7) & 0x1, 11 /*1 bit(s)*/, raw[1]); // A1'0
        WriteBitsToDword((colorBytes[1] >> 7) & 0x1, 12 /*1 bit(s)*/, raw[1]); // A1'1
        WriteBitsToDword((colorBytes[2] >> 7) & 0x1, 13 /*1 bit(s)*/, raw[1]); // A1'2
        WriteBitsToDword((colorBytes[3] >> 7) & 0x1, 14 /*1 bit(s)*/, raw[1]); // A1'3
        WriteBitsToDword((colorBytes[4] >> 7) & 0x1, 15 /*1 bit(s)*/, raw[1]); // A1'4
    }
    
    // Mask off the upper bits of raw[1]
    raw[1] = raw[1] & 0x0003FFFF;
    
    // Index data 1 (31 bits)
    WriteBitsToDword(miscByte0,         18 /*8 bit(s)*/, raw[1]);
    WriteBitsToDword(miscByte1 & 0x3F,  26 /*6 bit(s)*/, raw[1]); // __******
    WriteBitsToDword(miscByte1 >> 6,    0  /*2 bit(s)*/, raw[2]); // **______
    WriteBitsToDword(miscByte2,         2  /*8 bit(s)*/, raw[2]);
    WriteBitsToDword(miscByte3 & 0x7F,  10 /*7 bit(s)*/, raw[2]);
        
    // Index data 2 (47 bits)
    WriteBitsToDword(miscByte4,         17 /*8 bit(s)*/, raw[2]);
    WriteBitsToDword(miscByte5 & 0x7F,  25 /*7 bit(s)*/, raw[2]); // _*******
    WriteBitsToDword(miscByte5 >> 7,    0  /*1 bit(s)*/, raw[3]); // *_______
    WriteBitsToDword(miscByte6,         1  /*8 bit(s)*/, raw[3]);
    WriteBitsToDword(miscByte7,         9  /*8 bit(s)*/, raw[3]);
    WriteBitsToDword(miscByte8,         17 /*8 bit(s)*/, raw[3]);
    WriteBitsToDword(miscByte9 & 0x7F,  25 /*7 bit(s)*/, raw[3]);
}

// For modes 5-6 (stable island pattern)
static uint staticBitStarts[9] =
{
    24, // 0 statics
    24, // 1 statics
    24, // 2 statics
    16, // 3 statics 
    16, // 4 statics
    8, // 5 statics
    8, // 6 statics
    0, // 7 statics
    0 // 8 statics
};
            
// For modes 5-6 (stable island pattern)
static uint otherByteOrder[9][7] =
{
    { 3, 4, 2, 5, 1, 6, 0 }, // 0 statics
    { 3, 4, 2, 5, 1, 6, 0 }, // 1 statics
    { 3, 4, 2, 5, 1, 6, 0 }, // 2 statics
    { 2, 3, 4, 1, 5, 0, 6 }, // 3 statics
    { 2, 3, 4, 5, 1, 6, 0 }, // 4 statics
    { 1, 2, 3, 4, 5, 0, 6 }, // 5 statics
    { 1, 2, 3, 4, 5, 6, 0 }, // 6 statics
    { 0, 1, 2, 3, 4, 5, 6 }, // 7 statics
    { 0, 1, 2, 3, 4, 5, 6 }, // 8 statics
};

void unshuffleMode5(uint colorAddress, uint miscAddress, uint ScrapData, uint modePattern,
    uint modeStatics, uint lowEntropy, in ByteAddressBuffer buffer, inout uint4 raw)
{
    // Mode 5
    // 6 bit mode               =>  6 bits
    // 2 bits rotation          =>  2 bits
    // 7 bits R0 - 7 bits R1    => 14 bits
    // 7 bits B0 - 7 bits G1    => 14 bits
    // 7 bits G0 - 7 bits B1    => 14 bits
    // 8 bits A0 - 8 bits A1    => 16 bits
    // 31 bits index color      => 31 bits
    // 31 bits index alpha      => 31 bits
    
    // 7 / 7 bytes color
    // 8 / 8 bytes misc
    // 2 bits scrap + extraScrap (?)
    
    // Color Data
    uint colorBytes[7];
    colorBytes[0] = fetchNextByte(colorAddress, buffer);
    colorBytes[1] = fetchNextByte(colorAddress + 1, buffer);
    colorBytes[2] = fetchNextByte(colorAddress + 2, buffer);
    colorBytes[3] = fetchNextByte(colorAddress + 3, buffer);
    colorBytes[4] = fetchNextByte(colorAddress + 4, buffer);
    colorBytes[5] = fetchNextByte(colorAddress + 5, buffer);
    colorBytes[6] = fetchNextByte(colorAddress + 6, buffer);
    
    // Misc Data (6 bytes)
    uint miscByte0 = fetchNextByte(miscAddress, buffer);
    uint miscByte1 = fetchNextByte(miscAddress + 1, buffer);
    uint miscByte2 = fetchNextByte(miscAddress + 2, buffer);
    uint miscByte3 = fetchNextByte(miscAddress + 3, buffer);
    uint miscByte4 = fetchNextByte(miscAddress + 4, buffer);
    uint miscByte5 = fetchNextByte(miscAddress + 5, buffer);
    uint miscByte6 = fetchNextByte(miscAddress + 6, buffer);
    uint miscByte7 = fetchNextByte(miscAddress + 7, buffer);
    
    // Write the mode
    WriteBitsToDword(32u, 0 /*6 bit(s)*/, raw[0]);
        
    // rotation 0 - 1
    WriteBitsToDword(miscByte3 >> 7, 6 /*1 bit(s)*/, raw[0]);
    WriteBitsToDword(miscByte7 >> 7, 7 /*1 bit(s)*/, raw[0]);
    
    // Color data depends on the modePattern
    if (modePattern == StableIsland) // matches experimental ID 13
    {
        // This mode is the "stable island" dynamic mode that depends on including a "statics" header to mark channels.  
        // Bits are then arranged around this stable island within the color stream
        // the pattern looks different based on the number of static fields identified.  

        //  56 color bits
        // byte         0                        1                        2                      3                      4                       5                        6
        //     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55   non-static  req'for 3 bytes, 4 bytes,  5 bytes,  6 bytes
        // 0 statics   7th           -          5th          -           3rd         -          1st          -          2nd          -          4th          -           6th                 8           24           32         40        48
        // 1           7th           -          5th          -           3rd         - 0  1  2  3  4  5  6  *-          2nd          -          4th          -           6th                 7           17           25         33        41
        // 2           7th           -          5th          -           3rd         - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  *  *-          4th          -           6th                 6           10           18         26        34
        // 3           6th           -          4th          - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  *  *  *-          5th          -           7th                 5            3           11         19        27
        // 4           7th           -          5th          - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  *  *  *  *-           6th                 4                         4         12        20
        // 5           6th           - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  *  *  *  *  *-           7th                 3                                   5         13
        // 6           7th           - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  0  1  2  3  4- 5  6  *  *  *  *  *  *        2                                              6
        // 7   0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  0  1  2  3  4- 5  6  0  1  2  3  4  5- 6  *  *  *  *  *  *  *        1  
        // 8   0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  0  1  2  3  4_ 5  6  0  1  2  3  4  5- 6  0  1  2  3  4  5  6        0  
        
        // ENDPOINT ORDER:   A0  A1  B0  B1  G0  G1  R0  R1 
        uint order[8] = { 6, 7, 4, 5, 2, 3, 0, 1 };
        
        // For loop that mofidies order[] 
        // for each static, it decreases every index greater than it, and then zeroes itself
        // So if static count is zero, order remains as is.
        for (uint ep = 0; ep < 8; ++ep)
        {
            if ((modeStatics | lowEntropy) & (1u << ep))
            {
                // move other endpoints up in the order, to account for the 
                // removal of this static field from the normal order
                for (uint ee = 0; ee < 8; ee++)
                {
                    if (order[ee] > order[ep]) order[ee]--;
                }
                order[ep] = 0;
            }
        }
            
        uint staticFields = countbits(modeStatics | lowEntropy);
        uint staticBits = staticFields * 7;
        uint otherFields = 8 - staticFields;
            
        uint nextStaticBit = staticBitStarts[staticFields];
        uint nextScrapBit = 1;
            
        uint dwordIndex = 0;
        uint dwordPosition = 8;
            
        // Iterate through each endpoint { R0, R1, G0, G1, B0, B1, A0, A1 }
        // Check if the endpoint is (static|lowEntropy), or if it belongs to "others"
        // If its (static|lowEntropy) -> Pick the 5 bits from its location
        // If its not, pick 1 bit at a time by reverting the destRemapped 
        for (uint ep = 0; ep < 8; ep++)
        {
            // For statics, copy all 5 bits at once
            if ((modeStatics | lowEntropy) & (1u << ep))
            {
                // Find the offset into the colorstream data where the 5 bits of static endpoint data were written
                uint colorByteIndex = nextStaticBit / 8;
                uint colorBitIndex = nextStaticBit % 8;
                    
                uint intermediateEndpoint = 0u;
                    
                if (colorBitIndex > 1)
                {
                    uint lowBits = 8 - colorBitIndex;
                    uint highBits = 7 - lowBits;
                        
                    uint mask = (~0u) >> (32 - lowBits);
                    WriteBitsToDword((colorBytes[colorByteIndex] >> colorBitIndex) & mask, 0 /*lowBits bit(s)*/, intermediateEndpoint);
                        
                    mask = (~0u) >> (32 - highBits);
                    WriteBitsToDword(colorBytes[colorByteIndex + 1] & mask, lowBits /*highBits bit(s)*/, intermediateEndpoint);
                }
                else
                {
                    WriteBitsToDword((colorBytes[colorByteIndex] >> colorBitIndex) & 0x7Fu, 0 /*7 bit(s)*/, intermediateEndpoint);
                }
                    
                // Now write the 7 bits to the final position
                if (32 - dwordPosition < 7)
                {
                    uint bitsToRaw0 = 32 - dwordPosition;
                    uint bitsToRaw1 = 7 - bitsToRaw0;
                        
                    uint mask = (~0u) >> (32 - bitsToRaw0);
                    WriteBitsToDword(intermediateEndpoint & mask, dwordPosition /*bitsToRaw0 bit(s)*/, raw[dwordIndex]);
                        
                    dwordIndex += 1;
                    dwordPosition = 0;
                        
                    WriteBitsToDword(intermediateEndpoint >> bitsToRaw0, dwordPosition /*bitsToRaw1 bit(s)*/, raw[dwordIndex]);
                    dwordPosition += bitsToRaw1;
                }
                else
                {
                    WriteBitsToDword(intermediateEndpoint & 0x7Fu, dwordPosition /*7 bit(s)*/, raw[dwordIndex]);
                    dwordPosition += 7;
                }
                    
                nextStaticBit += 7;
            }
            // Copy 1 bit at a time, the bits are not contiguous
            else
            {
                uint intermediateEndpoint = 0u;
                uint intPosition = 0;
                for (uint b = 0; b < 7; ++b)
                {
                    const uint bi = 6 - b;
                    const uint destLinear = staticBits + order[ep] + (bi * otherFields);
                    const uint destLinearByte = destLinear / 8;
                    const uint destLinearBit = destLinear % 8;
                    const uint destRemappedByte = otherByteOrder[staticFields][destLinearByte];
                    const uint destRemapped = 8 * destRemappedByte + destLinearBit;
                        
                    // Find the offset into color where the single bit was written
                    uint colorByteIndex = destRemapped / 8;
                    uint colorBitIndex = destRemapped % 8;
                        
                    WriteBitsToDword((colorBytes[colorByteIndex] >> colorBitIndex) & 0x1u, intPosition /*1 bit(s)*/, intermediateEndpoint);
                    intPosition += 1;
                }
                    
                if (32 - dwordPosition < 7)
                {
                    uint firstHalf = 32 - dwordPosition;
                    uint secondHalf = 7 - firstHalf;
                        
                    uint mask = (~0u) >> (32 - firstHalf);;
                    WriteBitsToDword(intermediateEndpoint & mask, dwordPosition /*firstHalf bit(s)*/, raw[dwordIndex]);
                    dwordIndex++;
                    dwordPosition = 0;
                        
                    WriteBitsToDword(intermediateEndpoint >> firstHalf, dwordPosition /*secondHalf bit(s)*/, raw[dwordIndex]);
                    dwordPosition += secondHalf;
                }
                else
                {
                    WriteBitsToDword(intermediateEndpoint & 0x7Fu, dwordPosition /*7 bit(s)*/, raw[dwordIndex]);
                    dwordPosition += 7;
                }
            }
        }
    }
    else if (modePattern == EndpointPairSignificantBitInderleaved) // matches experimental ID 3
    {
        //  This shuffle treats 0/1 endpoints as a unit, and interleaves bits in significant bit order
        //  the most significant bits are placed at a 

        //  56 bits of RGBA 0\1:    bits 0 - 55
        //                  byte 0                                     byte 1                                 byte 2                                   byte 3                                  byte 4                                    byte 5                                  (Most significant)
        //  R0'0 R1'0 G0'0 G1'0 B0'0 B1'0 A0'0 A1'0  R0'1 R1'1 G0'1 G1'1 B0'1 B1'1 A0'1 A1'1   R0'2 R1'2 G0'2 G1'2 B0'2 B1'2 A0'2 A1'2   R0'3 R1'3 G0'3 G1'3 B0'3 B1'3 A0'3 A1'3   R0'4 R1'4 G0'4 G1'4 B0'4 B1'4 A0'4 A1'4  R0'5 R1'5 G0'5 G1'5 B0'5 B1'5 A0'5 A1'5  R0'6 R1'6 G0'6 G1'6 B0'6 B1'6 A0'6 A1'6
        //
        //
        
        // Color Data
        WriteBitsToDword((colorBytes[0]) & 0x1, 8  /*1 bit(s)*/, raw[0]); // R0 *
        WriteBitsToDword((colorBytes[1]) & 0x1, 9  /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorBytes[2]) & 0x1, 10 /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorBytes[3]) & 0x1, 11 /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorBytes[4]) & 0x1, 12 /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorBytes[5]) & 0x1, 13 /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorBytes[6]) & 0x1, 14 /*1 bit(s)*/, raw[0]); // R0
        
        WriteBitsToDword((colorBytes[0] >> 1) & 0x1, 15 /*1 bit(s)*/, raw[0]); // R1 *
        WriteBitsToDword((colorBytes[1] >> 1) & 0x1, 16 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorBytes[2] >> 1) & 0x1, 17 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorBytes[3] >> 1) & 0x1, 18 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorBytes[4] >> 1) & 0x1, 19 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorBytes[5] >> 1) & 0x1, 20 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorBytes[6] >> 1) & 0x1, 21 /*1 bit(s)*/, raw[0]); // R1
        
        WriteBitsToDword((colorBytes[0] >> 2) & 0x1, 22 /*1 bit(s)*/, raw[0]); // G0 *
        WriteBitsToDword((colorBytes[1] >> 2) & 0x1, 23 /*1 bit(s)*/, raw[0]); // G0
        WriteBitsToDword((colorBytes[2] >> 2) & 0x1, 24 /*1 bit(s)*/, raw[0]); // G0
        WriteBitsToDword((colorBytes[3] >> 2) & 0x1, 25 /*1 bit(s)*/, raw[0]); // G0
        WriteBitsToDword((colorBytes[4] >> 2) & 0x1, 26 /*1 bit(s)*/, raw[0]); // G0
        WriteBitsToDword((colorBytes[5] >> 2) & 0x1, 27 /*1 bit(s)*/, raw[0]); // G0
        WriteBitsToDword((colorBytes[6] >> 2) & 0x1, 28 /*1 bit(s)*/, raw[0]); // G0
                                 
        WriteBitsToDword((colorBytes[0] >> 3) & 0x1, 29 /*1 bit(s)*/, raw[0]); // G1 *
        WriteBitsToDword((colorBytes[1] >> 3) & 0x1, 30 /*1 bit(s)*/, raw[0]); // G1
        WriteBitsToDword((colorBytes[2] >> 3) & 0x1, 31 /*1 bit(s)*/, raw[0]); // G1
        WriteBitsToDword((colorBytes[3] >> 3) & 0x1, 0  /*1 bit(s)*/, raw[1]); // G1
        WriteBitsToDword((colorBytes[4] >> 3) & 0x1, 1  /*1 bit(s)*/, raw[1]); // G1
        WriteBitsToDword((colorBytes[5] >> 3) & 0x1, 2  /*1 bit(s)*/, raw[1]); // G1
        WriteBitsToDword((colorBytes[6] >> 3) & 0x1, 3  /*1 bit(s)*/, raw[1]); // G1
                                  
        WriteBitsToDword((colorBytes[0] >> 4) & 0x1, 4  /*1 bit(s)*/, raw[1]); // B0 *
        WriteBitsToDword((colorBytes[1] >> 4) & 0x1, 5  /*1 bit(s)*/, raw[1]); // B0
        WriteBitsToDword((colorBytes[2] >> 4) & 0x1, 6  /*1 bit(s)*/, raw[1]); // B0
        WriteBitsToDword((colorBytes[3] >> 4) & 0x1, 7  /*1 bit(s)*/, raw[1]); // B0
        WriteBitsToDword((colorBytes[4] >> 4) & 0x1, 8  /*1 bit(s)*/, raw[1]); // B0
        WriteBitsToDword((colorBytes[5] >> 4) & 0x1, 9  /*1 bit(s)*/, raw[1]); // B0
        WriteBitsToDword((colorBytes[6] >> 4) & 0x1, 10 /*1 bit(s)*/, raw[1]); // B0
                                   
        WriteBitsToDword((colorBytes[0] >> 5) & 0x1, 11 /*1 bit(s)*/, raw[1]); // B1 *
        WriteBitsToDword((colorBytes[1] >> 5) & 0x1, 12 /*1 bit(s)*/, raw[1]); // B1
        WriteBitsToDword((colorBytes[2] >> 5) & 0x1, 13 /*1 bit(s)*/, raw[1]); // B1
        WriteBitsToDword((colorBytes[3] >> 5) & 0x1, 14 /*1 bit(s)*/, raw[1]); // B1
        WriteBitsToDword((colorBytes[4] >> 5) & 0x1, 15 /*1 bit(s)*/, raw[1]); // B1
        WriteBitsToDword((colorBytes[5] >> 5) & 0x1, 16 /*1 bit(s)*/, raw[1]); // B1
        WriteBitsToDword((colorBytes[6] >> 5) & 0x1, 17 /*1 bit(s)*/, raw[1]); // B1
                                 
        //WriteBitsToDword(ScrapData & 0x1, 18, 1, raw[1]); // ex0
                               
        WriteBitsToDword((colorBytes[0] >> 6) & 0x1, 18 /*1 bit(s)*/, raw[1]); // A0 *
        WriteBitsToDword((colorBytes[1] >> 6) & 0x1, 19 /*1 bit(s)*/, raw[1]); // A0
        WriteBitsToDword((colorBytes[2] >> 6) & 0x1, 20 /*1 bit(s)*/, raw[1]); // A0
        WriteBitsToDword((colorBytes[3] >> 6) & 0x1, 21 /*1 bit(s)*/, raw[1]); // A0
        WriteBitsToDword((colorBytes[4] >> 6) & 0x1, 22 /*1 bit(s)*/, raw[1]); // A0
        WriteBitsToDword((colorBytes[5] >> 6) & 0x1, 23 /*1 bit(s)*/, raw[1]); // A0
        WriteBitsToDword((colorBytes[6] >> 6) & 0x1, 24 /*1 bit(s)*/, raw[1]); // A0
        
        //WriteBitsToDword((ScrapData >> 1) & 0x1, 26, 1, raw[1]); // ex1          
        
        WriteBitsToDword((colorBytes[0] >> 7) & 0x1, 25 /*1 bit(s)*/, raw[1]); // A1 *
        WriteBitsToDword((colorBytes[1] >> 7) & 0x1, 26 /*1 bit(s)*/, raw[1]); // A1
        WriteBitsToDword((colorBytes[2] >> 7) & 0x1, 27 /*1 bit(s)*/, raw[1]); // A1
        WriteBitsToDword((colorBytes[3] >> 7) & 0x1, 28 /*1 bit(s)*/, raw[1]); // A1
        WriteBitsToDword((colorBytes[4] >> 7) & 0x1, 29 /*1 bit(s)*/, raw[1]); // A1
        WriteBitsToDword((colorBytes[5] >> 7) & 0x1, 30 /*1 bit(s)*/, raw[1]); // A1
        WriteBitsToDword((colorBytes[6] >> 7) & 0x1, 31 /*1 bit(s)*/, raw[1]); // A1
    }
    
    // Index
    WriteBitsToDword(miscByte0,                 2  /*8 bit(s)*/, raw[2]);
    WriteBitsToDword(miscByte1,                 10 /*8 bit(s)*/, raw[2]);
    WriteBitsToDword(miscByte2,                 18 /*8 bit(s)*/, raw[2]);
    WriteBitsToDword(miscByte3 & 0x3F,          26 /*6 bit(s)*/, raw[2]);
    WriteBitsToDword((miscByte3 >> 6) & 0x1u,   0  /*1 bit(s)*/, raw[3]);
        
    WriteBitsToDword(miscByte4,         1  /*8 bit(s)*/, raw[3]);
    WriteBitsToDword(miscByte5,         9  /*8 bit(s)*/, raw[3]);
    WriteBitsToDword(miscByte6,         17 /*8 bit(s)*/, raw[3]);
    WriteBitsToDword(miscByte7 & 0x7F,  25 /*7 bit(s)*/, raw[3]);
}

void unshuffleMode6(uint colorAddress, uint miscAddress, uint ScrapData, uint modePattern,
    uint modeStatics, uint lowEntropy, in ByteAddressBuffer buffer, inout uint4 raw)
{
    // Mode 6
    // 7 bit mode               =>  7 bits
    // 7 bits R0 - 7 bits R1    => 14 bits
    // 7 bits B0 - 7 bits G1    => 14 bits
    // 7 bits G0 - 7 bits B1    => 14 bits
    // 7 bits A0 - 7 bits A1    => 14 bits
    // 1 bit  P0 - 1 bit  P1    => 2 bits
    // 63 bits index            => 63 bits
    
    // 7 / 7 bytes color
    // 8 / 8 bytes misc
    // 1 bit scrap + extraScrap (?)
    
    // Color Data
    uint colorBytes[7];
    colorBytes[0] = fetchNextByte(colorAddress, buffer);
    colorBytes[1] = fetchNextByte(colorAddress + 1, buffer);
    colorBytes[2] = fetchNextByte(colorAddress + 2, buffer);
    colorBytes[3] = fetchNextByte(colorAddress + 3, buffer);
    colorBytes[4] = fetchNextByte(colorAddress + 4, buffer);
    colorBytes[5] = fetchNextByte(colorAddress + 5, buffer);
    colorBytes[6] = fetchNextByte(colorAddress + 6, buffer);
    
    uint miscByte0 = fetchNextByte(miscAddress, buffer);
    uint miscByte1 = fetchNextByte(miscAddress + 1, buffer);
    uint miscByte2 = fetchNextByte(miscAddress + 2, buffer);
    uint miscByte3 = fetchNextByte(miscAddress + 3, buffer);
    uint miscByte4 = fetchNextByte(miscAddress + 4, buffer);
    uint miscByte5 = fetchNextByte(miscAddress + 5, buffer);
    uint miscByte6 = fetchNextByte(miscAddress + 6, buffer);
    uint miscByte7 = fetchNextByte(miscAddress + 7, buffer);
        
    // Write the mode
    WriteBitsToDword(64u, 0 /*7 bit(s)*/, raw[0]);
    
    // Color Data depends on modePattern
    if (modePattern == StableIsland)
    {
        // This mode is the "stable island" dynamic mode that depends on including a "statics" header to mark channels.  Bits are then arranged around this stable island within the color stream
        // the pattern looks different based on the number of static fields identified.  

        //  56 color bits
        // byte         0                        1                        2                      3                      4                       5                        6
        //     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55   non-static  req'for 3 bytes, 4 bytes,  5 bytes,  6 bytes
        // 0 statics   7th           -          5th          -           3rd         -          1st          -          2nd          -          4th          -           6th                 8           24           32         40        48
        // 1           7th           -          5th          -           3rd         - 0  1  2  3  4  5  6  *-          2nd          -          4th          -           6th                 7           17           25         33        41
        // 2           7th           -          5th          -           3rd         - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  *  *-          4th          -           6th                 6           10           18         26        34
        // 3           6th           -          4th          - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  *  *  *-          5th          -           7th                 5            3           11         19        27
        // 4           7th           -          5th          - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  *  *  *  *-           6th                 4                         4         12        20
        // 5           6th           - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  *  *  *  *  *-           7th                 3                                   5         13
        // 6           7th           - 0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  0  1  2  3  4- 5  6  *  *  *  *  *  *        2                                              6
        // 7   0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  0  1  2  3  4- 5  6  0  1  2  3  4  5- 6  *  *  *  *  *  *  *        1  
        // 8   0  1  2  3  4  5  6  0- 1  2  3  4  5  6  0  1- 2  3  4  5  6  0  1  2- 3  4  5  6  0  1  2  3- 4  5  6  0  1  2  3  4_ 5  6  0  1  2  3  4  5- 6  0  1  2  3  4  5  6        0  
        
        // ENDPOINT ORDER:   A0  A1  B0  B1  G0  G1  R0  R1 
        uint order[8] = { 6, 7, 4, 5, 2, 3, 0, 1 };
        
        // For loop that mofidies order[] 
        // for each static, it decreases every index greater than it, and then zeroes itself
        // So if static count is zero, order remains as is.
        for (uint ep = 0; ep < 8; ++ep)
        {
            if ((modeStatics | lowEntropy) & (1u << ep))
            {
                // move other endpoints up in the order, to account for the 
                // removal of this static field from the normal order
                for (uint ee = 0; ee < 8; ee++)
                {
                    if (order[ee] > order[ep]) order[ee]--;
                }
                order[ep] = 0;
            }
        }
            
        uint staticFields = countbits(modeStatics | lowEntropy);
        uint staticBits = staticFields * 7;
        uint otherFields = 8 - staticFields;
            
        uint nextStaticBit = staticBitStarts[staticFields];
        uint dwordIndex = 0;
        uint dwordPosition = 7;
            
        // Iterate through each endpoint { R0, R1, G0, G1, B0, B1, A0, A1 }
        // Check if the endpoint is (static|lowEntropy), or if it belongs to "others"
        // If its (static|lowEntropy) -> Pick the 5 bits from its location
        // If its not, pick 1 bit at a time by reverting the destRemapped 
        for (uint ep = 0; ep < 8; ep++)
        {
            // For statics, copy all 5 bits at once
            if ((modeStatics | lowEntropy) & (1u << ep))
            {
                // Find the offset into the colorstream data where the 5 bits of static endpoint data were written
                uint colorByteIndex = nextStaticBit / 8;
                uint colorBitIndex = nextStaticBit % 8;
                    
                uint intermediateEndpoint = 0u;
                    
                if (colorBitIndex > 1)
                {
                    uint lowBits = 8 - colorBitIndex;
                    uint highBits = 7 - lowBits;
                        
                    uint mask = (~0u) >> (32 - lowBits);
                    WriteBitsToDword((colorBytes[colorByteIndex] >> colorBitIndex) & mask, 0 /*lowBits bit(s)*/, intermediateEndpoint);
                        
                    mask = (~0u) >> (32 - highBits);
                    WriteBitsToDword(colorBytes[colorByteIndex + 1] & mask, lowBits /*highBits bit(s)*/, intermediateEndpoint);
                }
                else
                {
                    WriteBitsToDword((colorBytes[colorByteIndex] >> colorBitIndex) & 0x7Fu, 0 /*7 bit(s)*/, intermediateEndpoint);
                }
                    
                // Now write the 7 bits to the final position
                // Careful with the case where part goes to Raw0 and part to Raw1
                if (32 - dwordPosition < 7)
                {
                    uint bitsToRaw0 = 32 - dwordPosition;
                    uint bitsToRaw1 = 7 - bitsToRaw0;
                        
                    uint mask = (~0u) >> (32 - bitsToRaw0);
                    WriteBitsToDword(intermediateEndpoint & mask, dwordPosition /*bitsToRaw0 bit(s)*/, raw[dwordIndex]);
                        
                    dwordIndex += 1;
                    dwordPosition = 0;
                        
                    WriteBitsToDword(intermediateEndpoint >> bitsToRaw0, dwordPosition /*bitsToRaw1 bit(s)*/, raw[dwordIndex]);
                    dwordPosition += bitsToRaw1;
                }
                else
                {
                    WriteBitsToDword(intermediateEndpoint & 0x7Fu, dwordPosition /*7 bit(s)*/, raw[dwordIndex]);
                    dwordPosition += 7;
                }
                    
                nextStaticBit += 7;
            }
            // Copy 1 bit at a time, the bits are not contiguous
            else
            {
                uint intermediateEndpoint = 0u;
                uint intPosition = 0;
                for (uint b = 0; b < 7; ++b)
                {
                    const uint bi = 6 - b;
                    const uint destLinear = staticBits + order[ep] + (bi * otherFields);
                    const uint destLinearByte = destLinear / 8;
                    const uint destLinearBit = destLinear % 8;
                    const uint destRemappedByte = otherByteOrder[staticFields][destLinearByte];
                    const uint destRemapped = 8 * destRemappedByte + destLinearBit;
                        
                    // Find the offset into color where the single bit was written
                    uint colorByteIndex = destRemapped / 8;
                    uint colorBitIndex = destRemapped % 8;
                        
                    WriteBitsToDword((colorBytes[colorByteIndex] >> colorBitIndex) & 0x1u, intPosition /*1 bit(s)*/, intermediateEndpoint);
                    intPosition += 1;
                }
                    
                if (32 - dwordPosition < 7)
                {
                    uint firstHalf = 32 - dwordPosition;
                    uint secondHalf = 7 - firstHalf;
                        
                    uint mask = (~0u) >> (32 - firstHalf);;
                    WriteBitsToDword(intermediateEndpoint & mask, dwordPosition /*firstHalf bit(s)*/, raw[dwordIndex]);
                    dwordIndex++;
                    dwordPosition = 0;
                        
                    WriteBitsToDword(intermediateEndpoint >> firstHalf, dwordPosition /*secondHalf bit(s)*/, raw[dwordIndex]);
                    dwordPosition += secondHalf;
                }
                else
                {
                    WriteBitsToDword(intermediateEndpoint & 0x7Fu, dwordPosition /*7 bit(s)*/, raw[dwordIndex]);
                    dwordPosition += 7;
                }
            }
        }
    }
    else if (modePattern == EndpointPairSignificantBitInderleaved)
    {
        //  This shuffle treats 0/1 endpoints as a unit, and interleaves bits in significant bit order
        //  the most significant bits are placed at a byte boundary, and channel bits are interleaved.  Matches across modes are possible

        //  56 bits of RGBA 0\1:    bits 0 - 55
        //                  byte 0                                     byte 1                                 byte 2                                   byte 3                                  byte 4                                    byte 5                                  (Most significant)
        //  R0'0 R1'0 G0'0 G1'0 B0'0 B1'0 A0'0 A1'0  R0'1 R1'1 G0'1 G1'1 B0'1 B1'1 A0'1 A1'1   R0'2 R1'2 G0'2 G1'2 B0'2 B1'2 A0'2 A1'2   R0'3 R1'3 G0'3 G1'3 B0'3 B1'3 A0'3 A1'3   R0'4 R1'4 G0'4 G1'4 B0'4 B1'4 A0'4 A1'4  R0'5 R1'5 G0'5 G1'5 B0'5 B1'5 A0'5 A1'5  R0'6 R1'6 G0'6 G1'6 B0'6 B1'6 A0'6 A1'6
        //
        //
        
        WriteBitsToDword((colorBytes[0]) & 0x1, 7  /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorBytes[1]) & 0x1, 8  /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorBytes[2]) & 0x1, 9  /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorBytes[3]) & 0x1, 10 /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorBytes[4]) & 0x1, 11 /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorBytes[5]) & 0x1, 12 /*1 bit(s)*/, raw[0]); // R0
        WriteBitsToDword((colorBytes[6]) & 0x1, 13 /*1 bit(s)*/, raw[0]); // R0
        
        WriteBitsToDword((colorBytes[0] >> 1) & 0x1, 14 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorBytes[1] >> 1) & 0x1, 15 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorBytes[2] >> 1) & 0x1, 16 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorBytes[3] >> 1) & 0x1, 17 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorBytes[4] >> 1) & 0x1, 18 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorBytes[5] >> 1) & 0x1, 19 /*1 bit(s)*/, raw[0]); // R1
        WriteBitsToDword((colorBytes[6] >> 1) & 0x1, 20 /*1 bit(s)*/, raw[0]); // R1
        
        WriteBitsToDword((colorBytes[0] >> 2) & 0x1, 21 /*1 bit(s)*/, raw[0]); // G0
        WriteBitsToDword((colorBytes[1] >> 2) & 0x1, 22 /*1 bit(s)*/, raw[0]); // G0
        WriteBitsToDword((colorBytes[2] >> 2) & 0x1, 23 /*1 bit(s)*/, raw[0]); // G0
        WriteBitsToDword((colorBytes[3] >> 2) & 0x1, 24 /*1 bit(s)*/, raw[0]); // G0
        WriteBitsToDword((colorBytes[4] >> 2) & 0x1, 25 /*1 bit(s)*/, raw[0]); // G0
        WriteBitsToDword((colorBytes[5] >> 2) & 0x1, 26 /*1 bit(s)*/, raw[0]); // G0
        WriteBitsToDword((colorBytes[6] >> 2) & 0x1, 27 /*1 bit(s)*/, raw[0]); // G0
                                 
        WriteBitsToDword((colorBytes[0] >> 3) & 0x1, 28 /*1 bit(s)*/, raw[0]); // G1
        WriteBitsToDword((colorBytes[1] >> 3) & 0x1, 29 /*1 bit(s)*/, raw[0]); // G1
        WriteBitsToDword((colorBytes[2] >> 3) & 0x1, 30 /*1 bit(s)*/, raw[0]); // G1
        WriteBitsToDword((colorBytes[3] >> 3) & 0x1, 31 /*1 bit(s)*/, raw[0]); // G1
        WriteBitsToDword((colorBytes[4] >> 3) & 0x1, 0  /*1 bit(s)*/, raw[1]); // G1
        WriteBitsToDword((colorBytes[5] >> 3) & 0x1, 1  /*1 bit(s)*/, raw[1]); // G1
        WriteBitsToDword((colorBytes[6] >> 3) & 0x1, 2  /*1 bit(s)*/, raw[1]); // G1
                                  
        WriteBitsToDword((colorBytes[0] >> 4) & 0x1, 3 /*1 bit(s)*/, raw[1]); // B0
        WriteBitsToDword((colorBytes[1] >> 4) & 0x1, 4 /*1 bit(s)*/, raw[1]); // B0
        WriteBitsToDword((colorBytes[2] >> 4) & 0x1, 5 /*1 bit(s)*/, raw[1]); // B0
        WriteBitsToDword((colorBytes[3] >> 4) & 0x1, 6 /*1 bit(s)*/, raw[1]); // B0
        WriteBitsToDword((colorBytes[4] >> 4) & 0x1, 7 /*1 bit(s)*/, raw[1]); // B0
        WriteBitsToDword((colorBytes[5] >> 4) & 0x1, 8 /*1 bit(s)*/, raw[1]); // B0
        WriteBitsToDword((colorBytes[6] >> 4) & 0x1, 9 /*1 bit(s)*/, raw[1]); // B0
                                   
        WriteBitsToDword((colorBytes[0] >> 5) & 0x1, 10 /*1 bit(s)*/, raw[1]); // B1
        WriteBitsToDword((colorBytes[1] >> 5) & 0x1, 11 /*1 bit(s)*/, raw[1]); // B1
        WriteBitsToDword((colorBytes[2] >> 5) & 0x1, 12 /*1 bit(s)*/, raw[1]); // B1
        WriteBitsToDword((colorBytes[3] >> 5) & 0x1, 13 /*1 bit(s)*/, raw[1]); // B1
        WriteBitsToDword((colorBytes[4] >> 5) & 0x1, 14 /*1 bit(s)*/, raw[1]); // B1
        WriteBitsToDword((colorBytes[5] >> 5) & 0x1, 15 /*1 bit(s)*/, raw[1]); // B1
        WriteBitsToDword((colorBytes[6] >> 5) & 0x1, 16 /*1 bit(s)*/, raw[1]); // B1
                               
        WriteBitsToDword((colorBytes[0] >> 6) & 0x1, 17 /*1 bit(s)*/, raw[1]); // A0
        WriteBitsToDword((colorBytes[1] >> 6) & 0x1, 18 /*1 bit(s)*/, raw[1]); // A0
        WriteBitsToDword((colorBytes[2] >> 6) & 0x1, 19 /*1 bit(s)*/, raw[1]); // A0
        WriteBitsToDword((colorBytes[3] >> 6) & 0x1, 20 /*1 bit(s)*/, raw[1]); // A0
        WriteBitsToDword((colorBytes[4] >> 6) & 0x1, 21 /*1 bit(s)*/, raw[1]); // A0
        WriteBitsToDword((colorBytes[5] >> 6) & 0x1, 22 /*1 bit(s)*/, raw[1]); // A0
        WriteBitsToDword((colorBytes[6] >> 6) & 0x1, 23 /*1 bit(s)*/, raw[1]); // A0      
        
        WriteBitsToDword((colorBytes[0] >> 7) & 0x1, 24 /*1 bit(s)*/, raw[1]); // A1
        WriteBitsToDword((colorBytes[1] >> 7) & 0x1, 25 /*1 bit(s)*/, raw[1]); // A1
        WriteBitsToDword((colorBytes[2] >> 7) & 0x1, 26 /*1 bit(s)*/, raw[1]); // A1
        WriteBitsToDword((colorBytes[3] >> 7) & 0x1, 27 /*1 bit(s)*/, raw[1]); // A1
        WriteBitsToDword((colorBytes[4] >> 7) & 0x1, 28 /*1 bit(s)*/, raw[1]); // A1
        WriteBitsToDword((colorBytes[5] >> 7) & 0x1, 29 /*1 bit(s)*/, raw[1]); // A1
        WriteBitsToDword((colorBytes[6] >> 7) & 0x1, 30 /*1 bit(s)*/, raw[1]); // A1
    }
    
    // P0
    WriteBitsToDword(miscByte7 >> 7, 31 /*1 bit(s)*/, raw[1]);
        
    // P1
    WriteBitsToDword(ScrapData, 0 /*1 bit(s)*/, raw[2]);
        
    // Index
    WriteBitsToDword(miscByte0,         1  /*8 bit(s)*/, raw[2]);
    WriteBitsToDword(miscByte1,         9  /*8 bit(s)*/, raw[2]);
    WriteBitsToDword(miscByte2,         17 /*8 bit(s)*/, raw[2]);
    WriteBitsToDword(miscByte3 & 0x7F,  25 /*7 bit(s)*/, raw[2]);
    WriteBitsToDword(miscByte3 >> 7,    0  /*1 bit(s)*/, raw[3]);
    WriteBitsToDword(miscByte4,         1  /*8 bit(s)*/, raw[3]);
    WriteBitsToDword(miscByte5,         9  /*8 bit(s)*/, raw[3]);
    WriteBitsToDword(miscByte6,         17 /*8 bit(s)*/, raw[3]);
    WriteBitsToDword(miscByte7 & 0x7F,  25 /*7 bit(s)*/, raw[3]);
}


void unshuffleMode7(uint colorAddress, uint miscAddress, uint ScrapData, uint modePattern, in ByteAddressBuffer buffer, inout uint4 raw)
{
    // Mode 7
    // 8 bit mode                                       =>  8 bits
    // 6 bit partition                                  =>  6 bits
    // 5 bits R0 - 5 bits R1 - 5 bits R2 - 5 bits R3    => 20 bits
    // 5 bits B0 - 5 bits G1 - 5 bits R2 - 5 bits G3    => 20 bits
    // 5 bits G0 - 5 bits B1 - 5 bits R2 - 5 bits B3    => 20 bits
    // 5 bits A0 - 5 bits A1 - 5 bits A2 - 5 bits A3    => 20 bits
    // 1 bit  P0 - 1 bit  P1 - 1 bit  P2 - 1 bit  P3    =>  4 bits
    // 30 bits index                                    => 30 bits
    
    // 11/ 10/ 11 bytes color ?
    // 4 / 5 / 4  bytes misc
    // 0 bits scrap
    
    // Color Data
    uint colorBytes[11];
    colorBytes[0] = fetchNextByte(colorAddress, buffer);
    colorBytes[1] = fetchNextByte(colorAddress + 1, buffer);
    colorBytes[2] = fetchNextByte(colorAddress + 2, buffer);
    colorBytes[3] = fetchNextByte(colorAddress + 3, buffer);
    colorBytes[4] = fetchNextByte(colorAddress + 4, buffer);
    colorBytes[5] = fetchNextByte(colorAddress + 5, buffer);
    colorBytes[6] = fetchNextByte(colorAddress + 6, buffer);
    colorBytes[7] = fetchNextByte(colorAddress + 7, buffer);
    colorBytes[8] = fetchNextByte(colorAddress + 8, buffer);
    colorBytes[9] = fetchNextByte(colorAddress + 9, buffer);
    colorBytes[10] = fetchNextByte(colorAddress + 10, buffer);
    
    // Misc Data (5 bytes)
    uint miscByte0 = fetchNextByte(miscAddress, buffer);
    uint miscByte1 = fetchNextByte(miscAddress + 1, buffer);
    uint miscByte2 = fetchNextByte(miscAddress + 2, buffer);
    uint miscByte3 = fetchNextByte(miscAddress + 3, buffer);
    uint miscByte4 = fetchNextByte(miscAddress + 4, buffer);
    
    // Write the mode
    WriteBitsToDword(128u, 0 /*8 bit(s)*/, raw[0]);
    
    if (modePattern == EndpointQuadSignificantBitInderleaved)
    {
        //  This swizzle treats 0/1 endpoints as a group and 2/3 endpoints as a different group that vary independenty.
        //  the most significant bits are grouped in the middle.  Other possibly stable data is used to pad the endpoints pair
        //  data, to push the edge to mid-byte.

        //  40 bits of RGBA 0\1:    bits 4 - 43, with a 4 bit leading pad
        //                                                                                                                                                                                   Most significant 5
        //  [ 4 partition bits] R0'0 R1'0 G0'0 G1'0 A0'0 A1'0 B0'0 B1'0   R0'1 R1'1 G0'1 G1'1 A0'1 A1'1 B0'1 B1'1   R0'2 R1'2 G0'2 G1'2 A0'2 A1'2 B0'2 B1'2   R0'3 R1'3 G0'3 G1'3 A0'3 A1'3 B0'3 B1'3   R0'4 R1'4 G0'4 G1'4 A0'4 A1'4 B0'4 B1'4    
        //
        //  followed by reversed 40 bits of RGBA 2\3:    bits 44 - 83, padded with 4 bits 
        //    
        //       Most significant 5                                    4                                         3
        //  B2'4 B3'4 A0'4 A1'4 G2'4 G3'4 R2'4 R3'4   B2'3 B3'3 A0'3 A1'3 G2'3 G3'3 R2'3 R3'3   B2'2 B3'2 A0'2 A1'2 G2'2 G3'2 R2'2 R3'2   B2'1 B3'1 A0'1 A1'1 G2'1 G3'1 R2'1 R3'1   B2'0 B3'0 A0'0 A1'0 G2'0 G3'0 R2'0 R3'0   [2 partition, 2 P bits]
    
        // partition bits 0-3 from color
        WriteBitsToDword(colorBytes[0] & 0xF, 8 /*4 bit(s)*/, raw[0]);
    
        // partition bits 4-5 from color // colorBytes[10]: __** ____ 
        WriteBitsToDword((colorBytes[10] >> 4) & 0x3, 12 /*2 bit(s)*/, raw[0]);
    
        // Color data
        const uint kChannels = 8;
        uint dwordIndex = 0;
        uint dwordPosition = 14;
    
        // R0, R1
        uint base = 4;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // R2, R3 
        base = 82;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            if (dwordPosition == 32)
            {
                dwordPosition = 0;
                dwordIndex++;
            }
        }
    
        // G0, G1 
        base = 6;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // G2, G3 
        base = 80;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // B0, B1 
        base = 8;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            if (dwordPosition == 32)
            {
                dwordPosition = 0;
                dwordIndex++;
            }
        }
    
        // B2, B3 
        base = 78;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // A0, A1 
        base = 10;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // A2, A3 
        base = 76;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // 2 PBits
        WriteBitsToDword(colorBytes[10] >> 6, 30 /*2 bit(s)*/, raw[2]);
        
        // Remaining 2 pbits + 30bits index data
        WriteBitsToDword(miscByte0, 0  /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte1, 8  /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte2, 16 /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte3, 24 /*8 bit(s)*/, raw[3]);
    }
    else if (modePattern == EndpointQuadSignificantBitInderleavedAlt) // matches experimental ID 4
    {
        //  This swizzle treats 0/1 endpoints as a group and 2/3 endpoints as a different group that vary independenty.
        //  the most significant bits are grouped in the middle.  Alt implies byte aligned endpoint pair edge.

        //  40 bits of RGBA 0\1:    bits 0 - 39
        //                                                                                                                3                               4                                    Most significant 5
        //  R0'0 R1'0 G0'0 G1'0 A0'0 A1'0 B0'0 B1'0   R0'1 R1'1 G0'1 G1'1 A0'1 A1'1 B0'1 B1'1   R0'2 R1'2 G0'2 G1'2 A0'2 A1'2 B0'2 B1'2   R0'3 R1'3 G0'3 G1'3 A0'3 A1'3 B0'3 B1'3   R0'4 R1'4 G0'4 G1'4 A0'4 A1'4 B0'4 B1'4    
        //
        //  followed by reversed 40 bits of RGBA 2\3:    bits 40 - 79 
        //    
        //       Most significant 5                                    4                                         3
        //  B2'4 B3'4 A0'4 A1'4 G2'4 G3'4 R2'4 R3'4   B2'3 B3'3 A0'3 A1'3 G2'3 G3'3 R2'3 R3'3   B2'2 B3'2 A0'2 A1'2 G2'2 G3'2 R2'2 R3'2   B2'1 B3'1 A0'1 A1'1 G2'1 G3'1 R2'1 R3'1   B2'0 B3'0 A0'0 A1'0 G2'0 G3'0 R2'0 R3'0   
    
        // 6 partition bits from misc
        WriteBitsToDword(miscByte0 & 0x3F, 8 /*6 bit(s)*/, raw[0]);
    
        // Color data
        const uint kChannels = 8;
        uint dwordIndex = 0;
        uint dwordPosition = 14;
    
        // R0, R1
        uint base = 0;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // R2, R3 
        base = 78;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            if (dwordPosition == 32)
            {
                dwordPosition = 0;
                dwordIndex++;
            }
        }
    
        // G0, G1 
        base = 2;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // G2, G3 
        base = 76;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // B0, B1 
        base = 4;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            if (dwordPosition == 32)
            {
                dwordPosition = 0;
                dwordIndex++;
            }
        }
    
        // B2, B3 
        base = 74;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // A0, A1 
        base = 6;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // A2, A3 
        base = 72;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) - (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // 4 PBits + 30 bits index data from misc
        WriteBitsToDword(miscByte0 >> 6,    30 /*2 bit(s)*/, raw[2]);
        WriteBitsToDword(miscByte1,         0  /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte2,         8  /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte3,         16 /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte4,         24 /*8 bit(s)*/, raw[3]);
    }
    else if (modePattern == EndpointPairSignificantBitInderleaved)
    {
        //  This swizzle treats 0/1 endpoints as a group and 2/3 endpoints as a different group that vary independenty.
        //  Each pair is encoded independently.  Since Partition is sometimes low entropy, that is included in Color.

        //  40 bits of RGBA 0\1:    bits 0 - 39
        //                                                                                                                                                                                   Most significant 5
        //  R0'0 R1'0 G0'0 G1'0 A0'0 A1'0 B0'0 B1'0   R0'1 R1'1 G0'1 G1'1 A0'1 A1'1 B0'1 B1'1   R0'2 R1'2 G0'2 G1'2 A0'2 A1'2 B0'2 B1'2   R0'3 R1'3 G0'3 G1'3 A0'3 A1'3 B0'3 B1'3   R0'4 R1'4 G0'4 G1'4 A0'4 A1'4 B0'4 B1'4    
        //
        //  Followed by 40 bits of RGBA 2\3:    bits 40 - 79
        //                                                                                                                                                                                   Most significant 5
        //  R2'0 R3'0 G2'0 G3'0 A2'0 A3'0 B2'0 B3'0   R2'1 R3'1 G2'1 G3'1 A2'1 A3'1 B2'1 B3'1   R2'2 R3'2 G2'2 G3'2 A2'2 A3'2 B2'2 B3'2   R2'3 R3'3 G2'3 G3'3 A2'3 A3'3 B2'3 B3'3   R2'4 R3'4 G2'4 G3'4 A2'4 A3'4 B2'4 B3'4    
        //
        // Followed by partition and 2 pbits  (experiments with actual channel order still in process)
    
        // 6 partition bits from color
        WriteBitsToDword(colorBytes[10] & 0x3F, 8 /*6 bit(s)*/, raw[0]);
    
        // Color data
        const uint kChannels = 8;
        uint dwordIndex = 0;
        uint dwordPosition = 14;
    
        // R0, R1
        uint base = 0;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // R2, R3 
        base = 40;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            if (dwordPosition == 32)
            {
                dwordPosition = 0;
                dwordIndex++;
            }
        }
    
        // G0, G1 
        base = 2;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // G2, G3 
        base = 42;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // B0, B1 
        base = 4;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
            if (dwordPosition == 32)
            {
                dwordPosition = 0;
                dwordIndex++;
            }
        }
    
        // B2, B3 
        base = 44;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // A0, A1 
        base = 6;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // A2, A3 
        base = 46;
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
        for (uint j = 0; j < 5; ++j) // Each Component has 7 bits
        {
            uint colorTotalIndex = (base + 1) + (kChannels * j);
            WriteBitsToDword((colorBytes[colorTotalIndex / 8] >> (colorTotalIndex % 8)) & 0x1, dwordPosition++ /*1 bit(s)*/, raw[dwordIndex]);
        }
    
        // PBits 0-1
        WriteBitsToDword(colorBytes[10] >> 6, 30 /*2 bit(s)*/, raw[2]); // **__ ____ 
    
        // 2 bits PBits + 30 bits index data
        WriteBitsToDword(miscByte0, 0  /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte1, 8  /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte2, 16 /*8 bit(s)*/, raw[3]);
        WriteBitsToDword(miscByte3, 24 /*8 bit(s)*/, raw[3]);
    }
}


void unshuffleMode8(inout uint4 raw)
{
    // Mode 8
    // Mode 8 (the least significant byte is set to 0x00) is reserved. Don't use it in your encoder. 
    // If you pass this mode to the hardware, a block initialized to all zeroes is returned.
    
    // 0 bytes color
    // 15 bytes misc
    // 0 bits scrap
    
    // mode 8 is sometimes used to mark invalid texture regions within layouts that are not linear packed mips
    
    raw = uint4(0u, 0u, 0u, 0u);
}
