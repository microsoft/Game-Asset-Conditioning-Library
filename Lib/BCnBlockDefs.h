//-------------------------------------------------------------------------------------
// BCnBlockDefs.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma warning (push)
#pragma warning (disable : 4201)    /*  For now, suppressing warnings for anonymous structs/unions, will revisit if compiler support warants  */

/// <summary>
/// BC7 Mode 0 element
/// 
///  Bit Offset  Bits or ordering and count
///  0           1 bit Mode (1)
///  1           4 bits Partition
///  5           4 bits R0   4 bits R1   4 bits R2   4 bits R3   4 bits R4   4 bits R5
///  29          4 bits G0   4 bits G1   4 bits G2   4 bits G3   4 bits G4   4 bits G5
///  53          4 bits B0   4 bits B1   4 bits B2   4 bits B3   4 bits B4   4 bits B5
///  77          1 bit P0    1 bit P1    1 bit P2    1 bit P3    1 bit P4    1 bit P5
///  83          45 bits Index
/// </summary>
union BC7m0
{
    uint64_t Raw[2];
    struct {
        uint64_t Mode : 1;      // 0
        uint64_t Partition : 4; // 1 - 4
        uint64_t R0 : 4;        // 5 - 8
        uint64_t R1 : 4;        // 9 - 12
        uint64_t R2 : 4;        // 13 - 16
        uint64_t R3 : 4;        // 17 - 20
        uint64_t R4 : 4;        // 21 - 24
        uint64_t R5 : 4;        // 25 - 28
        uint64_t G0 : 4;        // 29 - 32
        uint64_t G1 : 4;        // 33 - 36
        uint64_t G2 : 4;        // 37 - 40
        uint64_t G3 : 4;        // 41 - 44
        uint64_t G4 : 4;        // 45 - 48
        uint64_t G5 : 4;        // 49 - 52
        uint64_t B0 : 4;        // 53 - 56 
        uint64_t B1 : 4;        // 57 - 60
        uint64_t B2_low : 3;    // 61 - 63 
        uint64_t B2_high : 1;   // 64 
        uint64_t B3 : 4;        // 65 - 68
        uint64_t B4 : 4;        // 69 - 72 
        uint64_t B5 : 4;        // 73 - 76
        uint64_t P0 : 1;        // 77 
        uint64_t P1 : 1;        // 78
        uint64_t P2 : 1;        // 79
        uint64_t P3 : 1;        // 80
        uint64_t P4 : 1;        // 81
        uint64_t P5 : 1;        // 82
        uint64_t Index : 45;    // 83 - 127
    };
    uint64_t B2() const { return B2_low + (B2_high << 3u); }
    void setB2(uint8_t value) { B2_low = value & 0b111; B2_high = (value >> 3) & 0b1; }

    void ApplyRotation7bit(uint8_t* rotations)
    {
        uint8_t rotationsShifted[6] = {};
        for (uint32_t e = 0; e < 6u; e++)
        {
            rotationsShifted[e] = uint8_t(rotations[e] >> 3u);
        }
        ApplyRotation(rotationsShifted);
    }
    void ApplyRotation(uint8_t* rotations)
    {
        R0 = (R0 + rotations[0]) % 16u;
        R1 = (R1 + rotations[1]) % 16u;
        R2 = (R2 + rotations[0]) % 16u;
        R3 = (R3 + rotations[1]) % 16u;
        R4 = (R4 + rotations[0]) % 16u;
        R5 = (R5 + rotations[1]) % 16u;

        G0 = (G0 + rotations[2]) % 16u;
        G1 = (G1 + rotations[3]) % 16u;
        G2 = (G2 + rotations[2]) % 16u;
        G3 = (G3 + rotations[3]) % 16u;
        G4 = (G4 + rotations[2]) % 16u;
        G5 = (G5 + rotations[3]) % 16u;

        B0 = (B0 + rotations[4]) % 16u;
        B1 = (B1 + rotations[5]) % 16u;
        uint8_t _B2 = (B2() + rotations[4]) % 16u;
        B2_low = _B2 & 0b111u;
        B2_high = (_B2 >> 3u) & 0b1u;
        B3 = (B3 + rotations[5]) % 16u;
        B4 = (B4 + rotations[4]) % 16u;
        B5 = (B5 + rotations[5]) % 16u;
    }
};
static_assert(sizeof(BC7m0) == 16, "BC7 blocks are all 16 bytes");

/// <summary>
/// BC7 Mode 1 element
/// 
///  Bit Offset  Bits or ordering and count
///  0           2 bits Mode (10)
///  2           6 bits Partition
///  8           6 bits R0   6 bits R1   6 bits R2   6 bits R3
///  32          6 bits G0   6 bits G1   6 bits G2   6 bits G3
///  56          6 bits B0   6 bits B1   6 bits B2   6 bits B3
///  80          1 bit P0    1 bit P1
///  82          46 bits Index
/// </summary>
union BC7m1
{
    uint64_t Raw[2];
    struct {
        uint64_t Mode : 2;      // 0 - 1
        uint64_t Partition : 6; // 2 - 7
        uint64_t R0 : 6;        // 8 - 13
        uint64_t R1 : 6;        // 14 - 19
        uint64_t R2 : 6;        // 20 - 25
        uint64_t R3 : 6;        // 26 - 31
        uint64_t G0 : 6;        // 32 - 37
        uint64_t G1 : 6;        // 38 - 43
        uint64_t G2 : 6;        // 44 - 49
        uint64_t G3 : 6;        // 50 - 55
        uint64_t B0 : 6;        // 56 - 61
        uint64_t B1_low : 2;    // 62 - 63
        uint64_t B1_high : 4;   // 64 - 67
        uint64_t B2 : 6;        // 68 - 73 
        uint64_t B3 : 6;        // 74 - 79
        uint64_t P0 : 1;        // 80 
        uint64_t P1 : 1;        // 81
        uint64_t Index : 46;    // 82 - 127
    };
    uint64_t B1() const { return B1_low + (B1_high << 2); }

    void ApplyRotation7bit(uint8_t* rotations)
    {
        uint8_t rotationsShifted[6] = {};
        for (uint32_t e = 0; e < 6; e++)
        {
            rotationsShifted[e] = uint8_t(rotations[e] >> 1u);
        }
        ApplyRotation(rotationsShifted);
    }
    void ApplyRotation(uint8_t* rotations)
    {
        R0 = (R0 + rotations[0]) % 64u;
        R1 = (R1 + rotations[1]) % 64u;
        R2 = (R2 + rotations[0]) % 64u;
        R3 = (R3 + rotations[1]) % 64u;

        G0 = (G0 + rotations[2]) % 64u;
        G1 = (G1 + rotations[3]) % 64u;
        G2 = (G2 + rotations[2]) % 64u;
        G3 = (G3 + rotations[3]) % 64u;

        B0 = (B0 + rotations[4]) % 64u;
        uint8_t _B1 = (B1() + rotations[5]) % 64u;
        B1_low = _B1 & 0b11u;
        B1_high = (_B1 >> 2u) & 0b1111u;
        B2 = (B2 + rotations[4]) % 64u;
        B3 = (B3 + rotations[5]) % 64u;
    }
};
static_assert(sizeof(BC7m1) == 16, "BC7 blocks are all 16 bytes");


/// <summary>
/// BC7 mode 2 element
///  Bit Offset  Bits or ordering and count
///  0           3 bits Mode (100)
///  3           6 bits Partition
///  9           5 bits R0   5 bits R1   5 bits R2   5 bits R3   5 bits R4   5 bits R5
///  39          5 bits G0   5 bits G1   5 bits G2   5 bits G3   5 bits R4   5 bits R5
///  69          5 bits B0   5 bits B1   5 bits B2   5 bits B3   5 bits R4   5 bits R5
///  99          29 bits Index
/// </summary>
union BC7m2
{
    uint64_t Raw[2];
    struct {
        uint64_t Mode : 3;      // 0 - 2
        uint64_t Partition : 6; // 3 - 8
        uint64_t R0 : 5;        // 9 - 13
        uint64_t R1 : 5;        // 14 - 18
        uint64_t R2 : 5;        // 19 - 23
        uint64_t R3 : 5;        // 24 - 28
        uint64_t R4 : 5;        // 29 - 33
        uint64_t R5 : 5;        // 34 - 38
        uint64_t G0 : 5;        // 39 - 43
        uint64_t G1 : 5;        // 44 - 48
        uint64_t G2 : 5;        // 49 - 53
        uint64_t G3 : 5;        // 54 - 58
        uint64_t G4 : 5;        // 59 - 63
        uint64_t G5 : 5;        // 64 - 68
        uint64_t B0 : 5;        // 69 - 73 
        uint64_t B1 : 5;        // 74 - 78
        uint64_t B2 : 5;        // 79 - 83 
        uint64_t B3 : 5;        // 84 - 88
        uint64_t B4 : 5;        // 89 - 83 
        uint64_t B5 : 5;        // 94 - 98
        uint64_t Index : 29;    // 99 - 127
    };

    void ApplyRotation7bit(uint8_t* rotations)
    {
        uint8_t rotationsShifted[6] = {};
        for (uint32_t e = 0; e < 6; e++)
        {
            rotationsShifted[e] = uint8_t(rotations[e] >> 2u);
        }
        ApplyRotation(rotationsShifted);
    }
    void ApplyRotation(uint8_t* rotations)
    {
        R0 = (R0 + rotations[0]) % 32u;
        R1 = (R1 + rotations[1]) % 32u;
        R2 = (R2 + rotations[0]) % 32u;
        R3 = (R3 + rotations[1]) % 32u;
        R4 = (R4 + rotations[0]) % 32u;
        R5 = (R5 + rotations[1]) % 32u;

        G0 = (G0 + rotations[2]) % 32u;
        G1 = (G1 + rotations[3]) % 32u;
        G2 = (G2 + rotations[2]) % 32u;
        G3 = (G3 + rotations[3]) % 32u;
        G4 = (G4 + rotations[2]) % 32u;
        G5 = (G5 + rotations[3]) % 32u;

        B0 = (B0 + rotations[4]) % 32u;
        B1 = (B1 + rotations[5]) % 32u;
        B2 = (B2 + rotations[4]) % 32u;
        B3 = (B3 + rotations[5]) % 32u;
        B4 = (B4 + rotations[4]) % 32u;
        B5 = (B5 + rotations[5]) % 32u;
    }
};
static_assert(sizeof(BC7m2) == 16, "BC7 blocks are all 16 bytes");

/// <summary>
/// BC7 mode 3 element
/// 
///  Bit Offset  Bits or ordering and count
///  0           4 bits Mode (1000)
///  4           6 bits Partition
///  10          7 bits R0   7 bits R1   7 bits R2   7 bits R3
///  38          7 bits G0   7 bits G1   7 bits G2   7 bits G3
///  66          7 bits B0   7 bits B1   7 bits B2   7 bits B3
///  94          1 bit P0    1 bit P1    1 bit P2    1 bit P3
///  98          30 bits Index
/// </summary>
union BC7m3
{
    uint64_t Raw[2];
    struct {
        uint64_t Mode : 4;      // 0 - 3
        uint64_t Partition : 6; // 4 - 9
        uint64_t R0 : 7;        // 10 - 16
        uint64_t R1 : 7;        // 17 - 23
        uint64_t R2 : 7;        // 24 - 30
        uint64_t R3 : 7;        // 31 - 37
        uint64_t G0 : 7;        // 38 - 44
        uint64_t G1 : 7;        // 45 - 51
        uint64_t G2 : 7;        // 52 - 58
        uint64_t G3_low : 5;    // 59 - 63
        uint64_t G3_high : 2;   // 64 - 65
        uint64_t B0 : 7;        // 66 - 72
        uint64_t B1 : 7;        // 73 - 79
        uint64_t B2 : 7;        // 80 - 86 
        uint64_t B3 : 7;        // 87 - 93
        uint64_t P0 : 1;        // 94 
        uint64_t P1 : 1;        // 95
        uint64_t P2 : 1;        // 96 
        uint64_t P3 : 1;        // 97
        uint64_t Index : 30;    // 98 - 127
    };
    uint64_t G3() const { return G3_low + (G3_high << 5); }

    void ApplyRotation7bit(uint8_t* rotations)
    {
        ApplyRotation(rotations);
    }
    void ApplyRotation(uint8_t* rotations)
    {
        R0 = (R0 + rotations[0]) % 128;
        R1 = (R1 + rotations[1]) % 128;
        R2 = (R2 + rotations[0]) % 128;
        R3 = (R3 + rotations[1]) % 128;

        G0 = (G0 + rotations[2]) % 128;
        G1 = (G1 + rotations[3]) % 128;
        G2 = (G2 + rotations[2]) % 128;
        uint8_t _G3 = (G3() + rotations[3]) % 128;
        G3_low = _G3 & 0b11111u;
        G3_high = (_G3 >> 5) & 0b11u;

        B0 = (B0 + rotations[4]) % 128;
        B1 = (B1 + rotations[5]) % 128;
        B2 = (B2 + rotations[4]) % 128;
        B3 = (B3 + rotations[5]) % 128;
    }
};
static_assert(sizeof(BC7m3) == 16, "BC7 blocks are all 16 bytes");


/// <summary>
/// BC7 mode 4 element
/// 
///  Bit Offset  Bits or ordering and count
///  0           5 bits Mode (10000)
///  5           2 bits Rotation
///  7           1 bit Idx Mode
///  8           5 bits R0   5 bits R1
///  18          5 bits G0   5 bits G1
///  28          5 bits B0   5 bits B1
///  38          6 bits A0   6 bits A1
///  50          31 bits Index Data
///  81          47 bits Index Data
/// </summary>
union BC7m4
{
    uint64_t Raw[2];
    struct {
        uint64_t Mode : 5;          // 0 - 4
        uint64_t Rotation : 2;      // 5 - 6
        uint64_t IdxMode : 1;       // 7
        uint64_t R0 : 5;            // 8 - 12
        uint64_t R1 : 5;            // 13 - 17
        uint64_t G0 : 5;            // 18 - 22
        uint64_t G1 : 5;            // 23 - 27
        uint64_t B0 : 5;            // 28 - 32 
        uint64_t B1 : 5;            // 33 - 37
        uint64_t A0 : 6;            // 38 - 43
        uint64_t A1 : 6;            // 44 - 49
        uint64_t IndexC_low : 14;   // 50 - 63
        uint64_t IndexC_high : 17;  // 64 - 77
        uint64_t IndexA : 47;       // 81 - 127
    };
};

/// <summary>
/// BC7 mode 4 element, derotated
/// 
/// Stock mode 4 elements contain an higher precision Alpha channel, that may contain
/// alpha or other data, based on the two rotation bits.  This variant of the block encoding 
/// is used to make color endpoints more similar to other modes, carving off the least significant
/// alpha bits, and swapping channels so the color data is forced into RGBA order
/// 
///  Bit Offset  Bits or ordering and count
///  0           5 bits Mode (10000)
///  5           2 bits Rotation
///  7           1 bit Idx Mode
///  8           5 bits R0   5 bits R1
///  18          5 bits G0   5 bits G1
///  28          5 bits B0   5 bits B1
///  38          1 bit ex0
///  39          5 bits A0
///  44                      1 bit ex1
///  45                      5 bits A1
///  50          31 bits Index Data
///  81          47 bits Index Data
/// </summary>

union BC7m4_Derotated
{
    uint64_t Raw[2];
    struct {
        uint64_t Mode : 5;          // 0 - 4
        uint64_t Rotation : 2;      // 5 - 6
        uint64_t IdxMode : 1;       // 7
        uint64_t R0 : 5;            // 8 - 12
        uint64_t R1 : 5;            // 13 - 17
        uint64_t G0 : 5;            // 18 - 22
        uint64_t G1 : 5;            // 23 - 27
        uint64_t B0 : 5;            // 28 - 32 
        uint64_t B1 : 5;            // 33 - 37
        uint64_t ex0 : 1;           // 38           
        uint64_t A0 : 5;            // 39 - 43
        uint64_t ex1 : 1;           // 44
        uint64_t A1 : 5;            // 45 - 49
        uint64_t IndexC_low : 14;   // 50 - 63
        uint64_t IndexC_high : 17;  // 64 - 77
        uint64_t IndexA : 47;       // 81 - 127
    };

    void ApplyRotation7bit(uint8_t* rotations)
    {
        uint8_t rotationsShifted[8] = {};
        for (uint32_t e = 0; e < 8u; e++)
        {
            rotationsShifted[e] = uint8_t(rotations[e] >> 2u);
        }
        ApplyRotation(rotationsShifted);
    }
    void ApplyRotation(uint8_t* rotations)
    {
        R0 = (R0 + rotations[0]) % 32u;
        R1 = (R1 + rotations[1]) % 32u;

        G0 = (G0 + rotations[2]) % 32u;
        G1 = (G1 + rotations[3]) % 32u;

        B0 = (B0 + rotations[4]) % 32u;
        B1 = (B1 + rotations[5]) % 32u;

        A0 = (A0 + rotations[6]) % 32u;
        A1 = (A1 + rotations[7]) % 32u;
    }
};
static_assert(sizeof(BC7m4) == 16, "BC7 blocks are all 16 bytes");

/// <summary>
/// BC7 mode 5 element
/// 
///  Bit Offset  Bits or ordering and count
///  0           6 bits Mode (100000)
///  6           2 bits Rotation
///  8           7 bits R0   7 bits R1
///  22          7 bits G0   7 bits G1
///  36          7 bits B0   7 bits B1
///  50          8 bits A0   8 bits A1
///  66          31 bits Colour Index
///  97          31 bits Alpha Index
/// </summary>
union BC7m5
{
    uint64_t Raw[2];
    struct {
        uint64_t Mode : 6;      // 0-5
        uint64_t Rotation : 2;  // 6-7
        uint64_t R0 : 7;        // 8-14
        uint64_t R1 : 7;        // 15-21
        uint64_t G0 : 7;        // 22
        uint64_t G1 : 7;        // 29
        uint64_t B0 : 7;        // 36 - 42 
        uint64_t B1 : 7;        // 43 - 49
        uint64_t A0 : 8;        // 50 - 57
        uint64_t A1_low : 6;    // 58 - 63
        uint64_t A1_high : 2;   // 64 - 65
        uint64_t indexC : 31;   // 66 - 96
        uint64_t indexA : 31;   // 97 - 127
    };
};
static_assert(sizeof(BC7m5) == 16, "BC7 blocks are all 16 bytes");

/// <summary>
/// /// BC7 mode 5 element, derotated
/// 
/// Stock mode 5 elements contain an higher precision Alpha channel, that may contain
/// alpha or other data, based on the two rotation bits.  This variant of the block encoding 
/// is used to make color endpoints more similar to other modes, carving off the least significant
/// alpha bits, and swapping channels so the color data is forced into RGBA order
/// 
///  Bit Offset  Bits or ordering and count
///  0           6 bits Mode (100000)
///  6           2 bits Rotation
///  8           7 bits R0   7 bits R1
///  22          7 bits G0   7 bits G1
///  36          7 bits B0   7 bits B1
///  50          1 bit ex0   
///  51          7 bits A0   
///  58                      1 bit ex1
///  59                      7 bits A1
///  66          31 bits Colour Index
///  97          31 bits Alpha Index
/// </summary>
union BC7m5_Derotated
{
    uint64_t Raw[2];
    struct {
        uint64_t Mode : 6;      // 0-5
        uint64_t Rotation : 2;  // 6-7
        uint64_t R0 : 7;        // 8-14
        uint64_t R1 : 7;        // 15-21
        uint64_t G0 : 7;        // 22
        uint64_t G1 : 7;        // 29
        uint64_t B0 : 7;        // 36 - 42 
        uint64_t B1 : 7;        // 43 - 49
        uint64_t ex0 : 1;       // 50
        uint64_t A0 : 7;        // 51 - 57
        uint64_t ex1 : 1;       // 58
        uint64_t A1_low : 5;    // 59 - 63
        uint64_t A1_high : 2;   // 64 - 65
        uint64_t indexC : 31;   // 66 - 96
        uint64_t indexA : 31;   // 97 - 127
    };

    uint64_t A1() const { return A1_low + (A1_high << 5u); }
    void SetA1(uint8_t v) { A1_low = v; A1_high = uint64_t(v >> 5u); }

    void ApplyRotation7bit(uint8_t* rotations)
    {
        ApplyRotation(rotations);
    }
    void ApplyRotation(uint8_t* rotations) 
    { 
        R0 = (R0 + rotations[0]) % 128;
        R1 = (R1 + rotations[1]) % 128;

        G0 = (G0 + rotations[2]) % 128;
        G1 = (G1 + rotations[3]) % 128;

        B0 = (B0 + rotations[4]) % 128;
        B1 = (B1 + rotations[5]) % 128;

        A0 = (A0 + rotations[6]) % 128;
        uint64_t _A1 = (A1() + rotations[7]) % 128;
        A1_low = _A1;
        A1_high = _A1 >> 5u;
    }
};
static_assert(sizeof(BC7m5_Derotated) == 16, "BC7 blocks are all 16 bytes");

/// <summary>
/// BC7 mode 6 element
/// 
///  Bit Offset  Bits or ordering and count
///  0           7 bits Mode (1000000)
///  7           7 bits R0   7 bits R1
///  21          7 bits G0   7 bits G1
///  35          7 bits B0   7 bits B1
///  49          7 bits A0   7 bits A1
///  63          1 bit P0    1 bit P1
///  65          63 bits Index
/// </summary>
union BC7m6
{
    uint64_t Raw[2];
    struct {
        uint64_t Mode : 7;
        uint64_t R0 : 7;
        uint64_t R1 : 7;
        uint64_t G0 : 7;
        uint64_t G1 : 7;
        uint64_t B0 : 7;
        uint64_t B1 : 7;
        uint64_t A0 : 7;
        uint64_t A1 : 7;
        uint64_t P0 : 1;
        uint64_t P1 : 1;
        uint64_t index : 63;
    };

    void ApplyRotation7bit(uint8_t* rotations)
    {
        ApplyRotation(rotations);
    }
    void ApplyRotation(uint8_t* rotations) 
    { 
        R0 = (R0 + rotations[0]) % 128;
        R1 = (R1 + rotations[1]) % 128;

        G0 = (G0 + rotations[2]) % 128;
        G1 = (G1 + rotations[3]) % 128;

        B0 = (B0 + rotations[4]) % 128;
        B1 = (B1 + rotations[5]) % 128;

        A0 = (A0 + rotations[6]) % 128;
        A1 = (A1 + rotations[7]) % 128;
    }
};
static_assert(sizeof(BC7m6) == 16, "BC7 blocks are all 16 bytes");

/// <summary>
/// BC7 mode 7 element
/// 
///  Bit Offset  Bits or ordering and count
///  0           8 bits Mode (10000000)
///  6           6 bits Partition
///  14          5 bits R0   5 bits R1   5 bits R2   5 bits R3
///  34          5 bits G0   5 bits G1   5 bits G2   5 bits G3
///  54          5 bits B0   5 bits B1   5 bits B2   5 bits B3
///  74          5 bits A0   5 bits A1   5 bits A2   5 bits A3
///  94          1 bit P0    1 bit P1    1 bit P2    1 bit P2
///  98          30 bits Index
/// </summary>
union BC7m7
{
    uint64_t Raw[2];
    struct {
        uint64_t Mode : 8;          // 0 - 7
        uint64_t Partition : 6;     // 8 - 13
        uint64_t R0 : 5;            // 14 - 18
        uint64_t R1 : 5;            // 19 - 23
        uint64_t R2 : 5;            // 24 - 28
        uint64_t R3 : 5;            // 29 - 33
        uint64_t G0 : 5;            // 34 - 38
        uint64_t G1 : 5;            // 39 - 43
        uint64_t G2 : 5;            // 44 - 48
        uint64_t G3 : 5;            // 49 - 53
        uint64_t B0 : 5;            // 54 - 58
        uint64_t B1 : 5;            // 59 - 63
        uint64_t B2 : 5;            // 64 - 68
        uint64_t B3 : 5;            // 69 - 73
        uint64_t A0 : 5;            // 74 - 78
        uint64_t A1 : 5;            // 79 - 83
        uint64_t A2 : 5;            // 84 - 88
        uint64_t A3 : 5;            // 89 - 93
        uint64_t P0 : 1;            // 94
        uint64_t P1 : 1;            // 95
        uint64_t P2 : 1;            // 96
        uint64_t P3 : 1;            // 97
        uint64_t index : 30;        // 98 - 127
    };

    void ApplyRotation7bit(uint8_t* rotations)
    {
        uint8_t rotationsShifted[8] = {};
        for (uint32_t e = 0; e < 8u; e++)
        {
            rotationsShifted[e] = uint8_t(rotations[e] >> 2u);
        }
        ApplyRotation(rotationsShifted);
    }
    void ApplyRotation(uint8_t* rotations)
    {
        R0 = (R0 + rotations[0]) % 32;
        R1 = (R1 + rotations[1]) % 32;
        R2 = (R2 + rotations[0]) % 32;
        R3 = (R3 + rotations[1]) % 32;

        G0 = (G0 + rotations[2]) % 32;
        G1 = (G1 + rotations[3]) % 32;
        G2 = (G2 + rotations[2]) % 32;
        G3 = (G3 + rotations[3]) % 32;

        B0 = (B0 + rotations[4]) % 32;
        B1 = (B1 + rotations[5]) % 32;
        B2 = (B2 + rotations[4]) % 32;
        B3 = (B3 + rotations[5]) % 32;

        A0 = (A0 + rotations[6]) % 32;
        A1 = (A1 + rotations[7]) % 32;
        A2 = (A2 + rotations[6]) % 32;
        A3 = (A3 + rotations[7]) % 32;
    }
};
static_assert(sizeof(BC7m7) == 16, "BC7 blocks are all 16 bytes");

#pragma warning (pop)
