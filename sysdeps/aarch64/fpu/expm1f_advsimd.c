/* Single-precision AdvSIMD expm1

   Copyright (C) 2023 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.  */

#include "v_math.h"
#include "poly_advsimd_f32.h"

static const struct data
{
  float32x4_t poly[5];
  float32x4_t invln2, ln2_lo, ln2_hi, shift;
  int32x4_t exponent_bias;
#if WANT_SIMD_EXCEPT
  uint32x4_t thresh;
#else
  float32x4_t oflow_bound;
#endif
} data = {
  /* Generated using fpminimax with degree=5 in [-log(2)/2, log(2)/2].  */
  .poly = { V4 (0x1.fffffep-2), V4 (0x1.5554aep-3), V4 (0x1.555736p-5),
	    V4 (0x1.12287cp-7), V4 (0x1.6b55a2p-10) },
  .invln2 = V4 (0x1.715476p+0f),
  .ln2_hi = V4 (0x1.62e4p-1f),
  .ln2_lo = V4 (0x1.7f7d1cp-20f),
  .shift = V4 (0x1.8p23f),
  .exponent_bias = V4 (0x3f800000),
#if !WANT_SIMD_EXCEPT
  /* Value above which expm1f(x) should overflow. Absolute value of the
     underflow bound is greater than this, so it catches both cases - there is
     a small window where fallbacks are triggered unnecessarily.  */
  .oflow_bound = V4 (0x1.5ebc4p+6),
#else
  /* asuint(oflow_bound) - asuint(0x1p-23), shifted left by 1 for absolute
     compare.  */
  .thresh = V4 (0x1d5ebc40),
#endif
};

/* asuint(0x1p-23), shifted by 1 for abs compare.  */
#define TinyBound v_u32 (0x34000000 << 1)

static float32x4_t VPCS_ATTR NOINLINE
special_case (float32x4_t x, float32x4_t y, uint32x4_t special)
{
  return v_call_f32 (expm1f, x, y, special);
}

/* Single-precision vector exp(x) - 1 function.
   The maximum error is 1.51 ULP:
   _ZGVnN4v_expm1f (0x1.8baa96p-2) got 0x1.e2fb9p-2
				  want 0x1.e2fb94p-2.  */
float32x4_t VPCS_ATTR NOINLINE V_NAME_F1 (expm1) (float32x4_t x)
{
  const struct data *d = ptr_barrier (&data);
  uint32x4_t ix = vreinterpretq_u32_f32 (x);

#if WANT_SIMD_EXCEPT
  /* If fp exceptions are to be triggered correctly, fall back to scalar for
     |x| < 2^-23, |x| > oflow_bound, Inf & NaN. Add ix to itself for
     shift-left by 1, and compare with thresh which was left-shifted offline -
     this is effectively an absolute compare.  */
  uint32x4_t special
      = vcgeq_u32 (vsubq_u32 (vaddq_u32 (ix, ix), TinyBound), d->thresh);
  if (__glibc_unlikely (v_any_u32 (special)))
    x = v_zerofy_f32 (x, special);
#else
  /* Handles very large values (+ve and -ve), +/-NaN, +/-Inf.  */
  uint32x4_t special = vceqzq_u32 (vcaltq_f32 (x, d->oflow_bound));
#endif

  /* Reduce argument to smaller range:
     Let i = round(x / ln2)
     and f = x - i * ln2, then f is in [-ln2/2, ln2/2].
     exp(x) - 1 = 2^i * (expm1(f) + 1) - 1
     where 2^i is exact because i is an integer.  */
  float32x4_t j = vsubq_f32 (vfmaq_f32 (d->shift, d->invln2, x), d->shift);
  int32x4_t i = vcvtq_s32_f32 (j);
  float32x4_t f = vfmsq_f32 (x, j, d->ln2_hi);
  f = vfmsq_f32 (f, j, d->ln2_lo);

  /* Approximate expm1(f) using polynomial.
     Taylor expansion for expm1(x) has the form:
	 x + ax^2 + bx^3 + cx^4 ....
     So we calculate the polynomial P(f) = a + bf + cf^2 + ...
     and assemble the approximation expm1(f) ~= f + f^2 * P(f).  */
  float32x4_t p = v_horner_4_f32 (f, d->poly);
  p = vfmaq_f32 (f, vmulq_f32 (f, f), p);

  /* Assemble the result.
     expm1(x) ~= 2^i * (p + 1) - 1
     Let t = 2^i.  */
  int32x4_t u = vaddq_s32 (vshlq_n_s32 (i, 23), d->exponent_bias);
  float32x4_t t = vreinterpretq_f32_s32 (u);

  if (__glibc_unlikely (v_any_u32 (special)))
    return special_case (vreinterpretq_f32_u32 (ix),
			 vfmaq_f32 (vsubq_f32 (t, v_f32 (1.0f)), p, t),
			 special);

  /* expm1(x) ~= p * t + (t - 1).  */
  return vfmaq_f32 (vsubq_f32 (t, v_f32 (1.0f)), p, t);
}
libmvec_hidden_def (V_NAME_F1 (expm1))
HALF_WIDTH_ALIAS_F1 (expm1)
