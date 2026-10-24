#include "Varispeed.h"

#if defined (__SSE2__) || defined (_M_X64) || defined (_M_AMD64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 2)
 #include <immintrin.h>
 #define HAWTHORNE_SSE 1
#else
 #define HAWTHORNE_SSE 0
#endif

#if HAWTHORNE_SSE && (defined (__GNUC__) || defined (__clang__))
 #define HAWTHORNE_AVX_FN __attribute__ ((target ("avx,fma")))
#else
 #define HAWTHORNE_AVX_FN
#endif

namespace hawthorne
{

#if HAWTHORNE_SSE
// AVX + FMA path, chosen at run time on CPUs that have it.
HAWTHORNE_AVX_FN static float dotAvx (const float* a, const float* b, int n) noexcept
{
    int i = 0;
    __m256 s0 = _mm256_setzero_ps(), s1 = _mm256_setzero_ps(), s2 = _mm256_setzero_ps(), s3 = _mm256_setzero_ps();
    for (; i + 32 <= n; i += 32)
    {
        s0 = _mm256_fmadd_ps (_mm256_loadu_ps (a + i),      _mm256_loadu_ps (b + i),      s0);
        s1 = _mm256_fmadd_ps (_mm256_loadu_ps (a + i + 8),  _mm256_loadu_ps (b + i + 8),  s1);
        s2 = _mm256_fmadd_ps (_mm256_loadu_ps (a + i + 16), _mm256_loadu_ps (b + i + 16), s2);
        s3 = _mm256_fmadd_ps (_mm256_loadu_ps (a + i + 24), _mm256_loadu_ps (b + i + 24), s3);
    }
    for (; i + 8 <= n; i += 8)
        s0 = _mm256_fmadd_ps (_mm256_loadu_ps (a + i), _mm256_loadu_ps (b + i), s0);

    const __m256 s = _mm256_add_ps (_mm256_add_ps (s0, s1), _mm256_add_ps (s2, s3));
    const __m128 h = _mm_add_ps (_mm256_castps256_ps128 (s), _mm256_extractf128_ps (s, 1));
    alignas (16) float lanes[4];
    _mm_store_ps (lanes, h);
    float sum = (lanes[0] + lanes[1]) + (lanes[2] + lanes[3]);
    for (; i < n; ++i)
        sum += a[i] * b[i];
    return sum;
}

static const bool useAvx = juce::SystemStats::hasAVX() && juce::SystemStats::hasFMA3();
#endif

float dotProduct (const float* a, const float* b, int n) noexcept
{
   #if HAWTHORNE_SSE
    if (useAvx)
        return dotAvx (a, b, n);
   #endif

    int i = 0;
   #if HAWTHORNE_SSE
    __m128 s0 = _mm_setzero_ps(), s1 = _mm_setzero_ps(), s2 = _mm_setzero_ps(), s3 = _mm_setzero_ps();
    for (; i + 16 <= n; i += 16)
    {
        s0 = _mm_add_ps (s0, _mm_mul_ps (_mm_loadu_ps (a + i),      _mm_loadu_ps (b + i)));
        s1 = _mm_add_ps (s1, _mm_mul_ps (_mm_loadu_ps (a + i + 4),  _mm_loadu_ps (b + i + 4)));
        s2 = _mm_add_ps (s2, _mm_mul_ps (_mm_loadu_ps (a + i + 8),  _mm_loadu_ps (b + i + 8)));
        s3 = _mm_add_ps (s3, _mm_mul_ps (_mm_loadu_ps (a + i + 12), _mm_loadu_ps (b + i + 12)));
    }
    for (; i + 4 <= n; i += 4)
        s0 = _mm_add_ps (s0, _mm_mul_ps (_mm_loadu_ps (a + i), _mm_loadu_ps (b + i)));

    alignas (16) float lanes[4];
    _mm_store_ps (lanes, _mm_add_ps (_mm_add_ps (s0, s1), _mm_add_ps (s2, s3)));
    float sum = (lanes[0] + lanes[1]) + (lanes[2] + lanes[3]);
   #else
    float s[4] = { 0, 0, 0, 0 };
    for (; i + 4 <= n; i += 4)
    {
        s[0] += a[i] * b[i]; s[1] += a[i + 1] * b[i + 1];
        s[2] += a[i + 2] * b[i + 2]; s[3] += a[i + 3] * b[i + 3];
    }
    float sum = (s[0] + s[1]) + (s[2] + s[3]);
   #endif
    for (; i < n; ++i)
        sum += a[i] * b[i];
    return sum;
}

} // namespace hawthorne
