//-------------------------------------------------------------------------------------
// CluserBlocks_Common.h
//
// Game Asset Conditioning Library - Microsoft toolkit for game asset compression
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
//-------------------------------------------------------------------------------------

#pragma once

#ifndef USE_DEBUG_PRINTF
#define USE_DEBUG_PRINTF 0
#endif

#include <vadefs.h>
#if USE_DEBUG_PRINTF
#include <debugapi.h>
void DebugPrintf(const char* format, ...)
{
	constexpr size_t bufferSize = 1024;
	char buffer[bufferSize];

	va_list args;
	va_start(args, format);
	vsnprintf_s(buffer, bufferSize, _TRUNCATE, format, args);
	va_end(args);

	OutputDebugStringA(buffer);
}
#else
void DebugPrintf(const char* format, ...) { format; }
#endif

//--

#include <stdint.h>
#include <intrin.h>
#include <immintrin.h>

// These are internal helpers for the following code.
#define _mm_GetDistScaleSq(sd_min, scale_sq) \
	__m128 denom = _mm_add_ps(sd_min, _mm_set1_ps(1.0f)); \
	__m128 scale_sq = _mm_rcp_ps(denom);
#define _mm256_GetDistScale(sd_min,scale) \
	__m256 denom = _mm256_add_ps(sd_min, _mm256_set1_ps(1.0f)); \
	__m256 scale = _mm256_rsqrt_ps(denom);

// In the following, there are two available measures of distance:
//  An absolute sum of squares distance (DistanceSquared<false>)
//  and a relative version scaled by 1/(1 + 4 * standard deviation)
//  (DistanceSquared<true>).

// Vec8f holds 4x the standard deviation and 4x the mean
//  of 16 pixels.  It is created by Vec64u8::GetSummary().
// The distance between two Vec8f's is a lower bound on the
//  distance between two Vec64u8's.
struct Vec8f {
	__m256 data;

	Vec8f() = default;

	explicit Vec8f(const float* src) { data = _mm256_loadu_ps(src); }

	// Note: This is lower bound on Vec64u8 distance
	template<bool UseScaledDist=false>
	static float DistanceSquared(const Vec8f& a, const Vec8f& b)
	{
		__m256 diff = _mm256_sub_ps(a.data, b.data);
		if (UseScaledDist)
		{
			__m256 sd_min = _mm256_min_ps(a.data, b.data);
			sd_min = _mm256_permute2f128_ps(sd_min, sd_min, 0x00); // Replicate sd into both halves
			_mm256_GetDistScale(sd_min, scale);
			diff = _mm256_mul_ps(diff, scale);
		}
		__m256 sq = _mm256_mul_ps(diff, diff);
		__m128 low = _mm256_castps256_ps128(sq);
		__m128 high = _mm256_extractf128_ps(sq, 1);
		__m128 sum = _mm_add_ps(low, high);
		sum = _mm_hadd_ps(sum, sum);
		sum = _mm_hadd_ps(sum, sum);
		return _mm_cvtss_f32(sum);
	}

	static Vec8f Min(const Vec8f& a, const Vec8f& b)
	{
		return Vec8f{ _mm256_min_ps(a.data, b.data) };
	}

	static Vec8f Max(const Vec8f& a, const Vec8f& b)
	{
		return Vec8f{ _mm256_max_ps(a.data, b.data) };
	}

	bool operator==(const Vec8f& other) const
	{
		return 0xFF == _mm256_movemask_ps(_mm256_cmp_ps(data, other.data, _CMP_EQ_OQ));
	}

	explicit Vec8f(__m256 d) : data(d) {}
};

// BoundingBox8f represents a bounding box of Vec8f values.
struct BoundingBox8f {
	Vec8f center;
	Vec8f radius;

	BoundingBox8f() = default;

	// Constructor from lower and upper bounds
	BoundingBox8f(const Vec8f& lower, const Vec8f& upper)
	{
		__m256 lo = lower.data;
		__m256 hi = upper.data;
		center.data = _mm256_mul_ps(_mm256_add_ps(lo, hi), _mm256_set1_ps(0.5f));
		radius.data = _mm256_mul_ps(_mm256_sub_ps(hi, lo), _mm256_set1_ps(0.5f));
	}

	// Constructor from two other bounding boxes
	BoundingBox8f(const BoundingBox8f& boxA, const BoundingBox8f& boxB)
	{
		__m256 lo = _mm256_min_ps(
			_mm256_sub_ps(boxA.center.data, boxA.radius.data),
			_mm256_sub_ps(boxB.center.data, boxB.radius.data));
		__m256 hi = _mm256_max_ps(
			_mm256_add_ps(boxA.center.data, boxA.radius.data),
			_mm256_add_ps(boxB.center.data, boxB.radius.data));
		center.data = _mm256_mul_ps(_mm256_add_ps(lo, hi), _mm256_set1_ps(0.5f));
		radius.data = _mm256_mul_ps(_mm256_sub_ps(hi, lo), _mm256_set1_ps(0.5f));
	}


	bool operator==(const BoundingBox8f& other) const
	{
		return center == other.center && radius == other.radius;
	}

	// Minimum squared distance between bounding boxes
	template<bool UseScaledDist=false>
	static float MinDistanceSquared(const BoundingBox8f& a, const BoundingBox8f& b)
	{
		__m256 delta = _mm256_sub_ps(a.center.data, b.center.data);
		__m256 abs_delta = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), delta); // abs(x)
		__m256 radius_sum = _mm256_add_ps(a.radius.data, b.radius.data);
		__m256 excess = _mm256_sub_ps(abs_delta, radius_sum);
		__m256 clamped = _mm256_max_ps(excess, _mm256_setzero_ps());
		if (UseScaledDist)
		{
			__m256 a_sd = _mm256_add_ps(a.center.data, a.radius.data); // upper bound
			__m256 b_sd = _mm256_add_ps(b.center.data, b.radius.data);
			__m256 sd_min = _mm256_min_ps(a_sd, b_sd);
			sd_min = _mm256_permute2f128_ps(sd_min, sd_min, 0x00); // Replicate sd into both halves
			_mm256_GetDistScale(sd_min, scale);
			clamped = _mm256_mul_ps(clamped, scale);
		}
		__m256 sq = _mm256_mul_ps(clamped, clamped);

		__m128 low = _mm256_castps256_ps128(sq);
		__m128 high = _mm256_extractf128_ps(sq, 1);
		__m128 sum = _mm_add_ps(low, high);
		sum = _mm_hadd_ps(sum, sum);
		sum = _mm_hadd_ps(sum, sum);
		return _mm_cvtss_f32(sum);
	}

	// Minimum squared distance from bounding box to Vec8f
	// It is nearly the same code as for squared distance to another bounding box
	// The Vec8f acts like a bounding box center, with a radius of zero.  Saves one instruction.
	template<bool UseScaledDist=false>
	static float MinDistanceSquared(const BoundingBox8f& a, const Vec8f& b)
	{
		__m256 delta = _mm256_sub_ps(a.center.data, b.data);
		__m256 abs_delta = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), delta); // abs(x)
		__m256 excess = _mm256_sub_ps(abs_delta, a.radius.data);
		__m256 clamped = _mm256_max_ps(excess, _mm256_setzero_ps());
		if (UseScaledDist)
		{
			__m256 box_sd = _mm256_add_ps(a.center.data, a.radius.data); // upper bound
			__m256 sd_min = _mm256_min_ps(box_sd, b.data);
			sd_min = _mm256_permute2f128_ps(sd_min, sd_min, 0x00); // Replicate sd into both halves
			_mm256_GetDistScale(sd_min, scale);
			clamped = _mm256_mul_ps(clamped, scale);
		}
		__m256 sq = _mm256_mul_ps(clamped, clamped);

		__m128 low = _mm256_castps256_ps128(sq);
		__m128 high = _mm256_extractf128_ps(sq, 1);
		__m128 sum = _mm_add_ps(low, high);
		sum = _mm_hadd_ps(sum, sum);
		sum = _mm_hadd_ps(sum, sum);
		return _mm_cvtss_f32(sum);
	}

	int8_t GetMajorAxis() const
	{
		__m256 data = radius.data;

		// Step 1: Reduce to max value
		__m128 low = _mm256_castps256_ps128(data);         // lower 128 bits
		__m128 high = _mm256_extractf128_ps(data, 1);      // upper 128 bits
		__m128 max128 = _mm_max_ps(low, high);             // max of low and high

		__m128 shuf = _mm_movehdup_ps(max128);             // duplicate high halves
		__m128 max1 = _mm_max_ps(max128, shuf);
		shuf = _mm_movehl_ps(shuf, max1);
		__m128 max2 = _mm_max_ss(max1, shuf);
		float max_val = _mm_cvtss_f32(max2);

		// Step 2: Compare to find index
		__m256 max_vec = _mm256_set1_ps(max_val);
		__m256 cmp = _mm256_cmp_ps(data, max_vec, _CMP_EQ_OQ);  // compare for equality
		unsigned int mask = _mm256_movemask_ps(cmp);            // bitmask of matches, must be at least one

		// Step 3: Find first set bit (index of max)
		unsigned long index;
		_BitScanForward(&index, mask);
		return (int8_t)index;
	}
};

struct Vec64u8 {
	__m256i data[2];

	// Load 4 rows of 4 8-bit rgba colors
	void LoadFourRows(const uint32_t* p0, const uint32_t* p1, const uint32_t* p2, const uint32_t* p3)
	{
		// Load 16 bytes from each pointer
		__m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p0));
		__m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p1));
		__m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p2));
		__m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p3));

		// Combine into two __m256i registers
		data[0] = _mm256_inserti128_si256(_mm256_castsi128_si256(a), b, 1); // [p0 | p1]
		data[1] = _mm256_inserti128_si256(_mm256_castsi128_si256(c), d, 1); // [p2 | p3]
	}

	void StoreFourRows(uint32_t* p0, uint32_t* p1, uint32_t* p2, uint32_t* p3) const
	{
		// Extract 128-bit halves
		__m128i a = _mm256_castsi256_si128(data[0]);              // lower 128 bits of data[0]
		__m128i b = _mm256_extracti128_si256(data[0], 1);         // upper 128 bits of data[0]
		__m128i c = _mm256_castsi256_si128(data[1]);              // lower 128 bits of data[1]
		__m128i d = _mm256_extracti128_si256(data[1], 1);         // upper 128 bits of data[1]

		// Store to memory
		_mm_storeu_si128(reinterpret_cast<__m128i*>(p0), a);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(p1), b);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(p2), c);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(p3), d);
	}

	// distance_squared of this Vec8f is a lower bound on distance_squared of Vec64u8
	// returns 4.0f * (vec4 standard_deviation, vec4 mean)
	// We use this because the sum of squares distance of two of these is (mA4-mB4)^2+(sdA4-sdB4)^2 = 16*(mA-mB)^2 + 16*(sdA-sdB)^2
	//  which is the lower bound of sum of squares diff between A and B (for 16 values).
	// Two extreme cases to see this are A and B all a's and b's, and A and B half a's and b's, and half -a's and -b's.
	Vec8f GetSummary() const
	{
		__m256i bytes;

		//---
		// Get __m128i sum = (sumR,sumG,sumB,sumA)

		const __m256i reorder1 = _mm256_setr_epi8(
			0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15,
			0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15);

		const __m256i reorder2 = _mm256_setr_epi32(0, 2, 1, 3, 4, 6, 5, 7);

		// Load 32 bytes (8 RGBA pixels) (rgba0-7)
		bytes = data[0];
		// Rearrange to be (r0-3,g0-3, b0-3,a0-3, r4-7,g4-7, b4-7,a4-7)
		bytes = _mm256_shuffle_epi8(bytes, reorder1);
		// Rearrange to be (r0-3,g0-3, r4-7,g4-7, b0-3,a0-3, b4-7,a4-7)
		bytes = _mm256_permute4x64_epi64(bytes, _MM_SHUFFLE(3, 1, 2, 0)); // Note _MM_SHUFFLE values are read right to left!
		// Rearrange to be (r0-7, g0-7, b0-7, a0-7)
		bytes = _mm256_permutevar8x32_epi32(bytes, reorder2);
		// Convert to (sumr, sumg, sumb, suma), each sum is in 64 bits (even though it only needs 16-bits)
		__m256i sum0 = _mm256_sad_epu8(bytes, _mm256_setzero_si256());

		// Load next 32 bytes (8 RGBA pixels) (rgba0-7)
		bytes = data[1];
		// Rearrange to be (r0-3,g0-3, b0-3,a0-3, r4-7,g4-7, b4-7,a4-7)
		bytes = _mm256_shuffle_epi8(bytes, reorder1);
		// Rearrange to be (r0-3,g0-3, r4-7,g4-7, b0-3,a0-3, b4-7,a4-7)
		bytes = _mm256_permute4x64_epi64(bytes, _MM_SHUFFLE(3, 1, 2, 0)); // Note _MM_SHUFFLE values are read right to left!
		// Rearrange to be (r0-7, g0-7, b0-7, a0-7)
		bytes = _mm256_permutevar8x32_epi32(bytes, reorder2);
		// Convert to (sumr, sumg, sumb, suma), each sum is in 64 bits (even though it only needs 16-bits)
		__m256i sum1 = _mm256_sad_epu8(bytes, _mm256_setzero_si256());

		// 32-bit (sumr,sumg,sumb,suma) for all 16 pixels.
		__m128i sum = _mm256_castsi256_si128(
			_mm256_permutevar8x32_epi32(
				_mm256_add_epi64(sum0, sum1),
				_mm256_setr_epi32(0, 2, 4, 6, 0, 0, 0, 0)));

		//--
		// Get __m128i sum_sq = (sumsqR,sumsqG,sumsqB,sumsqA)

		const char z = -128;

		// 8-bit (rgba0-7) -> 16-bit (r0-1,g0-1,b0-1,a0-1,r4-5,g4-5,b4-5,a4-5)
		const __m256i unpack8to16_1 = _mm256_setr_epi8(
			0, z, 4, z, 1, z, 5, z, 2, z, 6, z, 3, z, 7, z,
			0, z, 4, z, 1, z, 5, z, 2, z, 6, z, 3, z, 7, z);
		// 8-bit (rgba0-7) -> 16-bit (r2-3,g2-3,b2-3,a2-3,r6-7,g6-7,b6-7,a6-7)
		const __m256i unpack8to16_2 = _mm256_setr_epi8(
			8, z, 12, z, 9, z, 13, z, 10, z, 14, z, 11, z, 15, z,
			8, z, 12, z, 9, z, 13, z, 10, z, 14, z, 11, z, 15, z);

		// Load 32 bytes (8 RGBA pixels) (rgba0-7)
		// Rearrange to 16-bit (r0-1,g0-1,b0-1,a0-1,r4-5,g4-5,b4-5,a4-5)
		bytes = _mm256_shuffle_epi8(data[0], unpack8to16_1);
		// Obtain 32-bit (r0^2+r1^2, g0^2+g1^2, b0^2+b1^2,  a0^2+a1^2, r4^2+r5^2, g4^2+g5^2, b4^2+b5^2, a4^2+a5^2)
		__m256i sum_sq2 = _mm256_madd_epi16(bytes, bytes);
		// Rearrange to 16-bit (r2-3,g2-3,b2-3,a2-3,r6-7,g6-7,b6-7,a6-7)
		bytes = _mm256_shuffle_epi8(data[0], unpack8to16_2);
		// Accumlate 32-bit (r2^2+r3^2, g2^2+g3^2, b2^2+b3^2, a2^2+a3^2,  r6^2+r7^2, g6^2+g7^2, b6^2+b7^2, a6^2+a7^2)
		sum_sq2 = _mm256_add_epi32(sum_sq2, _mm256_madd_epi16(bytes, bytes));

		// Load next 32 bytes (8 RGBA pixels) (rgba0-7)
		// Rearrange to 16-bit (r0-1,g0-1,b0-1,a0-1,r4-5,g4-5,b4-5,a4-5)
		bytes = _mm256_shuffle_epi8(data[1], unpack8to16_1);
		// Accumulate 32-bit (r0^2+r1^2, g0^2+g1^2, b0^2+b1^2,  a0^2+a1^2, r4^2+r5^2, g4^2+g5^2, b4^2+b5^2, a4^2+a5^2)
		sum_sq2 = _mm256_add_epi32(sum_sq2, _mm256_madd_epi16(bytes, bytes));
		// Rearrange to 16-bit (r2-3,g2-3,b2-3,a2-3,r6-7,g6-7,b6-7,a6-7)
		bytes = _mm256_shuffle_epi8(data[1], unpack8to16_2);
		// Accumlate 32-bit (r2^2+r3^2, g2^2+g3^2, b2^2+b3^2, a2^2+a3^2,  r6^2+r7^2, g6^2+g7^2, b6^2+b7^2, a6^2+a7^2)
		sum_sq2 = _mm256_add_epi32(sum_sq2, _mm256_madd_epi16(bytes, bytes));

		// Add sumsqRGBA0-3 and sumsqRGBA4-7 to get 32-bit (sumsqR,sumsqG,sumsqB,sumsqA)
		__m128i sum_sq = _mm_add_epi32(
			_mm256_castsi256_si128(sum_sq2),
			_mm256_extracti128_si256(sum_sq2, 1)
		);

		//--
		// Using sum and sq_sq, get final standard deviation and mean and pack them together

		// Convert to float
		__m128 sum_f = _mm_cvtepi32_ps(sum);
		__m128 sq_f = _mm_cvtepi32_ps(sum_sq);

		// Compute mean4 and stddev4
		__m128 mean16 = sum_f;							// 16 x mean = sum
		__m128 sum_f_sq = _mm_mul_ps(sum_f, sum_f);		// 16 x sd = sqrt(16*sq - sum^2)
		__m128 stddev16 = _mm_sqrt_ps(_mm_fmsub_ps(_mm_set1_ps(16.0f), sq_f, sum_f_sq));

		// Combine stddev16 and mean16 into Vec8f and scale by 1/4 to make them stddev4 and mean4.
		__m256 result = _mm256_castps128_ps256(stddev16);
		result = _mm256_insertf128_ps(result, mean16, 1);
		result = _mm256_mul_ps(result, _mm256_set1_ps(0.25f));
		return Vec8f(result);
	}

	template<bool UseScaledDist=false>
	static float DistanceSquared(
		const Vec64u8& a, const Vec64u8& b,
		const Vec8f a_summary=Vec8f(_mm256_setzero_ps()),
		const Vec8f b_summary=Vec8f(_mm256_setzero_ps()))
	{
		const __m256i zero = _mm256_setzero_si256();

		// Unpack 8-bit to 16-bit
		__m256i a0_lo = _mm256_unpacklo_epi8(a.data[0], zero);
		__m256i a0_hi = _mm256_unpackhi_epi8(a.data[0], zero);
		__m256i a1_lo = _mm256_unpacklo_epi8(a.data[1], zero);
		__m256i a1_hi = _mm256_unpackhi_epi8(a.data[1], zero);

		__m256i b0_lo = _mm256_unpacklo_epi8(b.data[0], zero);
		__m256i b0_hi = _mm256_unpackhi_epi8(b.data[0], zero);
		__m256i b1_lo = _mm256_unpacklo_epi8(b.data[1], zero);
		__m256i b1_hi = _mm256_unpackhi_epi8(b.data[1], zero);

		// Subtract
		__m256i d0_lo = _mm256_sub_epi16(a0_lo, b0_lo);
		__m256i d0_hi = _mm256_sub_epi16(a0_hi, b0_hi);
		__m256i d1_lo = _mm256_sub_epi16(a1_lo, b1_lo);
		__m256i d1_hi = _mm256_sub_epi16(a1_hi, b1_hi);

		if (UseScaledDist)
		{
			// madd multiplies and then adds adjacement pairs of components, expanding from 16 to 32 bits.
			// Rearrange components so we are adding the same channels togeher.
			// This is necessary when using scaled dist, so that per channel scaling applies
			//  to sums just for each channel.
			__m256i shuffle_mask = _mm256_setr_epi8(
				0, 1, 8, 9,   // R0, R1
				2, 3, 10, 11,  // G0, G1
				4, 5, 12, 13,  // B0, B1
				6, 7, 14, 15,  // A0, A1
				16, 17, 24, 25,  // R2, R3
				18, 19, 26, 27,  // G2, G3
				20, 21, 28, 29,  // B2, B3
				22, 23, 30, 31   // A2, A3
			);
			d0_lo = _mm256_shuffle_epi8(d0_lo, shuffle_mask);
			d0_hi = _mm256_shuffle_epi8(d0_hi, shuffle_mask);
			d1_lo = _mm256_shuffle_epi8(d1_lo, shuffle_mask);
			d1_hi = _mm256_shuffle_epi8(d1_hi, shuffle_mask);
		}

		// Square and widen using madd_epi16 (d * d)
		__m256i sq0 = _mm256_madd_epi16(d0_lo, d0_lo);
		__m256i sq1 = _mm256_madd_epi16(d0_hi, d0_hi);
		__m256i sq2 = _mm256_madd_epi16(d1_lo, d1_lo);
		__m256i sq3 = _mm256_madd_epi16(d1_hi, d1_hi);

		// Accumulate all
		__m256i total = _mm256_add_epi32(_mm256_add_epi32(sq0, sq1), _mm256_add_epi32(sq2, sq3));

		// Horizontal sum
		__m128i low = _mm256_castsi256_si128(total);
		__m128i high = _mm256_extracti128_si256(total, 1);
		__m128i sum128 = _mm_add_epi32(low, high);

		__m128 sum128f = _mm_cvtepi32_ps(sum128);
		if (UseScaledDist)
		{
			__m256 sd_min_256 = _mm256_min_ps(a_summary.data, b_summary.data);
			__m128 sd_min = _mm256_castps256_ps128(sd_min_256);	// Obtain sd
			_mm_GetDistScaleSq(sd_min, scale_sq);
			sum128f = _mm_mul_ps(sum128f, scale_sq);
		}
		sum128f = _mm_hadd_ps(sum128f, sum128f);
		sum128f = _mm_hadd_ps(sum128f, sum128f);
		return  _mm_cvtss_f32(sum128f);
	}
};
