#pragma once

#include <vector>

#ifdef __SSE__
#include <xmmintrin.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif

#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <xsimd/xsimd.hpp>

namespace ling::utils::simd {

static float sum(std::vector<float> vec) {
  float sum = 0;
  size_t offset = 0;
  std::vector<float> c4;
  c4.reserve(4);
#ifdef __SSE__
  float* p = vec.data();
  __m128 c = _mm_setzero_ps();
  while (offset + 8 < vec.size()) {
    __m128 a = _mm_load_ps(p + offset);
    __m128 b = _mm_load_ps(p + offset + 4);
    c = _mm_add_ps(c, _mm_add_ps(a, b));
    offset += 8;
  }
  _mm_store_ps(c4.data(), c);
  sum = c4[0] + c4[1] + c4[2] + c4[3];
#endif
  while (offset < vec.size()) {
    sum += vec[offset++];
  }
  return sum;
}

static int64_t x_sum(std::vector<int64_t> vec) {
  const int64_t* p = vec.data();
  int64_t sum = 0;
  size_t offset = 0;
  //
  constexpr size_t inc = xsimd::batch<int64_t>::size;
  constexpr size_t inc2 = inc * 2;
  xsimd::batch<int64_t> c = {0};
  while (offset + inc2 <= vec.size()) {
    xsimd::batch<int64_t> a = xsimd::load_unaligned(p + offset);
    xsimd::batch<int64_t> b = xsimd::load_unaligned(p + offset + inc);
    c = c + (a + b);
    offset += inc2;
  }
  sum = xsimd::reduce_add(c);
  //
  while (offset < vec.size()) {
    sum += vec[offset++];
  }
  return sum;
}

static float distance_ip(std::vector<float> va, std::vector<float> vb) {
  if (va.size() != vb.size()) {
    spdlog::error("Illegal input for distance_ip");
    return 0.0f;
  }
  float result = 0.0;
#ifdef __AVX2__
  float* a = va.data();
  float* b = vb.data();
  constexpr int32_t BATCH_SIZE = 8;
  // process 8 floats at a time
  __m256 abVec = _mm256_setzero_ps(); // initialize to 0
  __m256 aVec;
  __m256 bVec;
  int n = dim;
  while (n >= BATCH_SIZE) {
    aVec = _mm256_loadu_ps(a);                  // load 8 floats from memory
    bVec = _mm256_loadu_ps(b);                  // load 8 floats from memory
    abVec = _mm256_fmadd_ps(aVec, bVec, abVec); // accumulate the product
    a += BATCH_SIZE, b += BATCH_SIZE, n -= BATCH_SIZE;
  }
  if (n != 0) {
    // NOLINTBEGIN
    __m256i mask;
    switch (n) {
      case 1:
        mask = _mm256_set_epi32(0, 0, 0, 0, 0, 0, 0, -1);
        break;
      case 2:
        mask = _mm256_set_epi32(0, 0, 0, 0, 0, 0, -1, -1);
        break;
      case 3:
        mask = _mm256_set_epi32(0, 0, 0, 0, 0, -1, -1, -1);
        break;
      case 4:
        mask = _mm256_set_epi32(0, 0, 0, 0, -1, -1, -1, -1);
        break;
      case 5:
        mask = _mm256_set_epi32(0, 0, 0, -1, -1, -1, -1, -1);
        break;
      case 6:
        mask = _mm256_set_epi32(0, 0, -1, -1, -1, -1, -1, -1);
        break;
      case 7:
        mask = _mm256_set_epi32(0, -1, -1, -1, -1, -1, -1, -1);
        break;
    }
    aVec = _mm256_maskload_ps(a, mask);
    bVec = _mm256_maskload_ps(b, mask);
    abVec = _mm256_fmadd_ps(aVec, bVec, abVec);
    // NOLINTEND
  }
  abVec = _mm256_add_ps(_mm256_permute2f128_ps(abVec, abVec, 1), abVec);
  abVec = _mm256_hadd_ps(abVec, abVec);
  abVec = _mm256_hadd_ps(abVec, abVec);

  _mm_store_ss(&result, _mm256_castps256_ps128(abVec));
#else
  for (int i = 0; i < va.size(); i++) {
    result += va[i] * vb[i];
  }
#endif
  return result;
}

static float x_distance_ip(std::vector<float> va, std::vector<float> vb) {
  if (va.size() != vb.size()) {
    spdlog::error("Illegal input for distance_ip");
    return 0.0f;
  }
  constexpr size_t inc = xsimd::batch<float>::size;
  const size_t vs = va.size();
  const size_t ims = vs - vs % inc;
  xsimd::batch<float> c = {0.0};
  size_t idx = 0;
  for (; idx < ims; idx+=inc) {
    auto a_vec = xsimd::batch<float>::load_unaligned(&va[idx]);
    auto b_vec = xsimd::batch<float>::load_unaligned(&vb[idx]);
    c += a_vec * b_vec;
  }
  float result = xsimd::reduce_add(c);
  for (; idx < vs; idx++) {
    result += va[idx] * vb[idx];
  }
  return result;
}

}  // namespace ling::utils::simd