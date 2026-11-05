//-------------------------------------------------------------------------------------
// streamheader.cpp
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#include "streamheader.h"

#include <cstring>
#include <unordered_map>
#include <algorithm>
#include <array>
#include <functional>

namespace
{
    constexpr uint32_t kNone = UINT32_MAX;

    unsigned FloorLog2(uint64_t x)
    {
        unsigned b = 0;
        while (x >>= 1) b++;
        return b;
    }

    size_t ExpGolombBits(uint64_t v)
    {
        return 2 * size_t(FloorLog2(v + 1)) + 1;
    }

    struct BitWriter
    {
        std::vector<uint8_t> Bytes;
        size_t BitCount = 0;

        void Put(uint64_t v, unsigned bits)
        {
            for (unsigned i = 0; i < bits; i++, BitCount++)
            {
                if ((BitCount & 7) == 0) Bytes.push_back(0);
                Bytes.back() |= uint8_t(((v >> i) & 1) << (BitCount & 7));
            }
        }

        void PutExpGolomb(uint64_t v)
        {
            const uint64_t x = v + 1;
            const unsigned b = FloorLog2(x);
            Put(0, b);
            Put(1, 1);
            Put(x, b);
        }
    };

    struct BitReader
    {
        const uint8_t* Data;
        size_t SizeBits;
        size_t Pos = 0;

        bool Get(unsigned bits, uint64_t& v)
        {
            v = 0;
            if (Pos + bits > SizeBits) return false;
            for (unsigned i = 0; i < bits; i++, Pos++)
            {
                v |= uint64_t((Data[Pos >> 3] >> (Pos & 7)) & 1) << i;
            }
            return true;
        }

        bool GetExpGolomb(uint64_t& v)
        {
            unsigned b = 0;
            for (;;)
            {
                uint64_t bit;
                if (!Get(1, bit)) return false;
                if (bit) break;
                if (++b > 40) return false;
            }
            uint64_t low;
            if (!Get(b, low)) return false;
            v = ((uint64_t(1) << b) | low) - 1;
            return true;
        }
    };

    struct BlockKey
    {
        uint64_t A, B;
        bool operator==(const BlockKey& o) const { return A == o.A && B == o.B; }
    };

    struct BlockKeyHash
    {
        size_t operator()(const BlockKey& k) const
        {
            uint64_t h = k.A * 0x9E3779B97F4A7C15ull;
            h ^= k.B + 0x632BE59BD9B4E019ull + (h << 6) + (h >> 2);
            h ^= h >> 29;
            h *= 0xBF58476D1CE4E5B9ull;
            h ^= h >> 32;
            return size_t(h);
        }
    };

    BlockKey ReadKey(const uint8_t* p, size_t blockSize)
    {
        BlockKey k = {};
        memcpy(&k.A, p, 8);
        if (blockSize == 16) memcpy(&k.B, p + 8, 8);
        return k;
    }

    bool ValidBlockStream(size_t srcSize, size_t blockSize)
    {
        return (blockSize == 8 || blockSize == 16) && srcSize % blockSize == 0 && srcSize / blockSize >= 2 &&
            srcSize / blockSize < kNone;
    }

    // Coded blocks: 9 log2 buckets per byte (0 | 1 | 2-3 | ... | 128-255), canonical Huffman code per (class, byte)
    constexpr unsigned kBuckets = 9;
    constexpr unsigned kMaxCodeLen = 8;
    constexpr unsigned kModeClassBits = 3;

    struct BucketCode
    {
        uint8_t Len[kBuckets];
        uint8_t Code[kBuckets];
        uint8_t Count[kMaxCodeLen + 1];     // codes per length; Count[0] = 0
        uint8_t Sorted[kBuckets];           // symbols in canonical order
    };

    bool BuildBucketCode(uint32_t packed, BucketCode& c)
    {
        c = {};
        for (unsigned s = 0; s < kBuckets; s++)
        {
            c.Len[s] = uint8_t(((packed >> (3 * s)) & 7) + 1);
            c.Count[c.Len[s]]++;
        }
        unsigned kraft = 0;
        for (unsigned len = 1; len <= kMaxCodeLen; len++) kraft += unsigned(c.Count[len]) << (kMaxCodeLen - len);
        if (kraft != 1u << kMaxCodeLen) return false;

        unsigned next[kMaxCodeLen + 1] = {};
        unsigned code = 0;
        for (unsigned len = 1; len <= kMaxCodeLen; len++)
        {
            code = (code + c.Count[len - 1]) << 1;
            next[len] = code;
        }
        unsigned i = 0;
        for (unsigned len = 1; len <= kMaxCodeLen; len++)
        {
            for (unsigned s = 0; s < kBuckets; s++)
            {
                if (c.Len[s] == len)
                {
                    c.Code[s] = uint8_t(next[len]++);
                    c.Sorted[i++] = uint8_t(s);
                }
            }
        }
        return true;
    }

    struct BlockCode
    {
        std::vector<BucketCode> Codes;      // per (class, byte position)
        size_t Size = 16;                   // bytes per block
        bool ModeClasses = true;            // class = BC7 mode (3 class bits); else one class, no class bits
        void (*Map)(uint8_t*, bool, const std::vector<StreamHeaderParam>&) = nullptr;
    };

    bool BuildBlockCode(const StreamHeaderBlockCode* blockCode, size_t blockSize, BlockCode& code)
    {
        if (blockCode == nullptr || blockCode->Lengths == nullptr || (blockCode->ModeClasses && blockSize != 16)) return false;
        code.Size = blockSize;
        code.ModeClasses = blockCode->ModeClasses;
        code.Map = blockCode->Map;
        code.Codes.resize((code.ModeClasses ? 8 : 1) * blockSize);
        for (size_t c = 0; c < code.Codes.size(); c++)
        {
            if (!BuildBucketCode(blockCode->Lengths[c], code.Codes[c])) return false;
        }
        return true;
    }

    unsigned Bucket(unsigned v)
    {
        return v == 0 ? 0 : FloorLog2(v) + 1;
    }

    // with mode classes, the trailing zero count of byte 15 (the BC7 mode; 8 = not codable); else 0
    unsigned BlockClass(const uint8_t* block, const BlockCode& code)
    {
        unsigned m = 0;
        while (code.ModeClasses && m < 8 && !((block[15] >> m) & 1)) m++;
        return m;
    }

    unsigned CodedByte(const uint8_t* block, const BlockCode& code, unsigned m, size_t p)
    {
        return p == 15 && code.ModeClasses ? unsigned(block[15]) >> (m + 1) : block[p];
    }

    // the form a block is coded in: code.Map's image of it (in tmp, code.Size bytes), or the block itself
    const uint8_t* CodedForm(const uint8_t* block, const BlockCode& code, const std::vector<StreamHeaderParam>& params, uint8_t* tmp)
    {
        if (code.Map == nullptr) return block;
        memcpy(tmp, block, code.Size);
        code.Map(tmp, false, params);
        return tmp;
    }

    size_t CodedBlockBits(const uint8_t* block, const BlockCode& code, unsigned posBits)
    {
        const unsigned m = BlockClass(block, code);
        size_t bits = posBits + (code.ModeClasses ? kModeClassBits : 0);
        for (size_t p = 0; p < code.Size; p++)
        {
            const unsigned b = Bucket(CodedByte(block, code, m, p));
            bits += code.Codes[m * code.Size + p].Len[b] + (b >= 2 ? b - 1 : 0);
        }
        return bits;
    }

    // a coded block's entry after its position: class bits and bytes
    void PutCodedBody(BitWriter& w, const uint8_t* block, const BlockCode& code)
    {
        const unsigned m = BlockClass(block, code);
        if (code.ModeClasses)
        {
            w.Put(m, kModeClassBits);
        }
        for (size_t p = 0; p < code.Size; p++)
        {
            const unsigned v = CodedByte(block, code, m, p);
            const unsigned b = Bucket(v);
            const BucketCode& c = code.Codes[m * code.Size + p];
            for (unsigned i = c.Len[b]; i-- > 0; ) w.Put((c.Code[b] >> i) & 1, 1);
            if (b >= 2) w.Put(v, b - 1);
        }
    }

    bool GetBucket(BitReader& r, const BucketCode& c, unsigned& sym)
    {
        unsigned code = 0, first = 0, index = 0;
        for (unsigned len = 1; len <= kMaxCodeLen; len++)
        {
            uint64_t bit;
            if (!r.Get(1, bit)) return false;
            code |= unsigned(bit);
            const unsigned count = c.Count[len];
            if (code < first + count)
            {
                sym = c.Sorted[index + code - first];
                return true;
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return false;
    }

    bool GetCodedBlock(BitReader& r, const BlockCode& code, uint8_t* block)
    {
        uint64_t m = 0;
        if (code.ModeClasses && !r.Get(kModeClassBits, m)) return false;
        for (size_t p = 0; p < code.Size; p++)
        {
            unsigned b;
            if (!GetBucket(r, code.Codes[m * code.Size + p], b)) return false;
            uint64_t v = b;
            if (b >= 2)
            {
                if (!r.Get(b - 1, v)) return false;
                v |= uint64_t(1) << (b - 1);
            }
            if (p == 15 && code.ModeClasses)
            {
                // the class bits are implied: bit m set, bits below it clear
                if (v >> (7 - m)) return false;
                v = (v << (m + 1)) | (uint64_t(1) << m);
            }
            block[p] = uint8_t(v);
        }
        return true;
    }

    // Thread group of block i in the per-format GPU decoders (UnshuffleBC1Ex.hlsl - UnshuffleBC7Ex.hlsl, main): a
    // thread decodes T blocks, the n % T trailing blocks one each, in groups of TG threads, with (T, TG) by header
    // transform: BC1_224 / BC1_44 (2, 128), BC3 (4, 256), BC4_116 (2, 256), or (4, 256) from 65536 blocks (the BC4
    // build's EX_WIDE_STREAM_BLOCKS: coded blocks always take its ParseHeader), BC5 (4, 128) and BC7 Join (1, 256).
    // Curve-only BC7_EX streams (header BC5_116116) decode 4 blocks per thread in groups of 256 (two of these groups).
    size_t DecoderGroup(size_t i, size_t n, uint8_t transformId)
    {
        size_t T = 4, TG = 256;
        switch (transformId)
        {
        case 0x01: case 0x08: T = 2; TG = 128; break;
        case 0x03: T = n >= 65536 ? 4 : 2; break;
        case 0x04: case 0x0A: TG = 128; break;
        case 0x06: T = 1; break;
        default: break;
        }
        const size_t whole = n / T * T;
        return (i < whole ? i / T : whole / T + (i - whole)) / TG;
    }

    constexpr size_t kMaxCodedGroups = 4;

    // Only the decoder thread groups holding coded blocks decode coded bodies (each group the bodies up to its last
    // one), so this rechooses the K = coded.size() coded blocks (the cheapest-first count: never more blocks than the
    // cheapest choice) to touch the fewest groups, then to make those groups decode the fewest bodies, then to use the
    // fewest bits. candidates are (bits, index), cheapest first; the bits of the chosen blocks must total at most
    // maxBits. A group's cheapest candidates are its best, and splitting K into m parts, each part size needs only its
    // m cheapest groups, so the search is exact for the group count (up to kMaxCodedGroups) and for the bits, and a
    // heuristic for the body count. Keeps coded (the cheapest K, sorted or not) unless the search finds better.
    void GroupCodedBlocks(const std::vector<std::pair<size_t, size_t>>& candidates, size_t n, uint8_t transformId,
        size_t maxBits, std::vector<size_t>& coded)
    {
        const size_t K = coded.size();

        // the cheapest K: groups, bodies and bits
        std::vector<std::pair<size_t, size_t>> globParts;     // (group, blocks), ascending group
        size_t globBits = 0;
        for (size_t j = 0; j < K; j++)
        {
            const size_t g = DecoderGroup(candidates[j].second, n, transformId);
            auto it = std::lower_bound(globParts.begin(), globParts.end(), std::make_pair(g, size_t(0)));
            if (it == globParts.end() || it->first != g)
            {
                it = globParts.insert(it, { g, 0 });
            }
            it->second++;
            globBits += candidates[j].first;
        }
        if (globParts.size() < 2)
        {
            return;
        }
        size_t globBodies = 0, through = 0;
        for (const auto& p : globParts)
        {
            through += p.second;
            globBodies += through;
        }

        // per group with candidates (a slot): its K cheapest candidates and the prefix sums of their bits
        std::vector<uint32_t> slotOf(DecoderGroup(n - 1, n, transformId) + 1, kNone);
        std::vector<size_t> groupOf;
        std::vector<std::vector<size_t>> sums, blocks;
        for (const auto& c : candidates)
        {
            const size_t g = DecoderGroup(c.second, n, transformId);
            if (slotOf[g] == kNone)
            {
                slotOf[g] = uint32_t(groupOf.size());
                groupOf.push_back(g);
                sums.emplace_back();
                blocks.emplace_back();
            }
            std::vector<size_t>& s = sums[slotOf[g]];
            if (s.size() < K)
            {
                s.push_back((s.empty() ? 0 : s.back()) + c.first);
                blocks[slotOf[g]].push_back(c.second);
            }
        }

        // top[a]: the (up to) kMaxCodedGroups slots whose a cheapest candidates take the fewest bits
        std::vector<std::array<uint32_t, kMaxCodedGroups>> top(K + 1);
        std::vector<size_t> topCount(K + 1, 0);
        for (size_t a = 1; a <= K; a++)
        {
            auto key = [&](uint32_t s) { return std::make_pair(sums[s][a - 1], groupOf[s]); };
            for (uint32_t s = 0; s < uint32_t(groupOf.size()); s++)
            {
                if (sums[s].size() < a) continue;
                size_t q = topCount[a];
                while (q > 0 && key(s) < key(top[a][q - 1])) q--;
                if (q == kMaxCodedGroups) continue;
                for (size_t r = (std::min)(topCount[a], kMaxCodedGroups - 1); r > q; r--) top[a][r] = top[a][r - 1];
                top[a][q] = s;
                topCount[a] = (std::min)(topCount[a] + 1, kMaxCodedGroups);
            }
        }

        // non-increasing partitions of K into m parts, each part on a distinct slot among the m cheapest for its size
        size_t m = 0, part[kMaxCodedGroups] = {};
        uint32_t slot[kMaxCodedGroups] = {};
        bool found = false;
        size_t bestBodies = 0, bestBits = 0, bestPart[kMaxCodedGroups] = {};
        uint32_t bestSlot[kMaxCodedGroups] = {};
        std::function<void(size_t, size_t)> assign = [&](size_t i, size_t bits)
        {
            if (i == m)
            {
                std::pair<size_t, size_t> order[kMaxCodedGroups];
                for (size_t q = 0; q < m; q++) order[q] = { groupOf[slot[q]], part[q] };
                std::sort(order, order + m);
                size_t bodies = 0, upTo = 0;
                for (size_t q = 0; q < m; q++)
                {
                    upTo += order[q].second;
                    bodies += upTo;
                }
                if (!found || std::make_pair(bodies, bits) < std::make_pair(bestBodies, bestBits))
                {
                    found = true;
                    bestBodies = bodies;
                    bestBits = bits;
                    std::copy(part, part + m, bestPart);
                    std::copy(slot, slot + m, bestSlot);
                }
                return;
            }
            const size_t a = part[i];
            for (size_t r = 0; r < (std::min)(topCount[a], m); r++)
            {
                const uint32_t s = top[a][r];
                if (std::find(slot, slot + i, s) != slot + i || bits + sums[s][a - 1] > maxBits) continue;
                slot[i] = s;
                assign(i + 1, bits + sums[s][a - 1]);
            }
        };
        std::function<void(size_t, size_t, size_t)> partition = [&](size_t i, size_t left, size_t maxPart)
        {
            if (i + 1 == m)
            {
                if (left <= maxPart)
                {
                    part[i] = left;
                    assign(0, 0);
                }
                return;
            }
            for (size_t a = (std::min)(maxPart, left - (m - i - 1)); a >= 1 && a * (m - i) >= left; a--)
            {
                part[i] = a;
                partition(i + 1, left - a, a);
            }
        };
        for (m = 1; m <= (std::min)({ kMaxCodedGroups, globParts.size(), K }) && !found; m++)
        {
            partition(0, K, K);
        }
        m--;

        if (found && (m < globParts.size() || std::make_pair(bestBodies, bestBits) < std::make_pair(globBodies, globBits)))
        {
            coded.clear();
            for (size_t q = 0; q < m; q++)
            {
                coded.insert(coded.end(), blocks[bestSlot[q]].begin(), blocks[bestSlot[q]].begin() + bestPart[q]);
            }
        }
    }
}

static_assert(sizeof(kStreamHeaderParamSize) <= 5, "parameter presence bits are byte 0 bits [6:3]");
static_assert(sizeof(kStreamHeaderTransforms) <= 8, "header transform codes are byte 0 bits [2:0]");

// Writes the parameter part of the header (everything before the removed-block descriptor). sorted receives the
// parameters in ascending id order, as StreamHeader_Parse returns them.
static bool PutHeadParams(uint8_t transformId, const std::vector<StreamHeaderParam>& params, std::vector<uint8_t>& head,
    std::vector<StreamHeaderParam>& sorted)
{
    size_t code = 0;
    while (code < sizeof(kStreamHeaderTransforms) && kStreamHeaderTransforms[code] != transformId) code++;
    if (code == sizeof(kStreamHeaderTransforms)) return false;

    const StreamHeaderParam* byId[sizeof(kStreamHeaderParamSize)] = {};
    uint8_t present = 0;
    for (const StreamHeaderParam& p : params)
    {
        if (p.Id == 0 || p.Id >= sizeof(kStreamHeaderParamSize) || byId[p.Id] || p.Value.size() != kStreamHeaderParamSize[p.Id]) return false;
        byId[p.Id] = &p;
        present |= uint8_t(1 << (p.Id - 1));
    }
    head = { uint8_t(code | (present << 3)) };
    sorted.clear();
    for (const StreamHeaderParam* p : byId)
    {
        if (p)
        {
            head.insert(head.end(), p->Value.begin(), p->Value.end());
            sorted.push_back(*p);
        }
    }
    return true;
}

// Reads the parameter part of the header; o receives the offset of the removed-block descriptor
static bool GetHeadParams(const uint8_t* src, size_t srcSize, StreamHeaderInfo& info, size_t& o)
{
    if (srcSize < GACL_STREAMHEADER_FIXED_BYTES || (src[0] & GACL_STREAMHEADER_ESCAPE) != 0)
    {
        return false;
    }
    const size_t code = src[0] & 7;
    if (code >= sizeof(kStreamHeaderTransforms)) return false;
    info.TransformId = kStreamHeaderTransforms[code];

    o = GACL_STREAMHEADER_FIXED_BYTES;
    for (uint8_t id = 1; id < sizeof(kStreamHeaderParamSize); id++)
    {
        if (((src[0] >> (id + 2)) & 1) == 0) continue;
        const size_t s = kStreamHeaderParamSize[id];
        if (o + s > srcSize) return false;
        info.Params.push_back({ id, std::vector<uint8_t>(src + o, src + o + s) });
        o += s;
    }
    return true;
}

bool StreamHeader_Encode(const uint8_t* src, size_t srcSize, size_t blockSize, uint8_t transformId,
    const std::vector<StreamHeaderParam>& params, std::vector<uint8_t>& dest, StreamHeaderInfo* infoOut,
    const StreamHeaderBlockCode* blockCode)
{
    dest.clear();
    if (src == nullptr || !ValidBlockStream(srcSize, blockSize)) return false;
    const size_t n = srcSize / blockSize;

    std::vector<uint8_t> head;
    std::vector<StreamHeaderParam> sortedParams;
    if (!PutHeadParams(transformId, params, head, sortedParams)) return false;

    // Link each block to the previous and next block with identical content
    std::vector<uint32_t> prev(n), next(n, kNone);
    {
        std::unordered_map<BlockKey, uint32_t, BlockKeyHash> last;
        last.reserve(n);
        for (size_t i = 0; i < n; i++)
        {
            auto ins = last.try_emplace(ReadKey(src + i * blockSize, blockSize), uint32_t(i));
            if (ins.second)
            {
                prev[i] = kNone;
            }
            else
            {
                prev[i] = ins.first->second;
                next[ins.first->second] = uint32_t(i);
                ins.first->second = uint32_t(i);
            }
        }
    }

    // Greedy: remove the earliest block that has a kept copy, sourcing it from the nearest kept copy.
    // Sources are pinned so they are never removed. Once the duplicates run out, code the cheapest remaining
    // blocks into the header. Stop once the header fits in the removed blocks; then rechoose that many coded blocks
    // to fit on as few decoder thread groups as possible (GroupCodedBlocks).
    enum : uint8_t { Free = 0, Removed = 1, Pinned = 2, Coded = 3 };
    std::vector<uint8_t> state(n, Free);
    std::vector<size_t> removed, sources, coded;
    BlockCode code;
    std::vector<std::pair<size_t, size_t>> candidates;      // (bits, index) of codable blocks, cheapest first
    size_t nextCandidate = 0;
    bool duplicatesLeft = true;
    const unsigned posBits = FloorLog2(n - 1) + 1;
    size_t entryBits = 0, codedBits = 0;
    size_t p = 0;
    for (;;)
    {
        const size_t k = removed.size() + coded.size();
        if (k > 0 && head.size() + (ExpGolombBits(k - 1) + ExpGolombBits(coded.size()) + entryBits + 7) / 8 <= k * blockSize)
        {
            break;
        }
        if (k + 1 >= n) return false;

        if (duplicatesLeft)
        {
            size_t q = kNone;
            for (; p < n; p++)
            {
                if (state[p] != Free) continue;

                // every removed block precedes p, so the next copy is always kept
                const uint32_t fwd = next[p];
                uint32_t back = prev[p];
                while (back != kNone && state[back] == Removed) back = prev[back];

                if (fwd != kNone && (back == kNone || fwd - p <= p - back))
                {
                    q = fwd;
                }
                else if (back != kNone)
                {
                    q = back;
                }
                else
                {
                    continue;
                }
                break;
            }
            if (q != kNone)
            {
                const size_t gap = removed.empty() ? p : p - removed.back() - 1;
                const size_t dist = q > p ? 2 * (q - p - 1) : 2 * (p - q - 1) + 1;
                entryBits += ExpGolombBits(gap) + ExpGolombBits(dist);
                removed.push_back(p);
                sources.push_back(q);
                state[p] = Removed;
                state[q] = Pinned;
                p++;
                continue;
            }

            duplicatesLeft = false;
            if (BuildBlockCode(blockCode, blockSize, code))
            {
                uint8_t tmp[16];
                for (size_t i = 0; i < n; i++)
                {
                    if (state[i] != Free) continue;
                    const uint8_t* b = CodedForm(src + i * blockSize, code, sortedParams, tmp);
                    if (BlockClass(b, code) < 8)
                    {
                        candidates.push_back({ CodedBlockBits(b, code, posBits), i });
                    }
                }
                std::sort(candidates.begin(), candidates.end());
            }
        }

        if (nextCandidate >= candidates.size()) return false;
        entryBits += candidates[nextCandidate].first;
        codedBits += candidates[nextCandidate].first;
        coded.push_back(candidates[nextCandidate].second);
        state[coded.back()] = Coded;
        nextCandidate++;
    }
    const size_t k = removed.size() + coded.size();
    if (coded.size() > 1)
    {
        // the same number of coded blocks within the same room, on as few decoder thread groups as possible
        const size_t room = 8 * (k * blockSize - head.size()) - ExpGolombBits(k - 1) - ExpGolombBits(coded.size());
        for (size_t i : coded) state[i] = Free;
        GroupCodedBlocks(candidates, n, transformId, room - (entryBits - codedBits), coded);
        for (size_t i : coded) state[i] = Coded;
    }
    std::sort(coded.begin(), coded.end());

    BitWriter w;
    w.PutExpGolomb(k - 1);
    w.PutExpGolomb(coded.size());
    for (size_t j = 0; j < removed.size(); j++)
    {
        const size_t r = removed[j], q = sources[j];
        w.PutExpGolomb(j == 0 ? r : r - removed[j - 1] - 1);
        w.PutExpGolomb(q > r ? 2 * (q - r - 1) : 2 * (r - q - 1) + 1);
    }
    // the positions of the coded blocks, then their bodies in the same order
    for (size_t i : coded)
    {
        w.Put(i, posBits);
    }
    for (size_t i : coded)
    {
        uint8_t tmp[16];
        PutCodedBody(w, CodedForm(src + i * blockSize, code, sortedParams, tmp), code);
    }

    dest.assign(srcSize, 0);
    memcpy(dest.data(), head.data(), head.size());
    memcpy(dest.data() + head.size(), w.Bytes.data(), w.Bytes.size());

    size_t o = k;
    for (size_t i = 0; i < n; i++)
    {
        if (state[i] != Removed && state[i] != Coded)
        {
            memcpy(dest.data() + o * blockSize, src + i * blockSize, blockSize);
            o++;
        }
    }

    if (infoOut)
    {
        infoOut->TransformId = transformId;
        infoOut->Params = sortedParams;
        infoOut->HeaderBytes = head.size() + w.Bytes.size();
        infoOut->HeaderBlocks = k;
        infoOut->Removed = removed;
        infoOut->Sources = sources;
        infoOut->Coded = coded;
        infoOut->CodedBlocks.clear();
        for (size_t i : coded)
        {
            infoOut->CodedBlocks.insert(infoOut->CodedBlocks.end(), src + i * blockSize, src + (i + 1) * blockSize);
        }
    }
    return true;
}

bool StreamHeader_Parse(const uint8_t* src, size_t srcSize, size_t blockSize, StreamHeaderInfo& info,
    const StreamHeaderBlockCode* blockCode)
{
    info = {};
    if (src == nullptr || !ValidBlockStream(srcSize, blockSize)) return false;
    const size_t n = srcSize / blockSize;

    size_t o = 0;
    if (!GetHeadParams(src, srcSize, info, o)) return false;

    BitReader r = { src + o, (srcSize - o) * 8 };
    uint64_t km1, c;
    if (!r.GetExpGolomb(km1) || km1 + 1 >= n || !r.GetExpGolomb(c) || c > km1 + 1) return false;
    const size_t k = size_t(km1) + 1;

    for (size_t j = 0; j < k - c; j++)
    {
        uint64_t gap, dist;
        if (!r.GetExpGolomb(gap) || !r.GetExpGolomb(dist)) return false;
        const uint64_t idx = j == 0 ? gap : uint64_t(info.Removed.back()) + 1 + gap;
        const uint64_t d = (dist >> 1) + 1;
        if (idx >= n || ((dist & 1) ? d > idx : idx + d >= n)) return false;
        info.Removed.push_back(size_t(idx));
        info.Sources.push_back(size_t((dist & 1) ? idx - d : idx + d));
    }

    if (c > 0)
    {
        BlockCode code;
        if (!BuildBlockCode(blockCode, blockSize, code)) return false;
        const unsigned posBits = FloorLog2(n - 1) + 1;
        info.CodedBlocks.resize(size_t(c) * blockSize);
        for (size_t j = 0; j < c; j++)
        {
            uint64_t idx;
            if (!r.Get(posBits, idx) || idx >= n || (j > 0 && idx <= info.Coded.back()) ||
                std::binary_search(info.Removed.begin(), info.Removed.end(), size_t(idx)))
            {
                return false;
            }
            info.Coded.push_back(size_t(idx));
        }
        for (size_t j = 0; j < c; j++)
        {
            uint8_t* block = info.CodedBlocks.data() + j * blockSize;
            if (!GetCodedBlock(r, code, block)) return false;
            if (code.Map) code.Map(block, true, info.Params);
        }
    }

    info.HeaderBytes = o + (r.Pos + 7) / 8;
    info.HeaderBlocks = k;
    if (info.HeaderBytes > k * blockSize) return false;

    // canonical form: unused descriptor bits and the padding are zero
    uint64_t tail;
    if ((r.Pos & 7) && (!r.Get(unsigned(8 - (r.Pos & 7)), tail) || tail != 0)) return false;
    for (size_t b = info.HeaderBytes; b < k * blockSize; b++)
    {
        if (src[b] != 0) return false;
    }

    // sources must be kept blocks
    for (size_t s : info.Sources)
    {
        if (std::binary_search(info.Removed.begin(), info.Removed.end(), s) ||
            std::binary_search(info.Coded.begin(), info.Coded.end(), s))
        {
            return false;
        }
    }
    return true;
}

size_t StreamHeader_MapBlock(const StreamHeaderInfo& info, size_t block)
{
    if (std::binary_search(info.Coded.begin(), info.Coded.end(), block))
    {
        return SIZE_MAX;
    }
    const size_t k = info.Removed.size();
    for (size_t j = 0; j < k; j++)
    {
        if (info.Removed[j] == block)
        {
            block = info.Sources[j];
            break;
        }
    }
    size_t shift = 0;
    for (size_t j = 0; j < k; j++)
    {
        shift += info.Removed[j] > block ? 1 : 0;
    }
    for (size_t r : info.Coded)
    {
        shift += r > block ? 1 : 0;
    }
    return block + shift;
}

bool StreamHeader_Decode(const uint8_t* src, size_t srcSize, size_t blockSize, StreamHeaderInfo& info, std::vector<uint8_t>& dest,
    const StreamHeaderBlockCode* blockCode)
{
    dest.clear();
    if (!StreamHeader_Parse(src, srcSize, blockSize, info, blockCode)) return false;

    const size_t n = srcSize / blockSize;
    dest.resize(srcSize);
    size_t c = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (c < info.Coded.size() && info.Coded[c] == i)
        {
            memcpy(dest.data() + i * blockSize, info.CodedBlocks.data() + c * blockSize, blockSize);
            c++;
            continue;
        }
        memcpy(dest.data() + i * blockSize, src + StreamHeader_MapBlock(info, i) * blockSize, blockSize);
    }
    return true;
}

const StreamHeaderParam* StreamHeader_FindParam(const StreamHeaderInfo& info, uint8_t id)
{
    for (const StreamHeaderParam& p : info.Params)
    {
        if (p.Id == id) return &p;
    }
    return nullptr;
}
