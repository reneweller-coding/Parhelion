/**
 * @file arm_neon.h
 * @brief NOT the real <arm_neon.h>, and not the test shim either (Tests/neonshim): the NEON intrinsics Vec.h uses, on
 *        SSE's four lanes, so the NEON path can be *timed* on the desktop (Phase 7: what a set costs on the Quest, before
 *        a headset is at hand). The Quest's core does four lanes per instruction as SSE does; the table reads, which the
 *        AVX2 path gathers, run through the scalar loops as they do on the Quest.
 *
 * Only for timing: the fused forms are a multiply and an add here (SSE has no FMA), so the results are not the NEON
 * path's bit for bit -- the test shim (Tests/neonshim) is the one that checks those. Built with PARH_NEON_BENCH, which
 * Vec.h treats as PARH_NEON_SHIM (the NEON path, named "neon-bench").
 */
#pragma once
#include <cstdint>
#include <smmintrin.h>

typedef float float32_t;     ///< ACLE scalar type
typedef __m128 float32x4_t;  ///< four float lanes
typedef __m128i uint32x4_t;  ///< four mask lanes

inline float32x4_t vld1q_f32(const float32_t* p) { return _mm_loadu_ps(p); }
inline void vst1q_f32(float32_t* p, float32x4_t a) { _mm_storeu_ps(p, a); }
inline float32x4_t vdupq_n_f32(float32_t x) { return _mm_set1_ps(x); }
inline float32x4_t vaddq_f32(float32x4_t a, float32x4_t b) { return _mm_add_ps(a, b); }
inline float32x4_t vsubq_f32(float32x4_t a, float32x4_t b) { return _mm_sub_ps(a, b); }
inline float32x4_t vmulq_f32(float32x4_t a, float32x4_t b) { return _mm_mul_ps(a, b); }
inline float32x4_t vdivq_f32(float32x4_t a, float32x4_t b) { return _mm_div_ps(a, b); }
inline float32x4_t vnegq_f32(float32x4_t a) { return _mm_xor_ps(a, _mm_set1_ps(-0.0f)); }
inline float32x4_t vabsq_f32(float32x4_t a) { return _mm_andnot_ps(_mm_set1_ps(-0.0f), a); }
inline float32x4_t vsqrtq_f32(float32x4_t a) { return _mm_sqrt_ps(a); }
inline float32x4_t vrndmq_f32(float32x4_t a) { return _mm_floor_ps(a); }
/** @brief a + b c (not fused: timing only). */
inline float32x4_t vfmaq_f32(float32x4_t a, float32x4_t b, float32x4_t c) { return _mm_add_ps(a, _mm_mul_ps(b, c)); }
/** @brief a - b c (not fused: timing only). */
inline float32x4_t vfmsq_f32(float32x4_t a, float32x4_t b, float32x4_t c) { return _mm_sub_ps(a, _mm_mul_ps(b, c)); }
inline uint32x4_t vcltq_f32(float32x4_t a, float32x4_t b) { return _mm_castps_si128(_mm_cmplt_ps(a, b)); }
inline uint32x4_t vcgtq_f32(float32x4_t a, float32x4_t b) { return _mm_castps_si128(_mm_cmpgt_ps(a, b)); }
inline uint32x4_t vcgeq_f32(float32x4_t a, float32x4_t b) { return _mm_castps_si128(_mm_cmpge_ps(a, b)); }
inline uint32x4_t vandq_u32(uint32x4_t a, uint32x4_t b) { return _mm_and_si128(a, b); }
inline uint32x4_t vorrq_u32(uint32x4_t a, uint32x4_t b) { return _mm_or_si128(a, b); }
/** @brief Select: @p t where the mask is set, @p f elsewhere (the masks are whole lanes). */
inline float32x4_t vbslq_f32(uint32x4_t m, float32x4_t t, float32x4_t f) { return _mm_blendv_ps(f, t, _mm_castsi128_ps(m)); }
