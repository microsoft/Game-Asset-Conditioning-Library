//-------------------------------------------------------------------------------------
// bc7.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#include "gacl.h"

#include <vector>

struct ColorRotation
{
    uint8_t E[8];
};

// Describes ordering strategies where we trade 1-4 scrap bits to flip some channels
union EndpointOrderStrategy 
{
    struct
    {
        uint16_t B0R : 1;
        uint16_t B0G : 1;
        uint16_t B0B : 1;
        uint16_t B0A : 1;

        uint16_t B1R : 1;
        uint16_t B1G : 1;
        uint16_t B1B : 1;
        uint16_t B1A : 1;

        uint16_t B2R : 1;
        uint16_t B2G : 1;
        uint16_t B2B : 1;
        uint16_t B2A : 1;

        uint16_t B3R : 1;
        uint16_t B3G : 1;
        uint16_t B3B : 1;
        uint16_t B3A : 1;
    } BitChannels;
    struct
    {
        uint16_t B0 : 4;
        uint16_t B1 : 4;
        uint16_t B2 : 4;
        uint16_t B3 : 4;
    } Bits;
    struct
    {
        uint8_t B01;
        uint8_t B23;
    } BitPairs;
    uint16_t Raw;
    uint16_t BitsUsed() const
    {
        return Bits.B3 != 0 ? 4 : (Bits.B2 != 0 ? 3 : (Bits.B1 != 0 ? 2 : (Bits.B0 != 0 ? 1 : 0)));
    }
};

struct BC7TextureMetrics
{
    size_t ModeCounts[9];

    struct {
        uint8_t Statics;
        uint8_t LowEntropy;
        EndpointOrderStrategy OrderingStrategies[4];
    } M[8];

    uint8_t Statics;
    uint8_t LowEntropy;

};

struct BC7ModeJoinShuffleOptions
{
    bool AltEncode;
};

// BC7 shuffle tries a variety of transforms and keeps the best.
// Since the compress is already done as part of transform selection, a "transform-only" version of BC7 is not included

void BC7_Mode45_ReorderRGBA(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest);
void BC7_CollectTextureMetrics(const uint8_t* src, size_t srcSize, BC7TextureMetrics& m);

void BC7_ModeJoin_RotateColors(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, bool altEncoding, size_t regionSize);
void BC7_ModeJoin_Transform(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics, std::vector<uint8_t>& controlBytes);
void BC7_ModeJoin_Reverse(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, BC7ModeJoinShuffleOptions& opt, BC7TextureMetrics& metrics, std::vector<uint8_t>& controlBytes);


enum BC7ModeSplitModeTransform
{
    BC7ModeSplitModeTransformA = 1,
    BC7ModeSplitModeTransformB = 2,
    BC7ModeSplitModeTransformAny = 3,
};

enum BC7ModeSplitShufflePattern : uint8_t
{
    EndpointPair4bit,
    ColorPlane4bit,
    EndpointPairSignificantBitInderleaved,
    EndpointQuadSignificantBitInderleaved,
    EndpointQuadSignificantBitInderleavedAlt,
    StableIsland,
};

enum BC7ModeSplitFieldOrderStrategy
{
    GroupCorrelatedFields = 1,
    DropLeastEntropic = 2,
};

struct BC7ModeSplitShuffleOptions
{
    BC7ModeSplitModeTransform ModeTransform;
    BC7ModeSplitShufflePattern Patterns[8];
    uint8_t EndpointOrderStrategy;
    uint32_t RotationRegionSize;
};



void BC7_ModeSplit_Transform(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& destA, std::vector<uint8_t>& destB, BC7ModeSplitShuffleOptions& opt, BC7TextureMetrics& metrics);
void BC7_ModeSplit_Reverse(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dest, size_t destSize, const uint8_t* ref);

void BC7_ModeSplit_Validate_CopyBitOrders();


