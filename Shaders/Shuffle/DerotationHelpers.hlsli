//--------------------------------------------------------------------------------------
// DerotationHelpers.hlsli
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

void derotateMode0(uint modeRotationByteAddress, uint chunkIndex, in ByteAddressBuffer buffer, inout uint4 rawOut)
{
    // Chunk index lets us know how deep into the mode rotation we need to go to fetch the rotation data for this block
    uint address = modeRotationByteAddress + 1 + (chunkIndex * 6);
    
    uint rotations[6]; 
    rotations[0] = fetchNextByte(address, buffer);
    rotations[1] = fetchNextByte(address + 1, buffer);
    rotations[2] = fetchNextByte(address + 2, buffer);
    rotations[3] = fetchNextByte(address + 3, buffer);
    rotations[4] = fetchNextByte(address + 4, buffer);
    rotations[5] = fetchNextByte(address + 5, buffer);

    // Mode 0
    // 1 bit mode                                                               =>  1 bit
    // 4 bit partition                                                          =>  4 bits
    // 4 bits R0 - 4 bits R1 - 4 bits R2 - 4 bits R3 - 4 bits R4 - 4 bits R5    => 24 bits
    // 4 bits G0 - 4 bits G1 - 4 bits G2 - 4 bits G3 - 4 bits G4 - 4 bits G5    => 24 bits
    // 4 bits B0 - 4 bits B1 - 4 bits B2 - 4 bits B3 - 4 bits B4 - 4 bits B5    => 24 bits
    // 1 bit  P0 - 1 bit  P1 - 1 bit  P2 - 1 bit  P3 - 1 bit  P4 - 1 bit  P5    =>  6 bits 
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    uint raw2 = 0u;
    
    // Write the first bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0x1Fu, 0 /*5 bit(s)*/, raw0);
    
    // De-Rotate
    // R0-1-2-3-4-5
    uint R0 = (rawOut[0] >> 5) & 0xFu;
    R0 = (R0 - (rotations[0])) % 16;
    uint R1 = (rawOut[0] >> 9) & 0xFu;
    R1 = (R1 - (rotations[1])) % 16;
    uint R2 = (rawOut[0] >> 13) & 0xFu;
    R2 = (R2 - (rotations[0])) % 16;
    uint R3 = (rawOut[0] >> 17) & 0xFu;
    R3 = (R3 - (rotations[1])) % 16;
    uint R4 = (rawOut[0] >> 21) & 0xFu;
    R4 = (R4 - (rotations[0])) % 16;
    uint R5 = (rawOut[0] >> 25) & 0xFu;
    R5 = (R5 - (rotations[1])) % 16;
    
    WriteBitsToDword(R0, 5  /*4 bit(s)*/, raw0);
    WriteBitsToDword(R1, 9  /*4 bit(s)*/, raw0);
    WriteBitsToDword(R2, 13 /*4 bit(s)*/, raw0);
    WriteBitsToDword(R3, 17 /*4 bit(s)*/, raw0);
    WriteBitsToDword(R4, 21 /*4 bit(s)*/, raw0);
    WriteBitsToDword(R5, 25 /*4 bit(s)*/, raw0);
    
    // G0-1-2-3-4-5
    uint G0 = ((rawOut[1] & 0x1u) << 3) | (rawOut[0] >> 29);
    G0 = (G0 - (rotations[2])) % 16;
    uint G1 = (rawOut[1] >> 1) & 0xFu;
    G1 = (G1 - (rotations[3])) % 16;
    uint G2 = (rawOut[1] >> 5) & 0xFu;
    G2 = (G2 - (rotations[2])) % 16;
    uint G3 = (rawOut[1] >> 9) & 0xFu;
    G3 = (G3 - (rotations[3])) % 16;
    uint G4 = (rawOut[1] >> 13) & 0xFu;
    G4 = (G4 - (rotations[2])) % 16;
    uint G5 = (rawOut[1] >> 17) & 0xFu;
    G5 = (G5 - (rotations[3])) % 16;
    
    WriteBitsToDword((G0 & 0x7u),   29 /*3 bit(s)*/, raw0);
    WriteBitsToDword((G0 >> 3),     0  /*1 bit(s)*/, raw1);
    WriteBitsToDword(G1,            1  /*4 bit(s)*/, raw1);
    WriteBitsToDword(G2,            5  /*4 bit(s)*/, raw1);
    WriteBitsToDword(G3,            9  /*4 bit(s)*/, raw1);
    WriteBitsToDword(G4,            13 /*4 bit(s)*/, raw1);
    WriteBitsToDword(G5,            17 /*4 bit(s)*/, raw1);
    
    // B0-1-2-3-4-5
    uint B0 = (rawOut[1] >> 21) & 0xFu;
    B0 = (B0 - (rotations[4])) % 16;
    uint B1 = (rawOut[1] >> 25) & 0xFu;
    B1 = (B1 - (rotations[5])) % 16;
    uint B2 = ((rawOut[2] & 0x1u) << 3) | (rawOut[1] >> 29);
    B2 = (B2 - (rotations[4])) % 16;
    uint B3 = (rawOut[2] >> 1) & 0xFu;
    B3 = (B3 - (rotations[5])) % 16;
    uint B4 = (rawOut[2] >> 5) & 0xFu;
    B4 = (B4 - (rotations[4])) % 16;
    uint B5 = (rawOut[2] >> 9) & 0xFu;
    B5 = (B5 - (rotations[5])) % 16;
    
    WriteBitsToDword(B0,            21 /*4 bit(s)*/, raw1);
    WriteBitsToDword(B1,            25 /*4 bit(s)*/, raw1);
    WriteBitsToDword((B2 & 0x7u),   29 /*3 bit(s)*/, raw1);
    WriteBitsToDword((B2 >> 3),     0  /*1 bit(s)*/, raw2);
    WriteBitsToDword(B3,            1  /*4 bit(s)*/, raw2);
    WriteBitsToDword(B4,            5  /*4 bit(s)*/, raw2);
    WriteBitsToDword(B5,            9  /*4 bit(s)*/, raw2);

    // Copy the last bits (after color) of rawOut[2] to raw2
    WriteBitsToDword((rawOut[2] >> 13) & 0x7u,  13 /*3 bit(s)*/, raw2);
    WriteBitsToDword(rawOut[2] >> 16,           16 /*8 bit(s)*/, raw2);
    WriteBitsToDword(rawOut[2] >> 24,           24 /*8 bit(s)*/, raw2);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
    rawOut[2] = raw2;
}

void derotateMode1(uint modeRotationByteAddress, uint chunkIndex, in ByteAddressBuffer buffer, inout uint4 rawOut)
{
    // Chunk index lets us know how deep into the mode rotation we need to go to fetch the rotation data for this block
    uint address = modeRotationByteAddress + 1 + (chunkIndex * 6);
    
    uint rotations[6];
    rotations[0] = fetchNextByte(address, buffer);
    rotations[1] = fetchNextByte(address + 1, buffer);
    rotations[2] = fetchNextByte(address + 2, buffer);
    rotations[3] = fetchNextByte(address + 3, buffer);
    rotations[4] = fetchNextByte(address + 4, buffer);
    rotations[5] = fetchNextByte(address + 5, buffer);

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
    R0 = (R0 - (rotations[0])) % 64;
    uint R1 = (rawOut[0] >> 14) & 0x3Fu;
    R1 = (R1 - (rotations[1])) % 64;
    uint R2 = (rawOut[0] >> 20) & 0x3Fu;
    R2 = (R2 - (rotations[0])) % 64;
    uint R3 = (rawOut[0] >> 26) & 0x3Fu;
    R3 = (R3 - (rotations[1])) % 64;
    
    WriteBitsToDword(R0, 8  /*6 bit(s)*/, raw0);
    WriteBitsToDword(R1, 14 /*6 bit(s)*/, raw0);
    WriteBitsToDword(R2, 20 /*6 bit(s)*/, raw0);
    WriteBitsToDword(R3, 26 /*6 bit(s)*/, raw0);
    
    // G0-1-2-3-4-5
    uint G0 = rawOut[1] & 0x3Fu;
    G0 = (G0 - (rotations[2])) % 64;
    uint G1 = (rawOut[1] >> 6) & 0x3Fu;
    G1 = (G1 - (rotations[3])) % 64;
    uint G2 = (rawOut[1] >> 12) & 0x3Fu;
    G2 = (G2 - (rotations[2])) % 64;
    uint G3 = (rawOut[1] >> 18) & 0x3Fu;
    G3 = (G3 - (rotations[3])) % 64;
    
    WriteBitsToDword(G0, 0  /*6 bit(s)*/, raw1);
    WriteBitsToDword(G1, 6  /*6 bit(s)*/, raw1);
    WriteBitsToDword(G2, 12 /*6 bit(s)*/, raw1);
    WriteBitsToDword(G3, 18 /*6 bit(s)*/, raw1);
    
    // B0-1-2-3-4-5
    uint B0 = (rawOut[1] >> 24) & 0x3Fu;
    B0 = (B0 - (rotations[4])) % 64;
    uint B1 = ((rawOut[2] & 0xFu) << 2) | (rawOut[1] >> 30);
    B1 = (B1 - (rotations[5])) % 64;
    uint B2 = (rawOut[2] >> 4) & 0x3Fu;
    B2 = (B2 - (rotations[4])) % 64;
    uint B3 = (rawOut[2] >> 10) & 0x3Fu;
    B3 = (B3 - (rotations[5])) % 64;
    
    WriteBitsToDword(B0,        24 /*6 bit(s)*/, raw1);
    WriteBitsToDword(B1 & 0x3u, 30 /*2 bit(s)*/, raw1);
    WriteBitsToDword(B1 >> 2,   0  /*4 bit(s)*/, raw2);
    WriteBitsToDword(B2,        4  /*6 bit(s)*/, raw2);
    WriteBitsToDword(B3,        10 /*6 bit(s)*/, raw2);

    // Copy the last bits (after color) of rawOut[2] to raw2
    WriteBitsToDword(rawOut[2] >> 16, 16 /*8 bit(s)*/, raw2);
    WriteBitsToDword(rawOut[2] >> 24, 24 /*8 bit(s)*/, raw2);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
    rawOut[2] = raw2;
}

void derotateMode2(uint modeRotationByteAddress, uint chunkIndex, in ByteAddressBuffer buffer, inout uint4 rawOut)
{
    // Chunk index lets us know how deep into the mode rotation we need to go to fetch the rotation data for this block
    uint address = modeRotationByteAddress + 1 + (chunkIndex * 6);
    
    uint rotations[6];
    rotations[0] = fetchNextByte(address, buffer);
    rotations[1] = fetchNextByte(address + 1, buffer);
    rotations[2] = fetchNextByte(address + 2, buffer);
    rotations[3] = fetchNextByte(address + 3, buffer);
    rotations[4] = fetchNextByte(address + 4, buffer);
    rotations[5] = fetchNextByte(address + 5, buffer);

    // Mode 2
    // 3 bit mode                                                               =>  3 bits
    // 6 bit partition                                                          =>  6 bits
    // 5 bits R0 - 5 bits R1 - 5 bits R2 - 5 bits R3 - 5 bits R4 - 5 bits R5    => 30 bits
    // 5 bits B0 - 5 bits G1 - 5 bits R2 - 5 bits G3 - 5 bits G4 - 5 bits G5    => 30 bits
    // 5 bits G0 - 5 bits B1 - 5 bits R2 - 5 bits B3 - 5 bits B4 - 5 bits B5    => 30 bits
    // 29 bits index                                                            => 29 bits
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    uint raw2 = 0u;
    uint raw3 = 0u;
    
    // Write the first bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0xFFu,         0 /*8 bit(s)*/, raw0);
    WriteBitsToDword((rawOut[0] >> 8) & 0x1u,   8 /*1 bit(s)*/, raw0);
    
    // De-Rotate
    // R0-1-2-3-4-5
    uint R0 = (rawOut[0] >> 9) & 0x1Fu;
    R0 = (R0 - (rotations[0])) % 32;
    uint R1 = (rawOut[0] >> 14) & 0x1Fu;
    R1 = (R1 - (rotations[1])) % 32;
    uint R2 = (rawOut[0] >> 19) & 0x1Fu;
    R2 = (R2 - (rotations[0])) % 32;
    uint R3 = (rawOut[0] >> 24) & 0x1Fu;
    R3 = (R3 - (rotations[1])) % 32;
    uint R4 = ((rawOut[1] & 0x3f) << 3) | (rawOut[0] >> 29);
    R4 = (R4 - (rotations[0])) % 32;
    uint R5 = (rawOut[1] >> 2) & 0x1Fu;
    R5 = (R5 - (rotations[1])) % 32;
    
    WriteBitsToDword(R0,        9  /*5 bit(s)*/, raw0);
    WriteBitsToDword(R1,        14 /*5 bit(s)*/, raw0);
    WriteBitsToDword(R2,        19 /*5 bit(s)*/, raw0);
    WriteBitsToDword(R3,        24 /*5 bit(s)*/, raw0);
    WriteBitsToDword(R4 & 0x7u, 29 /*3 bit(s)*/, raw0);
    WriteBitsToDword(R4 >> 3,   0  /*2 bit(s)*/, raw1);
    WriteBitsToDword(R5,        2  /*5 bit(s)*/, raw1);
    
    // G0-1-2-3-4-5
    uint G0 = (rawOut[1] >> 7) & 0x1Fu;
    G0 = (G0 - (rotations[2])) % 32;
    uint G1 = (rawOut[1] >> 12) & 0x1Fu;
    G1 = (G1 - (rotations[3])) % 32;
    uint G2 = (rawOut[1] >> 17) & 0x1Fu;
    G2 = (G2 - (rotations[2])) % 32;
    uint G3 = (rawOut[1] >> 22) & 0x1Fu;
    G3 = (G3 - (rotations[3])) % 32;
    uint G4 = (rawOut[1] >> 27) & 0x1Fu;
    G4 = (G4 - (rotations[2])) % 32;
    uint G5 = rawOut[2] & 0x1Fu;
    G5 = (G5 - (rotations[3])) % 32;
    
    WriteBitsToDword(G0, 7  /*5 bit(s)*/, raw1);
    WriteBitsToDword(G1, 12 /*5 bit(s)*/, raw1);
    WriteBitsToDword(G2, 17 /*5 bit(s)*/, raw1);
    WriteBitsToDword(G3, 22 /*5 bit(s)*/, raw1);
    WriteBitsToDword(G4, 27 /*5 bit(s)*/, raw1);
    WriteBitsToDword(G5, 0  /*5 bit(s)*/, raw2);
    
    // B0-1-2-3-4-5
    uint B0 = (rawOut[2] >> 5) & 0x1Fu;
    B0 = (B0 - (rotations[4])) % 32;
    uint B1 = (rawOut[2] >> 10) & 0x1Fu;
    B1 = (B1 - (rotations[5])) % 32;
    uint B2 = (rawOut[2] >> 15) & 0x1Fu;
    B2 = (B2 - (rotations[4])) % 32;
    uint B3 = (rawOut[2] >> 20) & 0x1Fu;
    B3 = (B3 - (rotations[5])) % 32;
    uint B4 = (rawOut[2] >> 25) & 0x1Fu;
    B4 = (B4 - (rotations[4])) % 32;
    uint B5 = ((rawOut[3] & 0x7u) << 2) | (rawOut[2] >> 30);
    B5 = (B5 - (rotations[5])) % 32;

    WriteBitsToDword(B0,        5  /*5 bit(s)*/, raw2);
    WriteBitsToDword(B1,        10 /*5 bit(s)*/, raw2);
    WriteBitsToDword(B2,        15 /*5 bit(s)*/, raw2);
    WriteBitsToDword(B3,        20 /*5 bit(s)*/, raw2);
    WriteBitsToDword(B4,        25 /*5 bit(s)*/, raw2);
    WriteBitsToDword(B5 & 0x3u, 30 /*2 bit(s)*/, raw2);
    WriteBitsToDword(B5 >> 2,   0  /*3 bit(s)*/, raw3);

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

void derotateMode3(uint modeRotationByteAddress, uint chunkIndex, in ByteAddressBuffer buffer, inout uint4 rawOut)
{   
    // Chunk index lets us know how deep into the mode rotation we need to go to fetch the rotation data for this block
    uint address = modeRotationByteAddress + 1 + (chunkIndex * 6);
    
    uint rotations[6];
    rotations[0] = fetchNextByte(address, buffer);
    rotations[1] = fetchNextByte(address + 1, buffer);
    rotations[2] = fetchNextByte(address + 2, buffer);
    rotations[3] = fetchNextByte(address + 3, buffer);
    rotations[4] = fetchNextByte(address + 4, buffer);
    rotations[5] = fetchNextByte(address + 5, buffer);
        
    // Mode 3
    // 4 bit mode                                       =>  4 bits
    // 6 bit partition                                  =>  6 bits
    // 7 bits R0 - 7 bits R1 - 7 bits R2 - 7 bits R3    => 28 bits
    // 7 bits B0 - 7 bits G1 - 7 bits R2 - 7 bits G3    => 28 bits
    // 7 bits G0 - 7 bits B1 - 7 bits R2 - 7 bits B3    => 28 bits
    // 1 bit  P0 - 1 bit  P1 - 1 bit  P2 - 1 bit  P3    =>  4 bits
    // 30 bits index                                    => 30 bits
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    uint raw2 = 0u;
    
    // Write the first bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0xFFu,         0 /*8 bit(s)*/, raw0);
    WriteBitsToDword((rawOut[0] >> 8) & 0x3u,   8 /*2 bit(s)*/, raw0);
    
    // De-Rotate
    // R0-1-2-3-4-5
    uint R0 = (rawOut[0] >> 10) & 0x7Fu;
    R0 = (R0 - (rotations[0])) % 128;
    uint R1 = (rawOut[0] >> 17) & 0x7Fu;
    R1 = (R1 - (rotations[1])) % 128;
    uint R2 = (rawOut[0] >> 24) & 0x7Fu;
    R2 = (R2 - (rotations[0])) % 128;
    uint R3 = ((rawOut[1] & 0x3Fu) << 1) | (rawOut[0] >> 31);
    R3 = (R3 - (rotations[1])) % 128;
    
    WriteBitsToDword(R0,        10 /*7 bit(s)*/, raw0);
    WriteBitsToDword(R1,        17 /*7 bit(s)*/, raw0);
    WriteBitsToDword(R2,        24 /*7 bit(s)*/, raw0);
    WriteBitsToDword(R3 & 0x1u, 31 /*1 bit(s)*/, raw0);
    WriteBitsToDword(R3 >> 1,   0  /*6 bit(s)*/, raw1);
    
    // G0-1-2-3-4-5
    uint G0 = (rawOut[1] >> 6) & 0x7Fu;
    G0 = (G0 - (rotations[2])) % 128;
    uint G1 = (rawOut[1] >> 13) & 0x7Fu;
    G1 = (G1 - (rotations[3])) % 128;
    uint G2 = (rawOut[1] >> 20) & 0x7Fu;
    G2 = (G2 - (rotations[2])) % 128;
    uint G3 = ((rawOut[2] & 0x3u) << 5) | (rawOut[1] >> 27);
    G3 = (G3 - (rotations[3])) % 128;
    
    WriteBitsToDword(G0,            6  /*7 bit(s)*/, raw1);
    WriteBitsToDword(G1,            13 /*7 bit(s)*/, raw1);
    WriteBitsToDword(G2,            20 /*7 bit(s)*/, raw1);
    WriteBitsToDword(G3 & 0x1Fu,    27 /*5 bit(s)*/, raw1);
    WriteBitsToDword(G3 >> 5,       0  /*2 bit(s)*/, raw2);
    
    // B0-1-2-3-4-5
    uint B0 = (rawOut[2] >> 2) & 0x7Fu;
    B0 = (B0 - (rotations[4])) % 128;
    uint B1 = (rawOut[2] >> 9) & 0x7Fu;
    B1 = (B1 - (rotations[5])) % 128;
    uint B2 = (rawOut[2] >> 16) & 0x7Fu;
    B2 = (B2 - (rotations[4])) % 128;
    uint B3 = (rawOut[2] >> 23) & 0x7Fu;
    B3 = (B3 - (rotations[5])) % 128;
    
    WriteBitsToDword(B0, 2  /*7 bit(s)*/, raw2);
    WriteBitsToDword(B1, 9  /*7 bit(s)*/, raw2);
    WriteBitsToDword(B2, 16 /*7 bit(s)*/, raw2);
    WriteBitsToDword(B3, 23 /*7 bit(s)*/, raw2);

    // Copy the last bits (after color) of rawOut[2] to raw2
    WriteBitsToDword(rawOut[2] >> 30, 30 /*2 bit(s)*/, raw2);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
    rawOut[2] = raw2;
}

void derotateMode4(uint modeRotationByteAddress, uint chunkIndex, in ByteAddressBuffer buffer, inout uint4 rawOut)
{
    // Chunk index lets us know how deep into the mode rotation we need to go to fetch the rotation data for this block
    uint address = modeRotationByteAddress + 1 + (chunkIndex * 8);
    
    uint rotations[8];
    rotations[0] = fetchNextByte(address, buffer);
    rotations[1] = fetchNextByte(address + 1, buffer);
    rotations[2] = fetchNextByte(address + 2, buffer);
    rotations[3] = fetchNextByte(address + 3, buffer);
    rotations[4] = fetchNextByte(address + 4, buffer);
    rotations[5] = fetchNextByte(address + 5, buffer);
    rotations[6] = fetchNextByte(address + 6, buffer);
    rotations[7] = fetchNextByte(address + 7, buffer);

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
    R0 = (R0 - (rotations[0])) % 32;
    uint R1 = (rawOut[0] >> 13) & 0x1Fu;
    R1 = (R1 - (rotations[1])) % 32;
    
    WriteBitsToDword(R0, 8  /*5 bit(s)*/, raw0);
    WriteBitsToDword(R1, 13 /*5 bit(s)*/, raw0);
    
    // G0-1
    uint G0 = (rawOut[0] >> 18) & 0x1Fu;
    G0 = (G0 - (rotations[2])) % 32;
    uint G1 = (rawOut[0] >> 23) & 0x1Fu;
    G1 = (G1 - (rotations[3])) % 32;
    
    WriteBitsToDword(G0, 18 /*5 bit(s)*/, raw0);
    WriteBitsToDword(G1, 23 /*5 bit(s)*/, raw0);
    
    // B0-1
    uint B0 = ((rawOut[1] & 0x1u) << 4) | (rawOut[0] >> 28);
    B0 = (B0 - (rotations[4])) % 32;
    uint B1 = (rawOut[1] >> 1) & 0x1Fu;
    B1 = (B1 - (rotations[5])) % 32;
    
    WriteBitsToDword(B0 & 0xFu, 28 /*4 bit(s)*/, raw0);
    WriteBitsToDword(B0 >> 4,   0  /*1 bit(s)*/, raw1);
    WriteBitsToDword(B1,        1  /*5 bit(s)*/, raw1);
    
    // A0-1
    uint A0 = (rawOut[1] >> 6) & 0x1Fu;
    A0 = (A0 - (rotations[6])) % 32;
    uint A1 = (rawOut[1] >> 11) & 0x1Fu;
    A1 = (A1 - (rotations[7])) % 32;
    
    WriteBitsToDword(A0, 6  /*5 bit(s)*/, raw1);
    WriteBitsToDword(A1, 11 /*5 bit(s)*/, raw1);

    // Copy the last bits (after color) of rawOut[2] to raw2
    WriteBitsToDword(rawOut[1] >> 16, 16 /*8 bit(s)*/, raw1);
    WriteBitsToDword(rawOut[1] >> 24, 24 /*8 bit(s)*/, raw1);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
}

void derotateMode5(uint modeRotationByteAddress, uint chunkIndex, in ByteAddressBuffer buffer, inout uint4 rawOut)
{   
    // Chunk index lets us know how deep into the mode rotation we need to go to fetch the rotation data for this block
    uint address = modeRotationByteAddress + 1 + (chunkIndex * 8);
    
    uint rotations[8];
    rotations[0] = fetchNextByte(address, buffer);
    rotations[1] = fetchNextByte(address + 1, buffer);
    rotations[2] = fetchNextByte(address + 2, buffer);
    rotations[3] = fetchNextByte(address + 3, buffer);
    rotations[4] = fetchNextByte(address + 4, buffer);
    rotations[5] = fetchNextByte(address + 5, buffer);
    rotations[6] = fetchNextByte(address + 6, buffer);
    rotations[7] = fetchNextByte(address + 7, buffer);

    // Mode 5
    // 6 bit mode               =>  6 bits
    // 2 bits rotation          =>  2 bits
    // 7 bits R0 - 7 bits R1    => 14 bits
    // 7 bits B0 - 7 bits G1    => 14 bits
    // 7 bits G0 - 7 bits B1    => 14 bits
    // 8 bits A0 - 8 bits A1    => 16 bits
    // 31 bits index color      => 31 bits
    // 31 bits index alpha      => 31 bits
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    uint raw2 = 0u;
    
    // Write the first 8 bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0xFFu, 0 /*8 bit(s)*/, raw0);
    
    // De-Rotate
    // R0-R1
    uint R0 = (rawOut[0] >> 8) & 0x7Fu;
    R0 = (R0 - rotations[0]) % 128;
    uint R1 = (rawOut[0] >> 15) & 0x7Fu;
    R1 = (R1 - rotations[1]) % 128;
    
    WriteBitsToDword(R0, 8  /*7 bit(s)*/, raw0);
    WriteBitsToDword(R1, 15 /*7 bit(s)*/, raw0);
    
    // G0-G1
    uint G0 = (rawOut[0] >> 22u) & 0x7Fu;
    G0 = (G0 - rotations[2]) % 128u;
    uint G1 = ((rawOut[0] >> 29u) & 0x7u) | ((rawOut[1] & 0xFu) << 3u);
    G1 = (G1 - rotations[3]) % 128u;
    
    WriteBitsToDword(G0,        22 /*7 bit(s)*/, raw0);
    WriteBitsToDword(G1 & 0x7u, 29 /*3 bit(s)*/, raw0);
    WriteBitsToDword(G1 >> 3u,  0  /*4 bit(s)*/, raw1);
    
    // B0-B1
    uint B0 = (rawOut[1] >> 4u) & 0x7Fu;
    B0 = (B0 - rotations[4]) % 128;
    uint B1 = (rawOut[1] >> 11u) & 0x7Fu;
    B1 = (B1 - rotations[5]) % 128;
    
    WriteBitsToDword(B0, 4  /*7 bit(s)*/, raw1);
    WriteBitsToDword(B1, 11 /*7 bit(s)*/, raw1);
    
    // A0-A1
    uint A0 = (rawOut[1] >> 18u) & 0x7Fu;
    A0 = (A0 - rotations[6]) % 128;
    uint A1 = (rawOut[1] >> 25u) & 0x7Fu;
    A1 = (A1 - rotations[7]) % 128;
    
    WriteBitsToDword(A0, 18 /*7 bit(s)*/, raw1);
    WriteBitsToDword(A1, 25 /*7 bit(s)*/, raw1);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
}

void derotateMode6(uint modeRotationByteAddress, uint chunkIndex, in ByteAddressBuffer buffer, inout uint4 rawOut)
{
    // Chunk index lets us know how deep into the mode rotation we need to go to fetch the rotation data for this block
    uint address = modeRotationByteAddress + 1 + (chunkIndex * 8);
    
    uint rotations[8];
    rotations[0] = fetchNextByte(address, buffer);
    rotations[1] = fetchNextByte(address + 1, buffer);
    rotations[2] = fetchNextByte(address + 2, buffer);
    rotations[3] = fetchNextByte(address + 3, buffer);
    rotations[4] = fetchNextByte(address + 4, buffer);
    rotations[5] = fetchNextByte(address + 5, buffer);
    rotations[6] = fetchNextByte(address + 6, buffer);
    rotations[7] = fetchNextByte(address + 7, buffer);
        
    // Mode 6
    // 7 bit mode               =>  7 bits
    // 7 bits R0 - 7 bits R1    => 14 bits
    // 7 bits B0 - 7 bits G1    => 14 bits
    // 7 bits G0 - 7 bits B1    => 14 bits
    // 7 bits A0 - 7 bits A1    => 14 bits
    // 1 bit  P0 - 1 bit  P1    => 2 bits
    // 63 bits index            => 63 bits
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    
    // Write the first 8 bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0x7Fu, 0 /*7 bit(s)*/, raw0);
    
    // De-Rotate
    // R0-1
    uint R0 = (rawOut[0] >> 7) & 0x7Fu;
    R0 = (R0 - rotations[0]) % 128;
    uint R1 = (rawOut[0] >> 14) & 0x7Fu;
    R1 = (R1 - rotations[1]) % 128;
    
    WriteBitsToDword(R0, 7  /*7 bit(s)*/, raw0);
    WriteBitsToDword(R1, 14 /*7 bit(s)*/, raw0);
    
    // G0-1
    uint G0 = (rawOut[0] >> 21) & 0x7Fu;
    G0 = (G0 - rotations[2]) % 128;
    uint G1 = ((rawOut[0] >> 28) & 0xFu) | ((rawOut[1] & 0x7u) << 4);
    G1 = (G1 - rotations[3]) % 128;
    
    WriteBitsToDword(G0,        21 /*7 bit(s)*/, raw0);
    WriteBitsToDword(G1 & 0xFu, 28 /*4 bit(s)*/, raw0);
    WriteBitsToDword(G1 >> 4u,  0  /*3 bit(s)*/, raw1);
    
    // B0-1
    uint B0 = (rawOut[1] >> 3) & 0x7Fu;
    B0 = (B0 - rotations[4]) % 128;
    uint B1 = (rawOut[1] >> 10) & 0x7Fu;
    B1 = (B1 - rotations[5]) % 128;
    
    WriteBitsToDword(B0, 3  /*7 bit(s)*/, raw1);
    WriteBitsToDword(B1, 10 /*7 bit(s)*/, raw1);
    
    // A0-1
    uint A0 = (rawOut[1] >> 17) & 0x7Fu;
    A0 = (A0 - rotations[6]) % 128;
    uint A1 = (rawOut[1] >> 24) & 0x7Fu;
    A1 = (A1 - rotations[7]) % 128;
    
    WriteBitsToDword(A0,        17 /*7 bit(s)*/, raw1);
    WriteBitsToDword(A1,        24 /*7 bit(s)*/, raw1);
    
    // Copy the last bits (after color) of rawOut[1] to raw1
    WriteBitsToDword((rawOut[1] >> 31) & 0x1u, 31 /*1 bit(s)*/, raw1);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
}

void derotateMode7(uint modeRotationByteAddress, uint chunkIndex, in ByteAddressBuffer buffer, inout uint4 rawOut)
{
    
    // Chunk index lets us know how deep into the mode rotation we need to go to fetch the rotation data for this block
    uint address = modeRotationByteAddress + 1 + (chunkIndex * 8);
    
    uint rotations[8];
    rotations[0] = fetchNextByte(address, buffer);
    rotations[1] = fetchNextByte(address + 1, buffer);
    rotations[2] = fetchNextByte(address + 2, buffer);
    rotations[3] = fetchNextByte(address + 3, buffer);
    rotations[4] = fetchNextByte(address + 4, buffer);
    rotations[5] = fetchNextByte(address + 5, buffer);
    rotations[6] = fetchNextByte(address + 6, buffer);
    rotations[7] = fetchNextByte(address + 7, buffer);
    
    // Mode 7
    // 8 bit mode                                       =>  8 bits
    // 6 bit partition                                  =>  6 bits
    // 5 bits R0 - 5 bits R1 - 5 bits R2 - 5 bits R3    => 20 bits
    // 5 bits B0 - 5 bits G1 - 5 bits R2 - 5 bits G3    => 20 bits
    // 5 bits G0 - 5 bits B1 - 5 bits R2 - 5 bits B3    => 20 bits
    // 5 bits A0 - 5 bits A1 - 5 bits A2 - 5 bits A3    => 20 bits
    // 1 bit  P0 - 1 bit  P1 - 1 bit  P2 - 1 bit  P3    =>  4 bits
    // 30 bits index                                    => 30 bits
    
    uint raw0 = 0u;
    uint raw1 = 0u;
    uint raw2 = 0u;
    
    // Write the first 14 bits (non-color) to Raw0 
    WriteBitsToDword(rawOut[0] & 0xFFu,         0 /*8 bit(s)*/, raw0);
    WriteBitsToDword((rawOut[0] >> 8) & 0x3Fu,  8 /*6 bit(s)*/, raw0);
    
    // De-Rotate
    // R0-1-2-3
    uint R0 = (rawOut[0] >> 14) & 0x1Fu;
    R0 = (R0 - (rotations[0])) % 32;
    uint R1 = (rawOut[0] >> 19) & 0x1Fu;
    R1 = (R1 - (rotations[1])) % 32;
    uint R2 = (rawOut[0] >> 24) & 0x1Fu;
    R2 = (R2 - (rotations[0])) % 32;
    uint R3 = ((rawOut[1] & 0x3u) << 3) | (rawOut[0] >> 29);
    R3 = (R3 - (rotations[1])) % 32;
    
    WriteBitsToDword(R0,        14 /*5 bit(s)*/, raw0);
    WriteBitsToDword(R1,        19 /*5 bit(s)*/, raw0);
    WriteBitsToDword(R2,        24 /*5 bit(s)*/, raw0);
    WriteBitsToDword(R3 & 0x7u, 29 /*3 bit(s)*/, raw0);
    WriteBitsToDword(R3 >> 3,   0  /*2 bit(s)*/, raw1);
    
    // G0-1-2-3
    uint G0 = (rawOut[1] >> 2) & 0x1Fu;
    G0 = (G0 - (rotations[2])) % 32;
    uint G1 = (rawOut[1] >> 7) & 0x1Fu;
    G1 = (G1 - (rotations[3])) % 32;
    uint G2 = (rawOut[1] >> 12) & 0x1Fu;
    G2 = (G2 - (rotations[2])) % 32;
    uint G3 = (rawOut[1] >> 17) & 0x1Fu;
    G3 = (G3 - (rotations[3])) % 32;
    
    WriteBitsToDword(G0, 2  /*5 bit(s)*/, raw1);
    WriteBitsToDword(G1, 7  /*5 bit(s)*/, raw1);
    WriteBitsToDword(G2, 12 /*5 bit(s)*/, raw1);
    WriteBitsToDword(G3, 17 /*5 bit(s)*/, raw1);
    
    // B0-1-2-3
    uint B0 = (rawOut[1] >> 22) & 0x1Fu;
    B0 = (B0 - (rotations[4])) % 32;
    uint B1 = (rawOut[1] >> 27) & 0x1Fu;
    B1 = (B1 - (rotations[5])) % 32;
    uint B2 = rawOut[2] & 0x1Fu;
    B2 = (B2 - (rotations[4])) % 32;
    uint B3 = (rawOut[2] >> 5) & 0x1Fu;
    B3 = (B3 - (rotations[5])) % 32;

    WriteBitsToDword(B0, 22 /*5 bit(s)*/, raw1);
    WriteBitsToDword(B1, 27 /*5 bit(s)*/, raw1);
    WriteBitsToDword(B2, 0  /*5 bit(s)*/, raw2);
    WriteBitsToDword(B3, 5  /*5 bit(s)*/, raw2);
    
    // A0-1-2-3
    uint A0 = (rawOut[2] >> 10) & 0x1Fu;
    A0 = (A0 - (rotations[6])) % 32;
    uint A1 = (rawOut[2] >> 15) & 0x1Fu;
    A1 = (A1 - (rotations[7])) % 32;
    uint A2 = (rawOut[2] >> 20) & 0x1Fu;
    A2 = (A2 - (rotations[6])) % 32;
    uint A3 = (rawOut[2] >> 25) & 0x1Fu;
    A3 = (A3 - (rotations[7])) % 32;

    WriteBitsToDword(A0,        10 /*5 bit(s)*/, raw2);
    WriteBitsToDword(A1,        15 /*5 bit(s)*/, raw2);
    WriteBitsToDword(A2,        20 /*5 bit(s)*/, raw2);
    WriteBitsToDword(A3,        25 /*5 bit(s)*/, raw2);
    
    // Copy the last bits (after color) of rawOut[2] to raw2
    WriteBitsToDword((rawOut[2] >> 30) & 0x3u, 30 /*2 bit(s)*/, raw2);
    
    // Write back to RawOut
    rawOut[0] = raw0;
    rawOut[1] = raw1;
    rawOut[2] = raw2;
}

// mode4ReorderRGBA and mode5ReorderRGBA are transforms that need to be applied to modes 4 and 5 after every other transform.

void mode4ReorderRGBA(in ByteAddressBuffer buffer, uint miscAddress, uint scrapData, inout uint4 rawOut)
{
    uint miscByte3 = fetchNextByte(miscAddress + 3, buffer);
    uint rotationBits = ((scrapData & 0x1) << 1) | (miscByte3 >> 7);
    
    uint A0Mask = 0x000007C0;
    uint A1Mask = 0x0000F800;
    uint A0ReadIndex = 6;
    uint A1ReadIndex = 11;
        
    uint tempA0 = (rawOut[1] & A0Mask) >> A0ReadIndex;
    uint tempA1 = (rawOut[1] & A1Mask) >> A1ReadIndex;
        
    // Clear all destination bits (12 total) to later overwrite them easily.
    // Raw1[6 - 11] | Raw1[12 -17]
    rawOut[1] &= ~(0x0003FFC0);
    
    if (rotationBits == 1)      // R0,R1 -> Positions Raw0[8 - 12], Raw0[13 - 17]
    {
        uint tempR0 = (rawOut[0] & 0x00001F00u) >> 8;
        uint tempR1 = (rawOut[0] & 0x0003E000u) >> 13;
        
        rawOut[0] &= ~(0x00001F00u);
        rawOut[0] &= ~(0x0003E000u);
        rawOut[0] |= tempA0 << 8;
        rawOut[0] |= tempA1 << 13;
        
        rawOut[1] |= (tempR0 << 7);
        rawOut[1] |= (tempR1 << 13);
    }
    else if (rotationBits == 2) // G0,G1 -> Positions Raw0[18 - 22], Raw0[23 - 27] 
    {
        uint tempG0 = (rawOut[0] & 0x007C0000u) >> 18;
        uint tempG1 = (rawOut[0] & 0x0F800000u) >> 23;
        
        rawOut[0] &= ~(0x007C0000u);
        rawOut[0] &= ~(0x0F800000u);
        rawOut[0] |= (tempA0 << 18);
        rawOut[0] |= (tempA1 << 23);
        
        rawOut[1] |= (tempG0 << 7);
        rawOut[1] |= (tempG1 << 13);
    }
    else if (rotationBits == 3) // B0,B1 -> Positions Raw0[28-31] + Raw1[0], Raw1[1 - 5]
    {
        uint tempB0 = (rawOut[1] & 0x1u) << 4 | (rawOut[0] >> 28);
        uint tempB1 = (rawOut[1] & 0x0000003Eu) >> 1;
        
        rawOut[0] &= ~(0xF0000000u);
        rawOut[1] &= ~(0x00000001u);
        rawOut[1] &= ~(0x0000003Eu);
        
        rawOut[0] |= (tempA0 & 0xFu) << 28;
        rawOut[1] |= (tempA0 >> 4 & 0x1u);
        rawOut[1] |= tempA1 << 1;
        
        rawOut[1] |= (tempB0 << 7);
        rawOut[1] |= (tempB1 << 13);
    }
    else
    {
        rawOut[1] |= tempA0 << 7;
        rawOut[1] |= tempA1 << 13;
    }
        
    // Here take the extra alpha bits from scrap and put them in place
    
    // A0 5 bits corresponds to Raw1[7 - 11]  --> A0 first bit is Raw1[6]
    // A1 5 bits corresponds to Raw1[13 - 17] --> A1 first bit is Raw1[12]
    uint scrapA0 = (scrapData >> 1) & 0x1u;
    uint scrapA1 = (scrapData >> 2) & 0x1u;
        
    WriteBitsToDword(scrapA0, 6  /*1 bit(s)*/, rawOut[1]);
    WriteBitsToDword(scrapA1, 12 /*1 bit(s)*/, rawOut[1]);
}

void mode5ReorderRGBA(in ByteAddressBuffer buffer, uint miscAddress, uint scrapData, inout uint4 rawOut)
{
    uint miscByte3 = fetchNextByte(miscAddress + 3, buffer);
    uint miscByte7 = fetchNextByte(miscAddress + 7, buffer);
    uint rotationBits = ((miscByte7 >> 7) << 1) | (miscByte3 >> 7);
    
    // We read A0, A1 from its position as 7bit endpoints, which are as follow:
    // A0 -> Raw1[18 - 24]
    // A1 -> Raw1[25 - 31]
    uint tempA0 = (rawOut[1] & 0x01FC0000) >> 18;
    uint tempA1 = (rawOut[1] & 0xFE000000) >> 25;
            
    // But when rotating and writing the final value, it will be to the correct position considering the missing bit
    // A0 (w/out first bit) final pos -> Raw1[19 - 25]
    // A1 (w/out first bit) final pos -> Raw1[27 - 31] and Raw2[0 - 1]
            
    // Clear all destination bits (16 total) to later overwrite them easily
    rawOut[1] &= ~(0x03FC0000u | 0xFC000000u);
    rawOut[2] &= ~(0x00000003u);
    
    if (rotationBits == 1)      // R0,R1 -> Positions Raw0[8 - 14], Raw0[15 - 21]
    {
        uint tempR0 = (rawOut[0] & 0x00007F00u) >> 8; // ok
        uint tempR1 = (rawOut[0] & 0x003F8000u) >> 15; // ok
        
        rawOut[0] &= ~(0x00007F00u | 0x003F8000u);
        rawOut[0] |= tempA0 << 8;
        rawOut[0] |= tempA1 << 15;
        
        rawOut[1] |= (tempR0 << 19); // ok
        rawOut[1] |= ((tempR1 & 0x1Fu) << 27); // ok
        rawOut[2] |= ((tempR1 >> 5) & 0x3u); // ok
    }
    else if (rotationBits == 2) // G0,G1 -> Positions Raw0[22 - 28], Raw0[29 - 31] and Raw1[0 - 3] 
    {
        uint tempG0 = (rawOut[0] & 0x1FC00000u) >> 22u; // ok
        uint tempG1 = (rawOut[1] & 0xFu) << 3u | ((rawOut[0] & 0xE0000000u) >> 29u); // ok
        
        rawOut[0] &= ~(0x1FC00000u | 0xE0000000u);
        rawOut[1] &= ~(0x0000000Fu);
        rawOut[0] |= (tempA0 << 22u);
        rawOut[0] |= ((tempA1 & 0x7u) << 29u);
        rawOut[1] |= ((tempA1 >> 3u) & 0xFu);
       
        rawOut[1] |= (tempG0 << 19u); // ok
        rawOut[1] |= ((tempG1 & 0x1Fu) << 27u); // ok
        rawOut[2] |= ((tempG1 >> 5) & 0x3u); // ok
    }
    else if (rotationBits == 3) // B0,B1 -> Positions Raw1[4 - 10], Raw1[11 - 17]
    {
        uint tempB0 = (rawOut[1] & 0x000007F0) >> 4; // ok
        uint tempB1 = (rawOut[1] & 0x0003F800) >> 11; // ok
        
        rawOut[1] &= ~(0x000007F0 | 0x0003F800);
        rawOut[1] |= tempA0 << 4;
        rawOut[1] |= tempA1 << 11;
        
        rawOut[1] |= (tempB0 << 19); // ok
        rawOut[1] |= ((tempB1 & 0x1Fu) << 27); // ok
        rawOut[2] |= ((tempB1 >> 5) & 0x3u); // ok
    }
    else
    {
        rawOut[1] |= tempA0 << 19;
        rawOut[1] |= (tempA1 & 0x1Fu) << 27;
        rawOut[2] |= (tempA1 >> 5) & 0x3u;
    }
    
    // Here take the extra alpha bits from scrap and put them in place
    
    // A0 7 bits are Raw1[19 - 25] -------------------> A0 first bit is Raw1[18]
    // A1 7 bits are Raw1[27 - 31] and Raw2[0 - 1] ---> A1 first bit is Raw1[26]
    uint A0first = scrapData & 0x1u;
    uint A1first = (scrapData >> 1) & 0x1u;
            
    // First, zero the position for both bits
    rawOut[1] &= ~(0x04040000u);
            
    // Then add the bit we got from scratch
    //rawOut[1] |= (A0first << 18);
    //rawOut[1] |= (A1first << 26);
    WriteBitsToDword(A0first, 18 /*1 bit(s)*/, rawOut[1]);
    WriteBitsToDword(A1first, 26 /*1 bit(s)*/, rawOut[1]);
}