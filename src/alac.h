/*
 * alac.h — vendored ALAC (Apple Lossless) decoder for bpplay
 *
 * This is Apple's own open-sourced ALAC reference decoder, adapted for a
 * single-header, single-translation-unit C build (the same convention this
 * project already uses for dr_flac.h):
 *
 *   - ALACBitUtilities.c/h, EndianPortable.c/h, ag_dec.c/aglib.h,
 *     dp_dec.c/dplib.h, matrix_dec.c/matrixlib.h, ALACAudioTypes.h are
 *     Apple's files verbatim (bit-exact codec math untouched), taken from
 *     https://github.com/mikebrady/alac (a pure-C fork of Apple's original
 *     https://github.com/macosforge/alac), Apache License 2.0.
 *   - ALACDecoder.cpp (the only C++ file in that project — a thin
 *     orchestration class around the pieces above) has been ported to plain
 *     C here: the class becomes a struct, methods become functions taking
 *     an explicit ALACDecoder* first argument. The control flow and every
 *     numeric computation are unchanged from the original.
 *   - alac_decoder_configure() replaces the original Init(), which parsed a
 *     raw "magic cookie" byte blob (a format designed for CoreAudio's
 *     AudioFormatGetProperty, with historical wrapper-atom sniffing that
 *     doesn't apply here). Since bpplay's own M4A atom parser (source.h)
 *     already locates and decodes the 24-byte ALACSpecificConfig directly,
 *     alac_decoder_configure() takes that struct pre-parsed instead of
 *     re-deriving it from a cookie blob — a boundary-only simplification,
 *     not a change to the codec itself.
 *
 * Original copyright (c) 2011 Apple Inc. Licensed under the Apache License,
 * Version 2.0 (http://www.apache.org/licenses/LICENSE-2.0). See
 * THIRD_PARTY_NOTICES.md for the full attribution.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BPPLAY_ALAC_H
#define BPPLAY_ALAC_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef MIN
#define MIN(x, y) ( (x)<(y) ?(x) :(y) )
#endif
#ifndef MAX
#define MAX(x, y) ( (x)>(y) ?(x): (y) )
#endif
#ifndef nil
#define nil NULL
#endif

#define RequireAction(condition, action)          if (!(condition)) { action }
#define RequireActionSilent(condition, action)     if (!(condition)) { action }
#define RequireNoErr(condition, action)            if ((condition)) { action }

enum {
    ALAC_noErr = 0,
    kALAC_UnimplementedError   = -4,
    kALAC_FileNotFoundError    = -43,
    kALAC_ParamError           = -50,
    kALAC_MemFullError         = -108
};

enum {
    kALACMaxChannels    = 8,
    kALACMaxEscapeHeaderBytes = 8,
    kALACMaxSearches    = 16,
    kALACMaxCoefs       = 16,
    kALACDefaultFramesPerPacket = 4096,
    kALACVersion        = 0,
    kALACCompatibleVersion = kALACVersion,
    kALACDefaultFrameSize  = 4096
};

/* note: this struct is exactly the 24-byte, big-endian-on-disk
 * ALACSpecificConfig wrapped in the 'alac' atom inside an M4A's stsd. */
typedef struct ALACSpecificConfig {
    uint32_t frameLength;
    uint8_t  compatibleVersion;
    uint8_t  bitDepth;          /* max 32 */
    uint8_t  pb;                /* 0 <= pb <= 255 */
    uint8_t  mb;
    uint8_t  kb;
    uint8_t  numChannels;
    uint16_t maxRun;
    uint32_t maxFrameBytes;
    uint32_t avgBitRate;
    uint32_t sampleRate;
} ALACSpecificConfig;

/* ------------------------------------------------------------------ */
/* Bit buffer                                                          */
/* ------------------------------------------------------------------ */

typedef struct BitBuffer {
    uint8_t  *cur;
    uint8_t  *end;
    uint32_t  bitIndex;
    uint32_t  byteSize;
} BitBuffer;

void     BitBufferInit( BitBuffer * bits, uint8_t * buffer, uint32_t byteSize );
uint32_t BitBufferRead( BitBuffer * bits, uint8_t numBits );
uint8_t  BitBufferReadSmall( BitBuffer * bits, uint8_t numBits );
uint8_t  BitBufferReadOne( BitBuffer * bits );
uint32_t BitBufferPeek( BitBuffer * bits, uint8_t numBits );
uint32_t BitBufferPeekOne( BitBuffer * bits );
uint32_t BitBufferUnpackBERSize( BitBuffer * bits );
uint32_t BitBufferGetPosition( BitBuffer * bits );
void     BitBufferByteAlign( BitBuffer * bits, int32_t addZeros );
void     BitBufferAdvance( BitBuffer * bits, uint32_t numBits );
void     BitBufferRewind( BitBuffer * bits, uint32_t numBits );
void     BitBufferWrite( BitBuffer * bits, uint32_t value, uint32_t numBits );
void     BitBufferReset( BitBuffer * bits );

typedef enum {
    ID_SCE = 0, ID_CPE = 1, ID_CCE = 2, ID_LFE = 3,
    ID_DSE = 4, ID_PCE = 5, ID_FIL = 6, ID_END = 7
} ELEMENT_TYPE;

/* ------------------------------------------------------------------ */
/* Adaptive Golomb (aglib)                                             */
/* ------------------------------------------------------------------ */

#define QBSHIFT 9
#define QB (1<<QBSHIFT)
#define PB0 40
#define MB0 10
#define KB0 14
#define MAX_RUN_DEFAULT 255
#define MMULSHIFT 2
#define MDENSHIFT (QBSHIFT - MMULSHIFT - 1)
#define MOFF ((1<<(MDENSHIFT-2)))
#define BITOFF 24
#define MAX_PREFIX_16        9
#define MAX_PREFIX_TOLONG_16 15
#define MAX_PREFIX_32        9
#define MAX_DATATYPE_BITS_16 16

typedef struct AGParamRec {
    uint32_t mb, mb0, pb, kb, wb, qb;
    uint32_t fw, sw;
    uint32_t maxrun;
} AGParamRec, *AGParamRecPtr;

void    set_standard_ag_params(AGParamRecPtr params, uint32_t fullwidth, uint32_t sectorwidth);
void    set_ag_params(AGParamRecPtr params, uint32_t m, uint32_t p, uint32_t k, uint32_t f, uint32_t s, uint32_t maxrun);
int32_t dyn_decomp(AGParamRecPtr params, BitBuffer * bitstream, int32_t * pc, int32_t numSamples, int32_t maxSize, uint32_t * outNumBits);

/* ------------------------------------------------------------------ */
/* Dynamic predictor (dplib)                                           */
/* ------------------------------------------------------------------ */

#define DENSHIFT_MAX  15
#define DENSHIFT_DEFAULT 9
#define AINIT 38
#define BINIT (-29)
#define CINIT (-2)
#define NUMCOEPAIRS 16

void unpc_block( int32_t * pc, int32_t * out, int32_t num, int16_t * coefs, int32_t numactive, uint32_t chanbits, uint32_t denshift );

/* ------------------------------------------------------------------ */
/* Matrixing (matrixlib)                                               */
/* ------------------------------------------------------------------ */

void unmix16( int32_t * u, int32_t * v, int16_t * out, uint32_t stride, int32_t numSamples, int32_t mixbits, int32_t mixres );
void unmix20( int32_t * u, int32_t * v, uint8_t * out, uint32_t stride, int32_t numSamples, int32_t mixbits, int32_t mixres );
void unmix24( int32_t * u, int32_t * v, uint8_t * out, uint32_t stride, int32_t numSamples,
              int32_t mixbits, int32_t mixres, uint16_t * shiftUV, int32_t bytesShifted );
void unmix32( int32_t * u, int32_t * v, int32_t * out, uint32_t stride, int32_t numSamples,
              int32_t mixbits, int32_t mixres, uint16_t * shiftUV, int32_t bytesShifted );
void copyPredictorTo24( int32_t * in, uint8_t * out, uint32_t stride, int32_t numSamples );
void copyPredictorTo24Shift( int32_t * in, uint16_t * shift, uint8_t * out, uint32_t stride, int32_t numSamples, int32_t bytesShifted );
void copyPredictorTo20( int32_t * in, uint8_t * out, uint32_t stride, int32_t numSamples );
void copyPredictorTo32( int32_t * in, int32_t * out, uint32_t stride, int32_t numSamples );
void copyPredictorTo32Shift( int32_t * in, uint16_t * shift, int32_t * out, uint32_t stride, int32_t numSamples, int32_t bytesShifted );

/* ------------------------------------------------------------------ */
/* Endian swap                                                         */
/* ------------------------------------------------------------------ */

uint16_t Swap16BtoN(uint16_t inUInt16);
uint32_t Swap32BtoN(uint32_t inUInt32);

/* ------------------------------------------------------------------ */
/* Decoder (C port of ALACDecoder.cpp)                                 */
/* ------------------------------------------------------------------ */

typedef struct ALACDecoder {
    ALACSpecificConfig config;
    uint16_t  activeElements;
    int32_t  *mixBufferU;
    int32_t  *mixBufferV;
    int32_t  *predictor;
    uint16_t *shiftBuffer;   /* shares memory with predictor, see configure() */
} ALACDecoder;

void    alac_decoder_init( ALACDecoder * dec );
void    alac_decoder_free( ALACDecoder * dec );
int32_t alac_decoder_configure( ALACDecoder * dec, const ALACSpecificConfig * cfg );
int32_t alac_decoder_decode( ALACDecoder * dec, BitBuffer * bits, uint8_t * sampleBuffer,
                              uint32_t numSamples, uint32_t numChannels, uint32_t * outNumSamples );

/* ==================================================================== */
#ifdef ALAC_IMPLEMENTATION
/* ==================================================================== */

/* -------------------- ALACBitUtilities.c -------------------- */

void BitBufferInit( BitBuffer * bits, uint8_t * buffer, uint32_t byteSize )
{
    bits->cur      = buffer;
    bits->end      = bits->cur + byteSize;
    bits->bitIndex = 0;
    bits->byteSize = byteSize;
}

uint32_t BitBufferRead( BitBuffer * bits, uint8_t numBits )
{
    uint32_t returnBits;
    returnBits = ((uint32_t)bits->cur[0] << 16) | ((uint32_t)bits->cur[1] << 8) | ((uint32_t)bits->cur[2]);
    returnBits = returnBits << bits->bitIndex;
    returnBits &= 0x00FFFFFF;
    bits->bitIndex += numBits;
    returnBits = returnBits >> (24 - numBits);
    bits->cur      += (bits->bitIndex >> 3);
    bits->bitIndex &= 7;
    return returnBits;
}

uint8_t BitBufferReadSmall( BitBuffer * bits, uint8_t numBits )
{
    uint16_t returnBits;
    returnBits = (uint16_t)((bits->cur[0] << 8) | bits->cur[1]);
    returnBits = (uint16_t)(returnBits << bits->bitIndex);
    bits->bitIndex += numBits;
    returnBits = (uint16_t)(returnBits >> (16 - numBits));
    bits->cur      += (bits->bitIndex >> 3);
    bits->bitIndex &= 7;
    return (uint8_t)returnBits;
}

uint8_t BitBufferReadOne( BitBuffer * bits )
{
    uint8_t returnBits;
    returnBits = (bits->cur[0] >> (7 - bits->bitIndex)) & 1;
    bits->bitIndex++;
    bits->cur      += (bits->bitIndex >> 3);
    bits->bitIndex &= 7;
    return returnBits;
}

uint32_t BitBufferPeek( BitBuffer * bits, uint8_t numBits )
{
    return ((((((uint32_t) bits->cur[0] << 16) | ((uint32_t) bits->cur[1] << 8) |
            ((uint32_t) bits->cur[2])) << bits->bitIndex) & 0x00FFFFFF) >> (24 - numBits));
}

uint32_t BitBufferPeekOne( BitBuffer * bits )
{
    return ((bits->cur[0] >> (7 - bits->bitIndex)) & 1);
}

uint32_t BitBufferUnpackBERSize( BitBuffer * bits )
{
    uint32_t size;
    uint8_t  tmp;
    for ( size = 0, tmp = 0x80u; tmp &= 0x80u; size = (size << 7u) | (tmp & 0x7fu) )
        tmp = (uint8_t) BitBufferReadSmall( bits, 8 );
    return size;
}

uint32_t BitBufferGetPosition( BitBuffer * bits )
{
    uint8_t *begin = bits->end - bits->byteSize;
    return ((uint32_t)(bits->cur - begin) * 8) + bits->bitIndex;
}

void BitBufferByteAlign( BitBuffer * bits, int32_t addZeros )
{
    if ( bits->bitIndex == 0 ) return;
    if ( addZeros ) BitBufferWrite( bits, 0, 8 - bits->bitIndex );
    else BitBufferAdvance( bits, 8 - bits->bitIndex );
}

void BitBufferAdvance( BitBuffer * bits, uint32_t numBits )
{
    if ( numBits ) {
        bits->bitIndex += numBits;
        bits->cur += (bits->bitIndex >> 3);
        bits->bitIndex &= 7;
    }
}

void BitBufferRewind( BitBuffer * bits, uint32_t numBits )
{
    uint32_t numBytes;
    if ( numBits == 0 ) return;
    if ( bits->bitIndex >= numBits ) { bits->bitIndex -= numBits; return; }
    numBits -= bits->bitIndex;
    bits->bitIndex = 0;
    numBytes = numBits / 8;
    numBits  = numBits % 8;
    bits->cur -= numBytes;
    if ( numBits > 0 ) { bits->bitIndex = 8 - numBits; bits->cur--; }
    if ( bits->cur < (bits->end - bits->byteSize) ) {
        bits->cur = (bits->end - bits->byteSize);
        bits->bitIndex = 0;
    }
}

void BitBufferWrite( BitBuffer * bits, uint32_t bitValues, uint32_t numBits )
{
    uint32_t invBitIndex;
    RequireAction( bits != nil, return; );
    RequireActionSilent( numBits > 0, return; );
    invBitIndex = 8 - bits->bitIndex;
    while ( numBits > 0 ) {
        uint32_t tmp;
        uint8_t  shift;
        uint8_t  mask;
        uint32_t curNum;
        curNum = MIN( invBitIndex, numBits );
        tmp = bitValues >> (numBits - curNum);
        shift  = (uint8_t)(invBitIndex - curNum);
        mask   = 0xffu >> (8 - curNum);
        mask <<= shift;
        bits->cur[0] = (bits->cur[0] & ~mask) | (((uint8_t) tmp << shift) & mask);
        numBits -= curNum;
        invBitIndex -= curNum;
        if ( invBitIndex == 0 ) { invBitIndex = 8; bits->cur++; }
    }
    bits->bitIndex = 8 - invBitIndex;
}

void BitBufferReset( BitBuffer * bits )
{
    bits->cur      = bits->end - bits->byteSize;
    bits->bitIndex = 0;
}

/* -------------------- EndianPortable.c -------------------- */
/* NOTE: upstream's endianness detection only recognised __i386__/__x86_64__/
 * TARGET_OS_WIN32 as little-endian, silently omitting arm64 — a real bug for
 * bpplay's universal (x86_64 + arm64) build. Extended to cover Apple Silicon. */

#define BSWAP16(x) (((x << 8) | ((x >> 8) & 0x00ff)))
#define BSWAP32(x) (((x << 24) | ((x << 8) & 0x00ff0000) | ((x >> 8) & 0x0000ff00) | ((x >> 24) & 0x000000ff)))

#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) || defined(__arm64__) || defined(TARGET_OS_WIN32)
#define ALAC_TARGET_RT_LITTLE_ENDIAN 1
#endif

uint16_t Swap16BtoN(uint16_t inUInt16)
{
#if ALAC_TARGET_RT_LITTLE_ENDIAN
    return (uint16_t)BSWAP16(inUInt16);
#else
    return inUInt16;
#endif
}

uint32_t Swap32BtoN(uint32_t inUInt32)
{
#if ALAC_TARGET_RT_LITTLE_ENDIAN
    return BSWAP32(inUInt32);
#else
    return inUInt32;
#endif
}

/* -------------------- ag_dec.c -------------------- */

#define CODE_TO_LONG_MAXBITS 32
#define N_MAX_MEAN_CLAMP     0xffff
#define N_MEAN_CLAMP_VAL     0xffff

void set_standard_ag_params(AGParamRecPtr params, uint32_t fullwidth, uint32_t sectorwidth)
{
    set_ag_params( params, MB0, PB0, KB0, fullwidth, sectorwidth, MAX_RUN_DEFAULT );
}

void set_ag_params(AGParamRecPtr params, uint32_t m, uint32_t p, uint32_t k, uint32_t f, uint32_t s, uint32_t maxrun)
{
    params->mb = params->mb0 = m;
    params->pb = p;
    params->kb = k;
    params->wb = (1u<<params->kb)-1;
    params->qb = QB-params->pb;
    params->fw = f;
    params->sw = s;
    params->maxrun = maxrun;
}

static inline int32_t alac_lead( int32_t m )
{
    long j;
    unsigned long c = (1ul << 31);
    for(j=0; j < 32; j++) {
        if((c & m) != 0) break;
        c >>= 1;
    }
    return (int32_t)j;
}

#define arithmin(a, b) ((a) < (b) ? (a) : (b))

static inline int32_t alac_lg3a( int32_t x)
{
    int32_t result;
    x += 3;
    result = alac_lead(x);
    return 31 - result;
}

static inline uint32_t alac_read32bit( uint8_t * buffer )
{
    uint32_t value;
    value = ((uint32_t)buffer[0] << 24) | ((uint32_t)buffer[1] << 16) |
             ((uint32_t)buffer[2] << 8) | (uint32_t)buffer[3];
    return value;
}

#define get_next_fromlong(inlong, suff) ((inlong) >> (32 - (suff)))

static inline uint32_t alac_getstreambits( uint8_t *in, int32_t bitoffset, int32_t numbits )
{
    uint32_t load1, load2;
    uint32_t byteoffset = bitoffset / 8;
    uint32_t result;
    load1 = alac_read32bit( in + byteoffset );
    if ( (numbits + (bitoffset & 0x7)) > 32) {
        int32_t load2shift;
        result = load1 << (bitoffset & 0x7);
        load2 = (uint32_t) in[byteoffset+4];
        load2shift = (8-(numbits + (bitoffset & 0x7)-32));
        load2 >>= load2shift;
        result >>= (32-numbits);
        result |= load2;
    } else {
        result = load1 >> (32-numbits-(bitoffset & 7));
    }
    if ( numbits != (int32_t)(sizeof(result) * 8) )
        result &= ~(0xfffffffful << numbits);
    return result;
}

static inline int32_t alac_dyn_get(unsigned char *in, uint32_t *bitPos, uint32_t m, uint32_t k)
{
    uint32_t tempbits = *bitPos;
    uint32_t result;
    uint32_t pre = 0, v;
    uint32_t streamlong;
    streamlong = alac_read32bit( in + (tempbits >> 3) );
    streamlong <<= (tempbits & 7);
    {
        uint32_t notI = ~streamlong;
        pre = alac_lead( notI);
    }
    if(pre >= MAX_PREFIX_16) {
        pre = MAX_PREFIX_16;
        tempbits += pre;
        streamlong <<= pre;
        result = get_next_fromlong(streamlong,MAX_DATATYPE_BITS_16);
        tempbits += MAX_DATATYPE_BITS_16;
    } else {
        tempbits += pre;
        tempbits += 1;
        streamlong <<= pre+1;
        v = get_next_fromlong(streamlong, k);
        tempbits += k;
        result = pre*m + v-1;
        if(v<2) {
            result -= (v-1);
            tempbits -= 1;
        }
    }
    *bitPos = tempbits;
    return (int32_t)result;
}

static inline int32_t alac_dyn_get_32bit( uint8_t * in, uint32_t * bitPos, int32_t m, int32_t k, int32_t maxbits )
{
    uint32_t tempbits = *bitPos;
    uint32_t v;
    uint32_t streamlong;
    uint32_t result;
    streamlong = alac_read32bit( in + (tempbits >> 3) );
    streamlong <<= (tempbits & 7);
    {
        uint32_t notI = ~streamlong;
        result = alac_lead( notI);
    }
    if((int32_t)result >= MAX_PREFIX_32) {
        result = alac_getstreambits(in, (int32_t)(tempbits+MAX_PREFIX_32), maxbits);
        tempbits += MAX_PREFIX_32 + maxbits;
    } else {
        tempbits += result;
        tempbits += 1;
        if (k != 1) {
            streamlong <<= result+1;
            v = get_next_fromlong(streamlong, k);
            tempbits += (uint32_t)k;
            tempbits -= 1;
            result = result*(uint32_t)m;
            if(v>=2) {
                result += (v-1);
                tempbits += 1;
            }
        }
    }
    *bitPos = tempbits;
    return (int32_t)result;
}

int32_t dyn_decomp( AGParamRecPtr params, BitBuffer * bitstream, int32_t * pc, int32_t numSamples, int32_t maxSize, uint32_t * outNumBits )
{
    uint8_t  *in;
    int32_t  *outPtr = pc;
    uint32_t bitPos, startPos, maxPos;
    uint32_t j, m, k, n, c, mz;
    int32_t  del, zmode;
    uint32_t mb;
    uint32_t pb_local = params->pb;
    uint32_t kb_local = params->kb;
    uint32_t wb_local = params->wb;
    int32_t  status;

    RequireAction( (bitstream != nil) && (pc != nil) && (outNumBits != nil), return kALAC_ParamError; );
    *outNumBits = 0;

    in = bitstream->cur;
    startPos = bitstream->bitIndex;
    maxPos = bitstream->byteSize * 8;
    bitPos = startPos;

    mb = params->mb0;
    zmode = 0;
    c = 0;
    status = ALAC_noErr;

    while (c < (uint32_t)numSamples) {
        RequireAction( bitPos < maxPos, status = kALAC_ParamError; goto Exit; );

        m = (mb)>>QBSHIFT;
        k = (uint32_t)alac_lg3a((int32_t)m);
        k = arithmin(k, kb_local);
        m = (1<<k)-1;

        n = (uint32_t)alac_dyn_get_32bit( in, &bitPos, (int32_t)m, (int32_t)k, maxSize );

        {
            uint32_t ndecode = n + (uint32_t)zmode;
            int32_t  multiplier = (- (int32_t)(ndecode&1));
            multiplier |= 1;
            del = (int32_t)((ndecode+1) >> 1) * (multiplier);
        }

        *outPtr++ = del;
        c++;

        mb = pb_local*(n+(uint32_t)zmode) + mb - ((pb_local*mb)>>QBSHIFT);
        if (n > N_MAX_MEAN_CLAMP) mb = N_MEAN_CLAMP_VAL;

        zmode = 0;

        if (((mb << MMULSHIFT) < QB) && (c < (uint32_t)numSamples)) {
            zmode = 1;
            k = (uint32_t)alac_lead((int32_t)mb) - BITOFF+((mb+MOFF)>>MDENSHIFT);
            mz = ((1u<<k)-1) & wb_local;

            n = (uint32_t)alac_dyn_get(in, &bitPos, mz, k);

            RequireAction(c+n <= (uint32_t)numSamples, status = kALAC_ParamError; goto Exit; );

            for(j=0; j < n; j++) { *outPtr++ = 0; ++c; }

            if(n >= 65535) zmode = 0;
            mb = 0;
        }
    }

Exit:
    *outNumBits = (bitPos - startPos);
    BitBufferAdvance( bitstream, *outNumBits );
    RequireAction( bitstream->cur <= bitstream->end, status = kALAC_ParamError; );
    return status;
}

/* -------------------- dp_dec.c -------------------- */

static inline int32_t alac_sign_of_int( int32_t i )
{
    int32_t negishift;
    negishift = (int32_t)(((uint32_t)-i) >> 31);
    return negishift | (i >> 31);
}

void unpc_block( int32_t * pc1, int32_t * out, int32_t num, int16_t * coefs, int32_t numactive, uint32_t chanbits, uint32_t denshift )
{
    int32_t  j, k, lim;
    int32_t  sum1, sg, sgn, top, dd;
    int32_t *pout;
    int32_t  del, del0;
    uint32_t chanshift = 32 - chanbits;
    int32_t  denhalf = 1<<(denshift-1);

    out[0] = pc1[0];
    if ( numactive == 0 ) {
        if ( (num > 1) && (pc1 != out) )
            memcpy( &out[1], &pc1[1], (size_t)(num - 1) * sizeof(int32_t) );
        return;
    }
    if ( numactive == 31 ) {
        int32_t prev;
        prev = out[0];
        for ( j = 1; j < num; j++ ) {
            del = pc1[j] + prev;
            prev = (del << chanshift) >> chanshift;
            out[j] = prev;
        }
        return;
    }

    for ( j = 1; j <= numactive; j++ ) {
        del = pc1[j] + out[j-1];
        out[j] = (del << chanshift) >> chanshift;
    }

    lim = numactive + 1;

    if ( numactive == 4 ) {
        int16_t a0, a1, a2, a3;
        int32_t b0, b1, b2, b3;
        a0 = coefs[0]; a1 = coefs[1]; a2 = coefs[2]; a3 = coefs[3];

        for ( j = lim; j < num; j++ ) {
            top = out[j - lim];
            pout = out + j - 1;
            b0 = top - pout[0]; b1 = top - pout[-1]; b2 = top - pout[-2]; b3 = top - pout[-3];
            sum1 = (denhalf - a0 * b0 - a1 * b1 - a2 * b2 - a3 * b3) >> denshift;
            del = pc1[j]; del0 = del;
            sg = alac_sign_of_int(del);
            del += top + sum1;
            out[j] = (del << chanshift) >> chanshift;
            if ( sg > 0 ) {
                sgn = alac_sign_of_int( b3 ); a3 = (int16_t)(a3 - sgn);
                del0 -= (4 - 3) * ((sgn * b3) >> denshift);
                if ( del0 <= 0 ) continue;
                sgn = alac_sign_of_int( b2 ); a2 = (int16_t)(a2 - sgn);
                del0 -= (4 - 2) * ((sgn * b2) >> denshift);
                if ( del0 <= 0 ) continue;
                sgn = alac_sign_of_int( b1 ); a1 = (int16_t)(a1 - sgn);
                del0 -= (4 - 1) * ((sgn * b1) >> denshift);
                if ( del0 <= 0 ) continue;
                a0 = (int16_t)(a0 - alac_sign_of_int( b0 ));
            } else if ( sg < 0 ) {
                sgn = -alac_sign_of_int( b3 ); a3 = (int16_t)(a3 - sgn);
                del0 -= (4 - 3) * ((sgn * b3) >> denshift);
                if ( del0 >= 0 ) continue;
                sgn = -alac_sign_of_int( b2 ); a2 = (int16_t)(a2 - sgn);
                del0 -= (4 - 2) * ((sgn * b2) >> denshift);
                if ( del0 >= 0 ) continue;
                sgn = -alac_sign_of_int( b1 ); a1 = (int16_t)(a1 - sgn);
                del0 -= (4 - 1) * ((sgn * b1) >> denshift);
                if ( del0 >= 0 ) continue;
                a0 = (int16_t)(a0 + alac_sign_of_int( b0 ));
            }
        }
        coefs[0] = a0; coefs[1] = a1; coefs[2] = a2; coefs[3] = a3;
    } else if ( numactive == 8 ) {
        int16_t a0, a1, a2, a3, a4, a5, a6, a7;
        int32_t b0, b1, b2, b3, b4, b5, b6, b7;
        a0 = coefs[0]; a1 = coefs[1]; a2 = coefs[2]; a3 = coefs[3];
        a4 = coefs[4]; a5 = coefs[5]; a6 = coefs[6]; a7 = coefs[7];

        for ( j = lim; j < num; j++ ) {
            top = out[j - lim];
            pout = out + j - 1;
            b0 = top - (*pout--); b1 = top - (*pout--); b2 = top - (*pout--); b3 = top - (*pout--);
            b4 = top - (*pout--); b5 = top - (*pout--); b6 = top - (*pout--); b7 = top - (*pout);
            pout += 8;
            sum1 = (denhalf - a0 * b0 - a1 * b1 - a2 * b2 - a3 * b3
                    - a4 * b4 - a5 * b5 - a6 * b6 - a7 * b7) >> denshift;
            del = pc1[j]; del0 = del;
            sg = alac_sign_of_int(del);
            del += top + sum1;
            out[j] = (del << chanshift) >> chanshift;
            if ( sg > 0 ) {
                sgn = alac_sign_of_int( b7 ); a7 = (int16_t)(a7 - sgn); del0 -= 1 * ((sgn * b7) >> denshift);
                if ( del0 <= 0 ) continue;
                sgn = alac_sign_of_int( b6 ); a6 = (int16_t)(a6 - sgn); del0 -= 2 * ((sgn * b6) >> denshift);
                if ( del0 <= 0 ) continue;
                sgn = alac_sign_of_int( b5 ); a5 = (int16_t)(a5 - sgn); del0 -= 3 * ((sgn * b5) >> denshift);
                if ( del0 <= 0 ) continue;
                sgn = alac_sign_of_int( b4 ); a4 = (int16_t)(a4 - sgn); del0 -= 4 * ((sgn * b4) >> denshift);
                if ( del0 <= 0 ) continue;
                sgn = alac_sign_of_int( b3 ); a3 = (int16_t)(a3 - sgn); del0 -= 5 * ((sgn * b3) >> denshift);
                if ( del0 <= 0 ) continue;
                sgn = alac_sign_of_int( b2 ); a2 = (int16_t)(a2 - sgn); del0 -= 6 * ((sgn * b2) >> denshift);
                if ( del0 <= 0 ) continue;
                sgn = alac_sign_of_int( b1 ); a1 = (int16_t)(a1 - sgn); del0 -= 7 * ((sgn * b1) >> denshift);
                if ( del0 <= 0 ) continue;
                a0 = (int16_t)(a0 - alac_sign_of_int( b0 ));
            } else if ( sg < 0 ) {
                sgn = -alac_sign_of_int( b7 ); a7 = (int16_t)(a7 - sgn); del0 -= 1 * ((sgn * b7) >> denshift);
                if ( del0 >= 0 ) continue;
                sgn = -alac_sign_of_int( b6 ); a6 = (int16_t)(a6 - sgn); del0 -= 2 * ((sgn * b6) >> denshift);
                if ( del0 >= 0 ) continue;
                sgn = -alac_sign_of_int( b5 ); a5 = (int16_t)(a5 - sgn); del0 -= 3 * ((sgn * b5) >> denshift);
                if ( del0 >= 0 ) continue;
                sgn = -alac_sign_of_int( b4 ); a4 = (int16_t)(a4 - sgn); del0 -= 4 * ((sgn * b4) >> denshift);
                if ( del0 >= 0 ) continue;
                sgn = -alac_sign_of_int( b3 ); a3 = (int16_t)(a3 - sgn); del0 -= 5 * ((sgn * b3) >> denshift);
                if ( del0 >= 0 ) continue;
                sgn = -alac_sign_of_int( b2 ); a2 = (int16_t)(a2 - sgn); del0 -= 6 * ((sgn * b2) >> denshift);
                if ( del0 >= 0 ) continue;
                sgn = -alac_sign_of_int( b1 ); a1 = (int16_t)(a1 - sgn); del0 -= 7 * ((sgn * b1) >> denshift);
                if ( del0 >= 0 ) continue;
                a0 = (int16_t)(a0 + alac_sign_of_int( b0 ));
            }
        }
        coefs[0] = a0; coefs[1] = a1; coefs[2] = a2; coefs[3] = a3;
        coefs[4] = a4; coefs[5] = a5; coefs[6] = a6; coefs[7] = a7;
    } else {
        for ( j = lim; j < num; j++ ) {
            sum1 = 0;
            pout = out + j - 1;
            top = out[j-lim];
            for ( k = 0; k < numactive; k++ )
                sum1 += coefs[k] * (pout[-k] - top);
            del = pc1[j]; del0 = del;
            sg = alac_sign_of_int( del );
            del += top + ((sum1 + denhalf) >> denshift);
            out[j] = (del << chanshift) >> chanshift;
            if ( sg > 0 ) {
                for ( k = (numactive - 1); k >= 0; k-- ) {
                    dd = top - pout[-k];
                    sgn = alac_sign_of_int( dd );
                    coefs[k] = (int16_t)(coefs[k] - sgn);
                    del0 -= (numactive - k) * ((sgn * dd) >> denshift);
                    if ( del0 <= 0 ) break;
                }
            } else if ( sg < 0 ) {
                for ( k = (numactive - 1); k >= 0; k-- ) {
                    dd = top - pout[-k];
                    sgn = alac_sign_of_int( dd );
                    coefs[k] = (int16_t)(coefs[k] + sgn);
                    del0 -= (numactive - k) * ((-sgn * dd) >> denshift);
                    if ( del0 >= 0 ) break;
                }
            }
        }
    }
}

/* -------------------- matrix_dec.c -------------------- */
/* bpplay only builds for little-endian targets (x86_64 / arm64). */
#define LBYTE 0
#define MBYTE 1
#define HBYTE 2

void unmix16( int32_t * u, int32_t * v, int16_t * out, uint32_t stride, int32_t numSamples, int32_t mixbits, int32_t mixres )
{
    int16_t *op = out;
    int32_t  j;
    if ( mixres != 0 ) {
        for ( j = 0; j < numSamples; j++ ) {
            int32_t l, r;
            l = u[j] + v[j] - ((mixres * v[j]) >> mixbits);
            r = l - v[j];
            op[0] = (int16_t) l; op[1] = (int16_t) r;
            op += stride;
        }
    } else {
        for ( j = 0; j < numSamples; j++ ) {
            op[0] = (int16_t) u[j]; op[1] = (int16_t) v[j];
            op += stride;
        }
    }
}

void unmix20( int32_t * u, int32_t * v, uint8_t * out, uint32_t stride, int32_t numSamples, int32_t mixbits, int32_t mixres )
{
    uint8_t *op = out;
    int32_t  j;
    if ( mixres != 0 ) {
        for ( j = 0; j < numSamples; j++ ) {
            int32_t l, r;
            l = u[j] + v[j] - ((mixres * v[j]) >> mixbits);
            r = l - v[j];
            l <<= 4; r <<= 4;
            op[HBYTE] = (uint8_t)((l >> 16) & 0xffu); op[MBYTE] = (uint8_t)((l >> 8) & 0xffu); op[LBYTE] = (uint8_t)((l >> 0) & 0xffu);
            op += 3;
            op[HBYTE] = (uint8_t)((r >> 16) & 0xffu); op[MBYTE] = (uint8_t)((r >> 8) & 0xffu); op[LBYTE] = (uint8_t)((r >> 0) & 0xffu);
            op += (stride - 1) * 3;
        }
    } else {
        for ( j = 0; j < numSamples; j++ ) {
            int32_t val;
            val = u[j] << 4;
            op[HBYTE] = (uint8_t)((val >> 16) & 0xffu); op[MBYTE] = (uint8_t)((val >> 8) & 0xffu); op[LBYTE] = (uint8_t)((val >> 0) & 0xffu);
            op += 3;
            val = v[j] << 4;
            op[HBYTE] = (uint8_t)((val >> 16) & 0xffu); op[MBYTE] = (uint8_t)((val >> 8) & 0xffu); op[LBYTE] = (uint8_t)((val >> 0) & 0xffu);
            op += (stride - 1) * 3;
        }
    }
}

void unmix24( int32_t * u, int32_t * v, uint8_t * out, uint32_t stride, int32_t numSamples,
              int32_t mixbits, int32_t mixres, uint16_t * shiftUV, int32_t bytesShifted )
{
    uint8_t *op = out;
    int32_t  shift = bytesShifted * 8;
    int32_t  l, r;
    int32_t  j, k;
    if ( mixres != 0 ) {
        if ( bytesShifted != 0 ) {
            for ( j = 0, k = 0; j < numSamples; j++, k += 2 ) {
                l = u[j] + v[j] - ((mixres * v[j]) >> mixbits);
                r = l - v[j];
                l = (l << shift) | (uint32_t) shiftUV[k + 0];
                r = (r << shift) | (uint32_t) shiftUV[k + 1];
                op[HBYTE] = (uint8_t)((l >> 16) & 0xffu); op[MBYTE] = (uint8_t)((l >> 8) & 0xffu); op[LBYTE] = (uint8_t)((l >> 0) & 0xffu);
                op += 3;
                op[HBYTE] = (uint8_t)((r >> 16) & 0xffu); op[MBYTE] = (uint8_t)((r >> 8) & 0xffu); op[LBYTE] = (uint8_t)((r >> 0) & 0xffu);
                op += (stride - 1) * 3;
            }
        } else {
            for ( j = 0; j < numSamples; j++ ) {
                l = u[j] + v[j] - ((mixres * v[j]) >> mixbits);
                r = l - v[j];
                op[HBYTE] = (uint8_t)((l >> 16) & 0xffu); op[MBYTE] = (uint8_t)((l >> 8) & 0xffu); op[LBYTE] = (uint8_t)((l >> 0) & 0xffu);
                op += 3;
                op[HBYTE] = (uint8_t)((r >> 16) & 0xffu); op[MBYTE] = (uint8_t)((r >> 8) & 0xffu); op[LBYTE] = (uint8_t)((r >> 0) & 0xffu);
                op += (stride - 1) * 3;
            }
        }
    } else {
        if ( bytesShifted != 0 ) {
            for ( j = 0, k = 0; j < numSamples; j++, k += 2 ) {
                l = u[j]; r = v[j];
                l = (l << shift) | (uint32_t) shiftUV[k + 0];
                r = (r << shift) | (uint32_t) shiftUV[k + 1];
                op[HBYTE] = (uint8_t)((l >> 16) & 0xffu); op[MBYTE] = (uint8_t)((l >> 8) & 0xffu); op[LBYTE] = (uint8_t)((l >> 0) & 0xffu);
                op += 3;
                op[HBYTE] = (uint8_t)((r >> 16) & 0xffu); op[MBYTE] = (uint8_t)((r >> 8) & 0xffu); op[LBYTE] = (uint8_t)((r >> 0) & 0xffu);
                op += (stride - 1) * 3;
            }
        } else {
            for ( j = 0; j < numSamples; j++ ) {
                int32_t val;
                val = u[j];
                op[HBYTE] = (uint8_t)((val >> 16) & 0xffu); op[MBYTE] = (uint8_t)((val >> 8) & 0xffu); op[LBYTE] = (uint8_t)((val >> 0) & 0xffu);
                op += 3;
                val = v[j];
                op[HBYTE] = (uint8_t)((val >> 16) & 0xffu); op[MBYTE] = (uint8_t)((val >> 8) & 0xffu); op[LBYTE] = (uint8_t)((val >> 0) & 0xffu);
                op += (stride - 1) * 3;
            }
        }
    }
}

void unmix32( int32_t * u, int32_t * v, int32_t * out, uint32_t stride, int32_t numSamples,
              int32_t mixbits, int32_t mixres, uint16_t * shiftUV, int32_t bytesShifted )
{
    int32_t *op = out;
    int32_t  shift = bytesShifted * 8;
    int32_t  l, r;
    int32_t  j, k;
    if ( mixres != 0 ) {
        for ( j = 0, k = 0; j < numSamples; j++, k += 2 ) {
            int32_t lt, rt;
            lt = u[j]; rt = v[j];
            l = lt + rt - ((mixres * rt) >> mixbits);
            r = l - rt;
            op[0] = (l << shift) | (uint32_t) shiftUV[k + 0];
            op[1] = (r << shift) | (uint32_t) shiftUV[k + 1];
            op += stride;
        }
    } else {
        if ( bytesShifted == 0 ) {
            for ( j = 0; j < numSamples; j++ ) { op[0] = u[j]; op[1] = v[j]; op += stride; }
        } else {
            for ( j = 0, k = 0; j < numSamples; j++, k += 2 ) {
                op[0] = (u[j] << shift) | (uint32_t) shiftUV[k + 0];
                op[1] = (v[j] << shift) | (uint32_t) shiftUV[k + 1];
                op += stride;
            }
        }
    }
}

void copyPredictorTo24( int32_t * in, uint8_t * out, uint32_t stride, int32_t numSamples )
{
    uint8_t *op = out;
    int32_t  j;
    for ( j = 0; j < numSamples; j++ ) {
        int32_t val = in[j];
        op[HBYTE] = (uint8_t)((val >> 16) & 0xffu); op[MBYTE] = (uint8_t)((val >> 8) & 0xffu); op[LBYTE] = (uint8_t)((val >> 0) & 0xffu);
        op += (stride * 3);
    }
}

void copyPredictorTo24Shift( int32_t * in, uint16_t * shift, uint8_t * out, uint32_t stride, int32_t numSamples, int32_t bytesShifted )
{
    uint8_t *op = out;
    int32_t  shiftVal = bytesShifted * 8;
    int32_t  j;
    for ( j = 0; j < numSamples; j++ ) {
        int32_t val = in[j];
        val = (val << shiftVal) | (uint32_t) shift[j];
        op[HBYTE] = (uint8_t)((val >> 16) & 0xffu); op[MBYTE] = (uint8_t)((val >> 8) & 0xffu); op[LBYTE] = (uint8_t)((val >> 0) & 0xffu);
        op += (stride * 3);
    }
}

void copyPredictorTo20( int32_t * in, uint8_t * out, uint32_t stride, int32_t numSamples )
{
    uint8_t *op = out;
    int32_t  j;
    for ( j = 0; j < numSamples; j++ ) {
        int32_t val = in[j];
        op[HBYTE] = (uint8_t)((val >> 12) & 0xffu); op[MBYTE] = (uint8_t)((val >> 4) & 0xffu); op[LBYTE] = (uint8_t)((val << 4) & 0xffu);
        op += (stride * 3);
    }
}

void copyPredictorTo32( int32_t * in, int32_t * out, uint32_t stride, int32_t numSamples )
{
    int32_t i, j;
    for ( i = 0, j = 0; i < numSamples; i++, j += (int32_t)stride ) out[j] = in[i];
}

void copyPredictorTo32Shift( int32_t * in, uint16_t * shift, int32_t * out, uint32_t stride, int32_t numSamples, int32_t bytesShifted )
{
    int32_t *op = out;
    uint32_t shiftVal = (uint32_t)bytesShifted * 8;
    int32_t  j;
    for ( j = 0; j < numSamples; j++ ) {
        op[0] = (in[j] << shiftVal) | (uint32_t) shift[j];
        op += stride;
    }
}

/* -------------------- ALACDecoder (C port of ALACDecoder.cpp) -------------------- */

static void alac_zero16( int16_t * buffer, uint32_t numItems, uint32_t stride )
{
    if ( stride == 1 ) memset( buffer, 0, numItems * sizeof(int16_t) );
    else for ( uint32_t index = 0; index < (numItems * stride); index += stride ) buffer[index] = 0;
}
static void alac_zero24( uint8_t * buffer, uint32_t numItems, uint32_t stride )
{
    if ( stride == 1 ) memset( buffer, 0, numItems * 3 );
    else for ( uint32_t index = 0; index < (numItems * stride * 3); index += (stride * 3) ) {
        buffer[index+0] = 0; buffer[index+1] = 0; buffer[index+2] = 0;
    }
}
static void alac_zero32( int32_t * buffer, uint32_t numItems, uint32_t stride )
{
    if ( stride == 1 ) memset( buffer, 0, numItems * sizeof(int32_t) );
    else for ( uint32_t index = 0; index < (numItems * stride); index += stride ) buffer[index] = 0;
}

static int32_t alac_fill_element( BitBuffer * bits )
{
    int16_t count;
    count = (int16_t) BitBufferReadSmall( bits, 4 );
    if ( count == 15 ) count = (int16_t)(count + (int16_t) BitBufferReadSmall( bits, 8 ) - 1);
    BitBufferAdvance( bits, (uint32_t)(count * 8) );
    RequireAction( bits->cur <= bits->end, return kALAC_ParamError; );
    return ALAC_noErr;
}

static int32_t alac_data_stream_element( BitBuffer * bits )
{
    int32_t  data_byte_align_flag;
    uint16_t count;
    (void) BitBufferReadSmall( bits, 4 );   /* element_instance_tag, unused */
    data_byte_align_flag = BitBufferReadOne( bits );
    count = BitBufferReadSmall( bits, 8 );
    if ( count == 255 ) count = (uint16_t)(count + BitBufferReadSmall( bits, 8 ));
    if ( data_byte_align_flag ) BitBufferByteAlign( bits, 0 );
    BitBufferAdvance( bits, (uint32_t)(count * 8) );
    RequireAction( bits->cur <= bits->end, return kALAC_ParamError; );
    return ALAC_noErr;
}

void alac_decoder_init( ALACDecoder * dec )
{
    memset( dec, 0, sizeof(*dec) );
}

void alac_decoder_free( ALACDecoder * dec )
{
    if ( dec->mixBufferU ) { free(dec->mixBufferU); dec->mixBufferU = NULL; }
    if ( dec->mixBufferV ) { free(dec->mixBufferV); dec->mixBufferV = NULL; }
    if ( dec->predictor )  { free(dec->predictor);  dec->predictor  = NULL; }
    dec->shiftBuffer = NULL;
}

int32_t alac_decoder_configure( ALACDecoder * dec, const ALACSpecificConfig * cfg )
{
    dec->config = *cfg;

    RequireAction( dec->config.compatibleVersion <= kALACVersion, return kALAC_ParamError; );
    RequireAction( dec->config.frameLength > 0 && dec->config.frameLength <= 65536, return kALAC_ParamError; );
    RequireAction( dec->config.numChannels > 0 && dec->config.numChannels <= kALACMaxChannels, return kALAC_ParamError; );

    dec->mixBufferU = (int32_t *) calloc( dec->config.frameLength, sizeof(int32_t) );
    dec->mixBufferV = (int32_t *) calloc( dec->config.frameLength, sizeof(int32_t) );
    dec->predictor  = (int32_t *) calloc( dec->config.frameLength, sizeof(int32_t) );
    dec->shiftBuffer = (uint16_t *) dec->predictor;

    RequireAction( (dec->mixBufferU != nil) && (dec->mixBufferV != nil) && (dec->predictor != nil),
                    return kALAC_MemFullError; );
    return ALAC_noErr;
}

int32_t alac_decoder_decode( ALACDecoder * dec, BitBuffer * bits, uint8_t * sampleBuffer,
                              uint32_t numSamples, uint32_t numChannels, uint32_t * outNumSamples )
{
    BitBuffer   shiftBits;
    uint32_t    bits1, bits2;
    uint8_t     tag;
    AGParamRec  agParams;
    uint32_t    channelIndex;
    int16_t     coefsU[32];
    int16_t     coefsV[32];
    uint8_t     numU, numV;
    uint8_t     mixBits;
    int8_t      mixRes;
    uint16_t    unusedHeader;
    uint8_t     escapeFlag;
    uint32_t    chanBits;
    uint8_t     bytesShifted;
    uint32_t    shift;
    uint8_t     modeU, modeV;
    uint32_t    denShiftU, denShiftV;
    uint16_t    pbFactorU, pbFactorV;
    uint16_t    pb;
    int16_t    *out16;
    uint8_t    *out20;
    uint8_t    *out24;
    int32_t    *out32;
    uint8_t     headerByte;
    uint8_t     partialFrame;
    uint32_t    extraBits;
    int32_t     val;
    uint32_t    i, j;
    int32_t     status;

    RequireAction( (bits != nil) && (sampleBuffer != nil) && (outNumSamples != nil), return kALAC_ParamError; );
    RequireAction( numChannels > 0, return kALAC_ParamError; );

    dec->activeElements = 0;
    channelIndex = 0;
    status = ALAC_noErr;
    *outNumSamples = numSamples;

    while ( status == ALAC_noErr ) {
        RequireAction( bits->cur < bits->end, status = kALAC_ParamError; goto Exit; );

        pb = dec->config.pb;
        tag = BitBufferReadSmall( bits, 3 );
        switch ( tag ) {
            case ID_SCE:
            case ID_LFE: {
                (void) BitBufferReadSmall( bits, 4 ); /* elementInstanceTag */
                unusedHeader = (uint16_t) BitBufferRead( bits, 12 );
                RequireAction( unusedHeader == 0, status = kALAC_ParamError; goto Exit; );

                headerByte = (uint8_t) BitBufferRead( bits, 4 );
                partialFrame = headerByte >> 3;
                bytesShifted = (headerByte >> 1) & 0x3u;
                RequireAction( bytesShifted != 3, status = kALAC_ParamError; goto Exit; );
                shift = (uint32_t)bytesShifted * 8;
                escapeFlag = headerByte & 0x1;
                chanBits = dec->config.bitDepth - (bytesShifted * 8);

                if ( partialFrame != 0 ) {
                    numSamples  = BitBufferRead( bits, 16 ) << 16;
                    numSamples |= BitBufferRead( bits, 16 );
                }

                if ( escapeFlag == 0 ) {
                    mixBits = (uint8_t) BitBufferRead( bits, 8 );
                    mixRes  = (int8_t) BitBufferRead( bits, 8 );
                    (void) mixBits; (void) mixRes;

                    headerByte = (uint8_t) BitBufferRead( bits, 8 );
                    modeU     = headerByte >> 4;
                    denShiftU = headerByte & 0xfu;

                    headerByte = (uint8_t) BitBufferRead( bits, 8 );
                    pbFactorU  = headerByte >> 5;
                    numU       = headerByte & 0x1fu;
                    for ( i = 0; i < numU; i++ ) coefsU[i] = (int16_t) BitBufferRead( bits, 16 );

                    if ( bytesShifted != 0 ) {
                        shiftBits = *bits;
                        BitBufferAdvance( bits, ((uint32_t)bytesShifted * 8) * numSamples );
                    }

                    set_ag_params( &agParams, dec->config.mb, (uint32_t)(pb * pbFactorU) / 4, dec->config.kb, numSamples, numSamples, dec->config.maxRun );
                    status = dyn_decomp( &agParams, bits, dec->predictor, (int32_t)numSamples, (int32_t)chanBits, &bits1 );
                    RequireNoErr( status, goto Exit; );

                    if ( modeU == 0 ) {
                        unpc_block( dec->predictor, dec->mixBufferU, (int32_t)numSamples, &coefsU[0], numU, chanBits, denShiftU );
                    } else {
                        unpc_block( dec->predictor, dec->predictor, (int32_t)numSamples, nil, 31, chanBits, 0 );
                        unpc_block( dec->predictor, dec->mixBufferU, (int32_t)numSamples, &coefsU[0], numU, chanBits, denShiftU );
                    }
                } else {
                    shift = 32 - chanBits;
                    if ( chanBits <= 16 ) {
                        for ( i = 0; i < numSamples; i++ ) {
                            val = (int32_t) BitBufferRead( bits, (uint8_t) chanBits );
                            val = (int32_t)((uint32_t)val << shift) >> shift;
                            dec->mixBufferU[i] = val;
                        }
                    } else {
                        extraBits = chanBits - 16;
                        for ( i = 0; i < numSamples; i++ ) {
                            val = (int32_t) BitBufferRead( bits, 16 );
                            val = (int32_t)((uint32_t)val << 16) >> shift;
                            dec->mixBufferU[i] = val | (int32_t)BitBufferRead( bits, (uint8_t) extraBits );
                        }
                    }
                    bits1 = chanBits * numSamples;
                    bytesShifted = 0;
                }

                if ( bytesShifted != 0 ) {
                    shift = (uint32_t)bytesShifted * 8;
                    for ( i = 0; i < numSamples; i++ )
                        dec->shiftBuffer[i] = (uint16_t) BitBufferRead( &shiftBits, (uint8_t) shift );
                }

                switch ( dec->config.bitDepth ) {
                    case 16:
                        out16 = &((int16_t *)sampleBuffer)[channelIndex];
                        for ( i = 0, j = 0; i < numSamples; i++, j += numChannels ) out16[j] = (int16_t) dec->mixBufferU[i];
                        break;
                    case 20:
                        out20 = (uint8_t *)sampleBuffer + (channelIndex * 3);
                        copyPredictorTo20( dec->mixBufferU, out20, numChannels, (int32_t)numSamples );
                        break;
                    case 24:
                        out24 = (uint8_t *)sampleBuffer + (channelIndex * 3);
                        if ( bytesShifted != 0 ) copyPredictorTo24Shift( dec->mixBufferU, dec->shiftBuffer, out24, numChannels, (int32_t)numSamples, bytesShifted );
                        else copyPredictorTo24( dec->mixBufferU, out24, numChannels, (int32_t)numSamples );
                        break;
                    case 32:
                        out32 = &((int32_t *)sampleBuffer)[channelIndex];
                        if ( bytesShifted != 0 ) copyPredictorTo32Shift( dec->mixBufferU, dec->shiftBuffer, out32, numChannels, (int32_t)numSamples, bytesShifted );
                        else copyPredictorTo32( dec->mixBufferU, out32, numChannels, (int32_t)numSamples );
                        break;
                }

                channelIndex += 1;
                *outNumSamples = numSamples;
                break;
            }

            case ID_CPE: {
                if ( (channelIndex + 2) > numChannels ) goto NoMoreChannels;

                (void) BitBufferReadSmall( bits, 4 ); /* elementInstanceTag */
                unusedHeader = (uint16_t) BitBufferRead( bits, 12 );
                RequireAction( unusedHeader == 0, status = kALAC_ParamError; goto Exit; );

                headerByte = (uint8_t) BitBufferRead( bits, 4 );
                partialFrame = headerByte >> 3;
                bytesShifted = (headerByte >> 1) & 0x3u;
                RequireAction( bytesShifted != 3, status = kALAC_ParamError; goto Exit; );
                shift = (uint32_t)bytesShifted * 8;
                escapeFlag = headerByte & 0x1;
                chanBits = dec->config.bitDepth - (bytesShifted * 8) + 1;

                if ( partialFrame != 0 ) {
                    numSamples  = BitBufferRead( bits, 16 ) << 16;
                    numSamples |= BitBufferRead( bits, 16 );
                }

                if ( escapeFlag == 0 ) {
                    mixBits = (uint8_t) BitBufferRead( bits, 8 );
                    mixRes  = (int8_t) BitBufferRead( bits, 8 );

                    headerByte = (uint8_t) BitBufferRead( bits, 8 );
                    modeU     = headerByte >> 4;
                    denShiftU = headerByte & 0xfu;
                    headerByte = (uint8_t) BitBufferRead( bits, 8 );
                    pbFactorU  = headerByte >> 5;
                    numU       = headerByte & 0x1fu;
                    for ( i = 0; i < numU; i++ ) coefsU[i] = (int16_t) BitBufferRead( bits, 16 );

                    headerByte = (uint8_t) BitBufferRead( bits, 8 );
                    modeV     = headerByte >> 4;
                    denShiftV = headerByte & 0xfu;
                    headerByte = (uint8_t) BitBufferRead( bits, 8 );
                    pbFactorV  = headerByte >> 5;
                    numV       = headerByte & 0x1fu;
                    for ( i = 0; i < numV; i++ ) coefsV[i] = (int16_t) BitBufferRead( bits, 16 );

                    if ( bytesShifted != 0 ) {
                        shiftBits = *bits;
                        BitBufferAdvance( bits, ((uint32_t)bytesShifted * 8) * 2 * numSamples );
                    }

                    set_ag_params( &agParams, dec->config.mb, (uint32_t)(pb * pbFactorU) / 4, dec->config.kb, numSamples, numSamples, dec->config.maxRun );
                    status = dyn_decomp( &agParams, bits, dec->predictor, (int32_t)numSamples, (int32_t)chanBits, &bits1 );
                    RequireNoErr( status, goto Exit; );

                    if ( modeU == 0 ) {
                        unpc_block( dec->predictor, dec->mixBufferU, (int32_t)numSamples, &coefsU[0], numU, chanBits, denShiftU );
                    } else {
                        unpc_block( dec->predictor, dec->predictor, (int32_t)numSamples, nil, 31, chanBits, 0 );
                        unpc_block( dec->predictor, dec->mixBufferU, (int32_t)numSamples, &coefsU[0], numU, chanBits, denShiftU );
                    }

                    set_ag_params( &agParams, dec->config.mb, (uint32_t)(pb * pbFactorV) / 4, dec->config.kb, numSamples, numSamples, dec->config.maxRun );
                    status = dyn_decomp( &agParams, bits, dec->predictor, (int32_t)numSamples, (int32_t)chanBits, &bits2 );
                    RequireNoErr( status, goto Exit; );

                    if ( modeV == 0 ) {
                        unpc_block( dec->predictor, dec->mixBufferV, (int32_t)numSamples, &coefsV[0], numV, chanBits, denShiftV );
                    } else {
                        unpc_block( dec->predictor, dec->predictor, (int32_t)numSamples, nil, 31, chanBits, 0 );
                        unpc_block( dec->predictor, dec->mixBufferV, (int32_t)numSamples, &coefsV[0], numV, chanBits, denShiftV );
                    }
                } else {
                    chanBits = dec->config.bitDepth;
                    shift = 32 - chanBits;
                    if ( chanBits <= 16 ) {
                        for ( i = 0; i < numSamples; i++ ) {
                            val = (int32_t) BitBufferRead( bits, (uint8_t) chanBits );
                            val = (int32_t)((uint32_t)val << shift) >> shift;
                            dec->mixBufferU[i] = val;
                            val = (int32_t) BitBufferRead( bits, (uint8_t) chanBits );
                            val = (int32_t)((uint32_t)val << shift) >> shift;
                            dec->mixBufferV[i] = val;
                        }
                    } else {
                        extraBits = chanBits - 16;
                        for ( i = 0; i < numSamples; i++ ) {
                            val = (int32_t) BitBufferRead( bits, 16 );
                            val = (int32_t)((uint32_t)val << 16) >> shift;
                            dec->mixBufferU[i] = val | (int32_t)BitBufferRead( bits, (uint8_t)extraBits );
                            val = (int32_t) BitBufferRead( bits, 16 );
                            val = (int32_t)((uint32_t)val << 16) >> shift;
                            dec->mixBufferV[i] = val | (int32_t)BitBufferRead( bits, (uint8_t)extraBits );
                        }
                    }
                    bits1 = chanBits * numSamples;
                    bits2 = chanBits * numSamples;
                    (void) bits1; (void) bits2;
                    mixBits = 0; mixRes = 0;
                    bytesShifted = 0;
                }

                if ( bytesShifted != 0 ) {
                    shift = (uint32_t)bytesShifted * 8;
                    for ( i = 0; i < (numSamples * 2); i += 2 ) {
                        dec->shiftBuffer[i + 0] = (uint16_t) BitBufferRead( &shiftBits, (uint8_t) shift );
                        dec->shiftBuffer[i + 1] = (uint16_t) BitBufferRead( &shiftBits, (uint8_t) shift );
                    }
                }

                switch ( dec->config.bitDepth ) {
                    case 16:
                        out16 = &((int16_t *)sampleBuffer)[channelIndex];
                        unmix16( dec->mixBufferU, dec->mixBufferV, out16, numChannels, (int32_t)numSamples, mixBits, mixRes );
                        break;
                    case 20:
                        out20 = (uint8_t *)sampleBuffer + (channelIndex * 3);
                        unmix20( dec->mixBufferU, dec->mixBufferV, out20, numChannels, (int32_t)numSamples, mixBits, mixRes );
                        break;
                    case 24:
                        out24 = (uint8_t *)sampleBuffer + (channelIndex * 3);
                        unmix24( dec->mixBufferU, dec->mixBufferV, out24, numChannels, (int32_t)numSamples,
                                 mixBits, mixRes, dec->shiftBuffer, bytesShifted );
                        break;
                    case 32:
                        out32 = &((int32_t *)sampleBuffer)[channelIndex];
                        unmix32( dec->mixBufferU, dec->mixBufferV, out32, numChannels, (int32_t)numSamples,
                                 mixBits, mixRes, dec->shiftBuffer, bytesShifted );
                        break;
                }

                channelIndex += 2;
                *outNumSamples = numSamples;
                break;
            }

            case ID_CCE:
            case ID_PCE:
                status = kALAC_ParamError;
                break;

            case ID_DSE:
                status = alac_data_stream_element( bits );
                break;

            case ID_FIL:
                status = alac_fill_element( bits );
                break;

            case ID_END:
                BitBufferByteAlign( bits, 0 );
                goto Exit;
        }

        if ( channelIndex >= numChannels ) break;
    }

NoMoreChannels:
    for ( ; channelIndex < numChannels; channelIndex++ ) {
        switch ( dec->config.bitDepth ) {
            case 16: {
                int16_t *fill16 = &((int16_t *)sampleBuffer)[channelIndex];
                alac_zero16( fill16, numSamples, numChannels );
                break;
            }
            case 24: {
                uint8_t *fill24 = (uint8_t *)sampleBuffer + (channelIndex * 3);
                alac_zero24( fill24, numSamples, numChannels );
                break;
            }
            case 32: {
                int32_t *fill32 = &((int32_t *)sampleBuffer)[channelIndex];
                alac_zero32( fill32, numSamples, numChannels );
                break;
            }
        }
    }

Exit:
    return status;
}

/* ==================================================================== */
#endif /* ALAC_IMPLEMENTATION */
/* ==================================================================== */

#endif /* BPPLAY_ALAC_H */
