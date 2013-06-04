// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#ifndef RNG_HPP
#define RNG_HPP

// Pseudo-Random Number Generation ==========================================

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <string>


#include <stdint.h>

#if defined(__SSE2__) || ( defined( SC_VS ) && ( defined(_M_X64) || ( defined(_M_IX86_FP) && _M_IX86_FP >= 2 ) ) )
#  define RNG_USE_SSE2
#endif

#if defined( WIN32 ) || defined( _WIN32 ) || defined( __WIN32 )
#  if defined(RNG_USE_SSE2)
#    if defined( __MINGW__ ) || defined( __MINGW32__ )
       // <HACK> Include these headers (in this order) to avoid
       // an order-of-inclusion bug with MinGW headers.
#      include <stdlib.h>
       // Workaround MinGW header bug: http://sourceforge.net/tracker/?func=detail&atid=102435&aid=2962480&group_id=2435
       extern "C" {
#        include <emmintrin.h>
       }
#      include <malloc.h>
       // </HACK>
#    else
#      include <emmintrin.h>
#    endif
#  endif
#else
#  if defined(RNG_USE_SSE2)
#    include <emmintrin.h>
#    include <mm_malloc.h>
#  endif
#endif

namespace rng {

// ==========================================================================
// SFMT Random Number Generator
// ==========================================================================

/**
 * SIMD oriented Fast Mersenne Twister(SFMT) pseudorandom number generator
 *
 * @author Mutsuo Saito (Hiroshima University)
 * @author Makoto Matsumoto (Hiroshima University)
 *
 * Copyright (C) 2006, 2007 Mutsuo Saito, Makoto Matsumoto and Hiroshima
 * University. All rights reserved.
 *
 * The new BSD License is applied to this software.
 */

// http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/SFMT/

// ALWAYS ALLOCATE THROUGH NEW
template <typename Derived>
class rng_engine_mt_base_t
{
  // "Cleverness" alert: this template uses CRTP (see
  // http://en.wikipedia.org/wiki/Curiously_recurring_template_pattern)
  // to invoke the do_recursion() implementation in the _Derived_ class.
  // This lets us have two different flavors of the algorithm - one that
  // uses SSE2, and one without - without virtual functions or code
  // duplication.
public:
  /** 128-bit data structure */
  union w128_t
  {
#ifdef RNG_USE_SSE2
    __m128i si;
    __m128d sd;
#endif
    uint64_t u[2];
    uint32_t u32[4];
    double d[2];
  };

protected:
  static const int DSFMT_MEXP = 19937; // Mersenne Exponent. The period of the sequence is a multiple of 2^MEXP-1.

  static const int DSFMT_N = ( ( DSFMT_MEXP - 128 ) / 104 + 1 );

  static const int DSFMT_N64 = ( DSFMT_N * 2 );

  static const uint64_t DSFMT_LOW_MASK = 0x000FFFFFFFFFFFFFULL;
  static const uint64_t DSFMT_HIGH_CONST = 0x3FF0000000000000ULL;
  static const int DSFMT_SR = 12;

  static const int DSFMT_POS1 = 117;
  static const int DSFMT_SL1 = 19;
  static const uint64_t DSFMT_MSK1 = 0x000ffafffffffb3fULL;
  static const uint64_t DSFMT_MSK2 = 0x000ffdfffc90fffdULL;
  static const uint64_t DSFMT_FIX1 = 0x90014964b32f4329ULL;
  static const uint64_t DSFMT_FIX2 = 0x3b8d12ac548a7c7aULL;
  static const uint64_t DSFMT_PCV1 = 0x3d84e1ac0dc82880ULL;
  static const uint64_t DSFMT_PCV2 = 0x0000000000000001ULL;

  /** the 128-bit internal state array */
  struct dsfmt_t
  {
    w128_t status[DSFMT_N + 1];
    int idx;
  };

  /** global data */
  dsfmt_t dsfmt_global_data;

  /**
   * This function represents the recursion formula.
   * @param r output 128-bit
   * @param a a 128-bit part of the internal state array
   * @param b a 128-bit part of the internal state array
   * @param d a 128-bit part of the internal state array (I/O)
   */
  static void do_recursion( w128_t *r, const w128_t *a, const w128_t *b, w128_t *d )
  { Derived::do_recursion( r, a, b, d ); }

  /**
   * This function fills the internal state array with double precision
   * floating point pseudorandom numbers of the IEEE 754 format.
   * @param dsfmt dsfmt state vector.
   */

  void dsfmt_gen_rand_all( dsfmt_t *dsfmt )
  {
    int i;
    w128_t lung;

    lung = dsfmt->status[DSFMT_N];
    do_recursion( &dsfmt->status[0], &dsfmt->status[0],
                  &dsfmt->status[DSFMT_POS1], &lung );
    for ( i = 1; i < DSFMT_N - DSFMT_POS1; i++ )
    {
      do_recursion( &dsfmt->status[i], &dsfmt->status[i],
                    &dsfmt->status[i + DSFMT_POS1], &lung );
    }
    for ( ; i < DSFMT_N; i++ )
    {
      do_recursion( &dsfmt->status[i], &dsfmt->status[i],
                    &dsfmt->status[i + DSFMT_POS1 - DSFMT_N], &lung );

    }
    dsfmt->status[DSFMT_N] = lung;
  }

  /**
   * This function initializes the internal state array to fit the IEEE
   * 754 format.
   * @param dsfmt dsfmt state vector.
   */
  void initial_mask( dsfmt_t *dsfmt )
  {
    int i;
    uint64_t *psfmt;

    psfmt = &dsfmt->status[0].u[0];
    for ( i = 0; i < DSFMT_N * 2; i++ )
    {
      psfmt[i] = ( psfmt[i] & DSFMT_LOW_MASK ) | DSFMT_HIGH_CONST;
    }
  }

  /**
   * This function certificate the period of 2^{SFMT_MEXP}-1.
   * @param dsfmt dsfmt state vector.
   */
  void period_certification( dsfmt_t *dsfmt )
  {
    uint64_t pcv[2] = {DSFMT_PCV1, DSFMT_PCV2};
    uint64_t tmp[2];
    uint64_t inner;
    int i;

    tmp[0] = ( dsfmt->status[DSFMT_N].u[0] ^ DSFMT_FIX1 );
    tmp[1] = ( dsfmt->status[DSFMT_N].u[1] ^ DSFMT_FIX2 );

    inner = tmp[0] & pcv[0];
    inner ^= tmp[1] & pcv[1];
    for ( i = 32; i > 0; i >>= 1 )
    {
      inner ^= inner >> i;
    }
    inner &= 1;
    /* check OK */
    if ( inner == 1 )
    {
      return;
    }
    /* check NG, and modification */
    dsfmt->status[DSFMT_N].u[1] ^= 1;
    return;
  }

  int idxof( int i )
  {
    return i;
  }

  /**
   * This function initializes the internal state array with a 32-bit
   * integer seed.
   * @param dsfmt dsfmt state vector.
   * @param seed a 32-bit integer used as the seed.
   * @param mexp caller's mersenne expornent
   */
  void dsfmt_chk_init_gen_rand( dsfmt_t *dsfmt, uint32_t seed )
  {
    int i;
    uint32_t *psfmt;


    psfmt = &dsfmt->status[0].u32[0];
    psfmt[idxof( 0 )] = seed;
    for ( i = 1; i < ( DSFMT_N + 1 ) * 4; i++ )
    {
      psfmt[idxof( i )] = 1812433253UL
                          * ( psfmt[idxof( i - 1 )] ^ ( psfmt[idxof( i - 1 )] >> 30 ) ) + i;
    }
    initial_mask( dsfmt );
    period_certification( dsfmt );
    dsfmt->idx = DSFMT_N64;
  }

  double dsfmt_genrand_close_open( dsfmt_t *dsfmt )
  {
    double *psfmt64 = &dsfmt->status[0].d[0];

    if ( dsfmt->idx >= DSFMT_N64 )
    {
      dsfmt_gen_rand_all( dsfmt );
      dsfmt->idx = 0;
    }
    return psfmt64[dsfmt->idx++];
  }

public:
  void seed( uint32_t start )
  { dsfmt_chk_init_gen_rand( &dsfmt_global_data, start ); }

  // This is the main interface of the RNG_ENGINE. Returns a uniformly distributed number in the range [0 1]
  double operator()()
  { return dsfmt_genrand_close_open( &dsfmt_global_data ) - 1.0; }


#ifdef RNG_USE_SSE2
  // Hack to get proper alignment for rng_base_t<rng_engine_mt_sse2_t>:
  // 32-bit libraries typically align malloc chunks to sizeof(double) == 8.
  // This object needs to be aligned to sizeof(__m128d) == 16.
  static void* operator new( size_t size )
  {
    void* p = _mm_malloc( size, sizeof( __m128d ) );
    if ( !p ) throw ( std::bad_alloc() );
    return p;
  }
  static void operator delete( void* p )
  { return _mm_free( p ); }
#endif
};

// ==========================================================================
// SFMT Random Number Generator - Standard C, NO SSE2
// ==========================================================================

class rng_engine_mt_t : public rng_engine_mt_base_t<rng_engine_mt_t>
{
public:
  static void do_recursion( w128_t *r, const w128_t *a, const w128_t *b, w128_t *d )
  {
    uint64_t t0, t1, L0, L1;

    t0 = a->u[0];
    t1 = a->u[1];
    L0 = d->u[0];
    L1 = d->u[1];
    d->u[0] = ( t0 << DSFMT_SL1 ) ^ ( L1 >> 32 ) ^ ( L1 << 32 ) ^ b->u[0];
    d->u[1] = ( t1 << DSFMT_SL1 ) ^ ( L0 >> 32 ) ^ ( L0 << 32 ) ^ b->u[1];
    r->u[0] = ( d->u[0] >> DSFMT_SR ) ^ ( d->u[0] & DSFMT_MSK1 ) ^ t0;
    r->u[1] = ( d->u[1] >> DSFMT_SR ) ^ ( d->u[1] & DSFMT_MSK2 ) ^ t1;
  }
};

// ==========================================================================
// SFMT Random Number Generator - SSE2 optimizations
// ==========================================================================

#if defined(RNG_USE_SSE2)
class rng_engine_mt_sse2_t : public rng_engine_mt_base_t<rng_engine_mt_sse2_t>
{
public:
  static void do_recursion( w128_t *r, const w128_t *a, const w128_t *b, w128_t *d )
  {
    const int SSE2_SHUFF = 0x1b;

    /** mask data for sse2 */
    static const union { uint64_t u[2]; __m128i m; } sse2_param_mask = {{ DSFMT_MSK1, DSFMT_MSK2 }};

    __m128i v, w, x, y, z;

    x = a->si;
    z = _mm_slli_epi64( x, DSFMT_SL1 );
    y = _mm_shuffle_epi32( d -> si, SSE2_SHUFF );
    z = _mm_xor_si128( z, b -> si );
    y = _mm_xor_si128( y, z );

    v = _mm_srli_epi64( y, DSFMT_SR );
    w = _mm_and_si128( y, sse2_param_mask.m );
    v = _mm_xor_si128( v, x );
    v = _mm_xor_si128( v, w );
    r->si = v;
    d->si = y;
  }

  rng_engine_mt_sse2_t()
  {
    // Validate proper alignment for SSE2 types.
    assert( ( uintptr_t )dsfmt_global_data.status % 16 == 0 );
  }

  // 32-bit libraries typically align malloc chunks to sizeof(double) == 8.
  // This object needs to be aligned to sizeof(__m128d) == 16.
  static void* operator new( size_t size )
  { return _mm_malloc( size, sizeof( __m128d ) ); }
  static void operator delete( void* p )
  { return _mm_free( p ); }
};
#endif

// ==========================================================================
// Probability Distributions
// ==========================================================================

/* Offers some common probability distributions like
 * - uniform distribution
 * - gaussian distribution
 * - exponential distribution
 * - exgaussian distribution
 * - bernoulli distribution ( roll )
 *
 * It uses a RNG_GENERATOR to obtain uniformly distributed random numbers
 * in the range [0 1], which are then transformed to build the desired distribution.
 */

template <typename RNG_GENERATOR>
class distribution_t
{
private:
  // This is the RNG engine/generator used to obtain random numbers
  // Treat it as a black box which gives numbers between 0 and 1 through its () operator.
  RNG_GENERATOR* engine;

  double gauss_pair_value; // allow re-use of unused ( but necessary ) random number of a previous call to gauss()
  bool   gauss_pair_use;
public:
  distribution_t() :
    engine( new RNG_GENERATOR() ),
    gauss_pair_value( 0.0 ),
    gauss_pair_use( false )
  {
    seed();
  }

  ~distribution_t()
  {
    delete engine;
  }

  // Seed rng generator
  void seed( uint32_t value = static_cast<uint32_t>( std::time( NULL ) ) )
  { engine -> seed( value ); }

  // obtain a random value from the rng generator
  double real() { return ( *engine )(); }

  // bernoulli distribution
  bool roll( double chance )
  {
    if ( chance <= 0 ) return false;
    if ( chance >= 1 ) return true;
    return real() < chance;
  }

  // uniform distribution in the range [min max]
  double range( double min, double max )
  {
    assert( min <= max );
    return min + real() * ( max - min );
  }

  // gaussian distribution
  double gauss( double mean, double stddev, bool truncate_low_end = false )
  {
    /* This code adapted from ftp://ftp.taygeta.com/pub/c/boxmuller.c
     * Implements the Polar form of the Box-Muller Transformation
     *
     * (c) Copyright 1994, Everett F. Carter Jr.
     *     Permission is granted by the author to use
     *     this software for any application provided this
     *     copyright notice is preserved.
     */

    double z;

    if ( stddev != 0 )
    {
      if ( gauss_pair_use )
      {
        z = gauss_pair_value;
        gauss_pair_use = false;
      }
      else
      {
        double x1, x2, w, y1, y2;
        do
        {
          x1 = 2.0 * real() - 1.0;
          x2 = 2.0 * real() - 1.0;
          w = x1 * x1 + x2 * x2;
        }
        while ( w >= 1.0 || w == 0.0 );

        w = sqrt( ( -2.0 * log( w ) ) / w );
        y1 = x1 * w;
        y2 = x2 * w;

        z = y1;
        gauss_pair_value = y2;
        gauss_pair_use = true;
      }
    }
    else
      z = 0.0;

    double result = mean + z * stddev;

    // True gaussian distribution can of course yield any number at some probability.  So truncate on the low end.
    if ( truncate_low_end && result < 0 )
      result = 0;

    return result;
  }


  // exponential distribution
  double exponential( double nu )
  {
    double x;
    do
    {
      x = real();
    }
    while ( x >= 1.0 ); // avoid ln(0)

    return - std::log( 1 - x ) * nu;
  }

  // Exponentially modified Gaussian distribution
  double exgauss( double gauss_mean, double gauss_stddev, double exp_nu )
  { return std::max( 0.0, gauss( gauss_mean, gauss_stddev ) + exponential( exp_nu ) ); }


};

double stdnormal_cdf( double );
double stdnormal_inv( double );
} // end namespace rng



#endif // RNG_HPP