//-------------------------------------------------------------------------------------
// ClusterBlocks_BruteForce.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#include "ClusterBlocks_Common.h"
#include <vector>
#include <cstddef>
#include <cstdint>
#include <ppl.h>

template<bool UseScaledDist = false>
struct ClusterBlocks_BruteForce {

	class BitSet {
		std::vector<uint64_t> data;
		size_t numBits;

		static constexpr size_t BITS_PER_WORD = 64;

	public:
		BitSet(size_t bits = 0) : numBits(bits), data((bits + BITS_PER_WORD - 1) / BITS_PER_WORD, 0) {}

		// Clear all bits and reset size to 0
		void clear() {
			data.clear();
			numBits = 0;
		}

		// Resize the bitset
		void resize(size_t newSize) {
			numBits = newSize;
			data.resize((newSize + BITS_PER_WORD - 1) / BITS_PER_WORD, 0);
		}

		// Read-only access
		bool operator [](size_t index) const {
			return (data[index / BITS_PER_WORD] >> (index & 63)) & 1;
		}

		// Set bit to 1
		void set1(size_t index) {
			data[index / BITS_PER_WORD] |= (1ULL << (index & 63));
		}

		// Set bit to 0
		void set0(size_t index) {
			data[index / BITS_PER_WORD] &= ~(1ULL << (index & 63));
		}

		// Set bit to given value
		void set(size_t index, bool value) {
			if (value)
				set1(index);
			else
				set0(index);
		}

		// Optional: get current size
		size_t size() const {
			return numBits;
		}
	};

	// Treat window size as a block size instead.
	// Process blocks concurrently for faster compression.
	bool UseConcurrent = true;				// Set to false if you don't want this

	uint32_t numBlocks;
	Vec64u8* blocks;						// Map from blockIndex to Vec64u8 block, used for accurate distance tests.
	std::vector<Vec8f> blockSummaries;		// Map from blockIndex to Vec8f summary of block used for lower bounds distance tests.
	uint32_t window;						// If blockIndex j-i >= window, i can't be nearest neighbor of j.
	float distanceLimit;					// We don't need nearest neighbors with a distance >= distanceLimit
	BitSet paired;							// For blockIndex, whether it has been paired with another block.

	uint64_t* encodedData;
	uint32_t encodedStride64;

	uint32_t mergeCnt;
	float worstDist;
	float totalDist;

	ClusterBlocks_BruteForce():
		numBlocks(0),
		blocks(nullptr),
		window(0),
		distanceLimit(0),
		paired(0),
		encodedData(0),
		encodedStride64(0),
		mergeCnt(0),
		worstDist(0),
		totalDist(0) {}


	void Initialize(
		uint32_t _numBlocks,
		Vec64u8* _blocks,
		uint64_t* _encodedData, uint32_t _encodedStride64,
		uint32_t _window, float _distanceLimit)
	{
		// Window is in units of blocks.
		_window &= ~63; // Require window to be a multiple of 64 (for multi threaded access to paired).

		numBlocks = _numBlocks;
		blocks = _blocks;
		encodedData = _encodedData;
		encodedStride64 = _encodedStride64;
		window = _window;
		distanceLimit = _distanceLimit;

		blockSummaries.clear();
		blockSummaries.resize(numBlocks);
		for (uint32_t i = 0; i < numBlocks; i++)
			blockSummaries[i] = blocks[i].GetSummary();

		paired.clear();
		paired.resize(numBlocks);

		mergeCnt = 0;
		worstDist = 0.0f;
		totalDist = 0.0f;
	}

	void MergeBlockRange(
		uint32_t blockStart, uint32_t blockEnd, float curDistanceLimit,
		uint32_t &localMergeCnt, float &localWorstDist, float &localTotalDist)
	{
		for (uint32_t idx1 = blockStart + 1; idx1 < blockEnd; idx1++)
		{
			if (paired[idx1])
				continue;

			Vec64u8& block1 = blocks[idx1];
			Vec8f& summary1 = blockSummaries[idx1];

			float best = curDistanceLimit;
			uint32_t bestIdx = (uint32_t)-1;

			uint32_t base = idx1 >= window+blockStart ? idx1+1 - window : blockStart;
			for (uint32_t idx2 = base; idx2 < idx1; idx2++)
			{
				Vec64u8& block2 = blocks[idx2];
				Vec8f& summary2 = blockSummaries[idx2];

				float minDist = Vec8f::DistanceSquared<UseScaledDist>(summary1, summary2);
				if (minDist > best)
					continue; // This block can't improve best.
				
				float dist = Vec64u8::DistanceSquared<UseScaledDist>(block1, block2, summary1, summary2);
				// Allow dist==best here, that will cause a closer bestIdx to be used in that case.
				if (dist <= best)
				{
					best = dist;
					bestIdx = idx2;
				}
			}
			if (best < curDistanceLimit)
			{
				// Copy color data and summary data
				block1 = blocks[bestIdx];
				summary1 = blockSummaries[bestIdx];
				// Copy encoded data
				if (encodedStride64 == 1)
				{
					encodedData[idx1] = encodedData[bestIdx];
				}
				else // encodedStride64 == 2
				{
					encodedData[idx1 * 2] = encodedData[bestIdx * 2];
					encodedData[idx1 * 2 + 1] = encodedData[bestIdx * 2 + 1];
				}
				// flag blocks have having been paired
				paired.set1(idx1);
				paired.set1(bestIdx);

				localMergeCnt++;
				localWorstDist = std::max(localWorstDist, best);
				localTotalDist += best;
			}
		}
	}

	void MergeBlocks(float curDistanceLimit)
	{
		if (UseConcurrent)
		{
			concurrency::combinable<uint32_t> mergeCnts;
			concurrency::combinable<float> worstDists([=] {return worstDist; });
			concurrency::combinable<float> totalDists;

			uint32_t numSegments = (numBlocks + window - 1) / window;
			concurrency::parallel_for(uint32_t(0), numSegments,
				[&](uint32_t segment)
				{
					uint32_t startBlock = segment * window;
					uint32_t endBlock = std::min(startBlock + window, numBlocks);
					uint32_t localMergeCnt = 0;
					float localWorstDist = worstDist;
					float localTotalDist = 0.0f;
					MergeBlockRange(
						startBlock, endBlock, curDistanceLimit,
						localMergeCnt, localWorstDist, localTotalDist
						);
					mergeCnts.local() = localMergeCnt;
					worstDists.local() = localWorstDist;
					totalDists.local() = localTotalDist;
				}
			);

			mergeCnt += mergeCnts.combine([](uint32_t a, uint32_t b) { return a + b; });
			worstDist = worstDists.combine([](float a, float b) { return std::max(a, b); });
			totalDist += totalDists.combine([](float a, float b) { return a + b; });
		}
		else
		{
			MergeBlockRange(
				0, numBlocks, curDistanceLimit,
				mergeCnt, worstDist, totalDist);
		}
	}

	void ClusterBlocks(
		uint32_t _numBlocks,
		uint64_t* _encodedData, uint32_t _encodedStride64,
		Vec64u8* colorData,
		uint32_t k,
		float _distanceLimit,
		float avgDistLimit,
		uint32_t _window, // window is in blocks here
		uint32_t& final_k,
		float& final_largestDist,
		float& final_avgDist
	)
	{
		Initialize(_numBlocks, colorData, _encodedData, _encodedStride64, _window, _distanceLimit);

		float startDistance = UseScaledDist ? 8.0f : 64.0f;
		float curDistanceLimit = std::min(startDistance, _distanceLimit);
		for (;;)
		{
			MergeBlocks(curDistanceLimit);
			if (mergeCnt > k ||
				curDistanceLimit >= _distanceLimit ||
				totalDist >= numBlocks * avgDistLimit)
				break;
			curDistanceLimit *= 1.4142f; // sqrt(2)... twice as many steps as 2, may reach target with less error
		}

		final_k = mergeCnt;
		final_largestDist = worstDist;
		final_avgDist = totalDist / numBlocks;

	}
};
