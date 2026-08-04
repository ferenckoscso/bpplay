/*
 * dsd.h — part of bpplay (macOS Core edition)
 *
 * Copyright (C) 2026 Koscsó Ferenc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * dsd.h — DSF file reading and DoP (DSD over PCM) packing
 *
 * On macOS, DSD playback goes through DoP, because CoreAudio has no native
 * DSD path. DoP packs the DSD bits into 24-bit PCM frames, with a marker
 * byte alternating between 0x05 and 0xFA in the top byte — the DAC
 * recognises this and unpacks it. CRITICAL: the DoP stream must reach the
 * DAC bit-exact and untouched (no volume, no dither, no conversion), or the
 * result is noise rather than music.
 *
 * Three steps:
 *   1. DSF block de-interleave: DSF stores DSD in per-channel blocks of
 *      'blockSize' bytes (4096 in practice); we turn that into per-sample
 *      interleaving.
 *   2. Bit order: DSF is LSB-first, DoP is MSB-first — we reverse each byte.
 *   3. DoP frame building: 2 DSD bytes go into the low 16 bits of a 24-bit
 *      frame, the top byte carries the 0x05/0xFA marker, alternating from
 *      frame to frame.
 *
 * The output goes into the existing AudioSource as if it were 24-bit integer
 * PCM at the matching DoP sample rate — so the rest of the signal path (RAM,
 * mlock, IOProc) handles it UNCHANGED, and the bit-perfect guarantee carries
 * over automatically.
 */

#ifndef DSD_H
#define DSD_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* byte-level bit reversal: LSB-first <-> MSB-first */
static inline uint8_t bit_reverse(uint8_t b)
{
    b = (uint8_t)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = (uint8_t)((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = (uint8_t)((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}

/*
 * DSF reading + DoP packing.
 * Returns 24-bit "PCM" (in fact DoP) in the AudioSource, at the correct DoP
 * sample rate. The bits field is set to 24 so the signal path carries it as
 * integer.
 * outDsdRate: the source DSD sample rate (e.g. 2822400 = DSD64), for display.
 */
static int load_dsf_as_dop(const char *path, AudioSource *s, uint32_t *outDsdRate)
{
    memset(s, 0, sizeof(*s));

    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    if (fseek(f, 0, SEEK_END) != 0) { fprintf(stderr, "DSF: seek error.\n"); fclose(f); return -1; }
    long fsizeL = ftell(f);
    if (fsizeL < 92) { fprintf(stderr, "DSF too short.\n"); fclose(f); return -1; }
    rewind(f);
    const uint64_t fsize = (uint64_t)fsizeL;

    uint8_t *buf = (uint8_t *)malloc((size_t)fsize);
    if (!buf) { fprintf(stderr, "Out of memory (DSF file buffer).\n"); fclose(f); return -1; }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        fprintf(stderr, "DSF read error.\n"); free(buf); fclose(f); return -1;
    }
    fclose(f);

    /* "DSD " chunk */
    if (memcmp(buf, "DSD ", 4) != 0) {
        fprintf(stderr, "Not a DSF file (missing 'DSD ').\n"); free(buf); return -1;
    }
    /* "fmt " chunk at byte 28 */
    const uint64_t fmtPos = 28;
    if (memcmp(buf + fmtPos, "fmt ", 4) != 0) {
        fprintf(stderr, "DSF: missing 'fmt ' chunk.\n"); free(buf); return -1;
    }

    uint32_t channelType, channelNum, dsdRate, bitsPerSample;
    uint64_t sampleCount;
    uint32_t blockSize;
    memcpy(&channelType,   buf + fmtPos + 20, 4);
    memcpy(&channelNum,    buf + fmtPos + 24, 4);
    memcpy(&dsdRate,       buf + fmtPos + 28, 4);
    memcpy(&bitsPerSample, buf + fmtPos + 32, 4);
    memcpy(&sampleCount,   buf + fmtPos + 36, 8);
    memcpy(&blockSize,     buf + fmtPos + 44, 4);
    (void)channelType;

    if (channelNum != 2) {
        fprintf(stderr, "DSF: only stereo is supported (channels=%u).\n", channelNum);
        free(buf); return -1;
    }
    if (bitsPerSample != 1) {
        fprintf(stderr, "DSF: unexpected bitsPerSample=%u.\n", bitsPerSample);
        free(buf); return -1;
    }
    /*
     * Header validation. Everything below comes straight out of the file, so a
     * malformed or truncated DSF must not be able to cause a division by zero,
     * an unsigned wrap-around or an out-of-bounds read.
     *   - blockSize is 4096 per the DSF specification; we accept any non-zero,
     *     even value up to 1 MiB and reject anything else. Even-ness matters:
     *     it guarantees a DSD byte pair never straddles a block boundary.
     *   - dsdRate must be non-zero, since it is the divisor of the DoP rate.
     */
    if (blockSize == 0 || (blockSize & 1u) || blockSize > (1u << 20)) {
        fprintf(stderr, "DSF: invalid block size (%u).\n", blockSize);
        free(buf); return -1;
    }
    if (dsdRate == 0) {
        fprintf(stderr, "DSF: invalid sample rate (0).\n");
        free(buf); return -1;
    }
    if (outDsdRate) *outDsdRate = dsdRate;

    /* "data" chunk after fmt (fmtPos + 52) */
    const uint64_t dataPos = fmtPos + 52;
    if (dataPos + 12 > fsize || memcmp(buf + dataPos, "data", 4) != 0) {
        fprintf(stderr, "DSF: missing 'data' chunk.\n"); free(buf); return -1;
    }
    uint64_t dataChunkSize;
    memcpy(&dataChunkSize, buf + dataPos + 4, 8);

    /* The data chunk size includes its own 12-byte header. A declared size
     * below that would wrap around when we subtract it. */
    if (dataChunkSize < 12) {
        fprintf(stderr, "DSF: bad data chunk size.\n"); free(buf); return -1;
    }
    const uint8_t *dsdData    = buf + dataPos + 12;   /* start of the DSD blocks */
    const uint64_t availBytes = fsize - (dataPos + 12);
    uint64_t dsdBytes = dataChunkSize - 12;
    if (dsdBytes > availBytes) dsdBytes = availBytes; /* truncated file */

    /*
     * DSF block layout: the two channels alternate in blocks of 'blockSize'
     * bytes: [L block][R block][L block][R block]...
     * One block pair = 2*blockSize bytes, i.e. blockSize bytes per channel.
     */
    uint64_t blockPairs    = dsdBytes / (2ULL * blockSize);
    uint64_t dsdBytesPerCh = blockPairs * blockSize;   /* usable bytes per channel */

    /*
     * The last block of a DSF is padded up to blockSize. That padding is not
     * silence in DSD terms, so playing it can produce a DC step or a click at
     * the end of the track. The header's sampleCount field gives the true
     * number of 1-bit samples per channel, so we trim to it.
     */
    if (sampleCount > 0) {
        uint64_t realBytesPerCh = sampleCount / 8;
        if (realBytesPerCh > 0 && realBytesPerCh < dsdBytesPerCh)
            dsdBytesPerCh = realBytesPerCh;
    }
    if (dsdBytesPerCh < 2) {
        fprintf(stderr, "DSF: no audio data.\n"); free(buf); return -1;
    }

    /*
     * DoP frame: 2 DSD bytes (from one channel) -> one 24-bit PCM sample.
     * Stereo: every DoP frame = 2 * 24-bit samples (L,R) = 6 bytes.
     * The DoP PCM sample rate is dsdRate / 16 (16 DSD bits = 2 bytes ride in
     * a single frame).
     */
    uint64_t dopFramesPerCh = dsdBytesPerCh / 2;      /* 2 DSD bytes per DoP sample */
    uint64_t allocBytes = dopFramesPerCh * 2 /*ch*/ * 3 /*24-bit*/;

    s->data = (uint8_t *)malloc((size_t)allocBytes);
    if (!s->data) { fprintf(stderr, "Out of memory (DoP buffer).\n"); free(buf); return -1; }

    uint8_t *out = s->data;
    uint8_t marker = 0x05;        /* DoP marker, alternating 0x05 <-> 0xFA */

    /*
     * Walk the DoP frames. Every frame needs 2 DSD bytes from both channels;
     * because of the DSF block interleave those bytes have to be picked out of
     * the right blocks.
     */
    for (uint64_t fr = 0; fr < dopFramesPerCh; fr++) {
        uint64_t chByteIdx = fr * 2;                   /* which byte pair in the channel */
        uint64_t blk       = chByteIdx / blockSize;    /* which block */
        uint64_t inBlk     = chByteIdx % blockSize;    /* offset inside the block */
        uint64_t lBase = (blk * 2ULL) * blockSize + inBlk;        /* L block */
        uint64_t rBase = (blk * 2ULL + 1ULL) * blockSize + inBlk; /* R block */

        if (rBase + 1 >= dsdBytes) break;              /* never read past the data */

        /* DSD bytes (LSB-first in DSF) -> MSB-first for DoP */
        uint8_t l0 = bit_reverse(dsdData[lBase]);
        uint8_t l1 = bit_reverse(dsdData[lBase + 1]);
        uint8_t r0 = bit_reverse(dsdData[rBase]);
        uint8_t r1 = bit_reverse(dsdData[rBase + 1]);

        /* L sample: 24-bit, top byte = marker, low 16 bits = 2 DSD bytes,
         * stored little-endian: [low][mid][high] */
        out[0] = l1;        /* lowest byte  */
        out[1] = l0;        /* middle byte  */
        out[2] = marker;    /* top byte: DoP marker */
        /* R sample */
        out[3] = r1;
        out[4] = r0;
        out[5] = marker;
        out += 6;

        /* The marker alternates every 16 DSD bits (2 bytes), i.e. every frame. */
        marker = (marker == 0x05) ? 0xFA : 0x05;
    }

    /* If the bounds check cut the loop short, report what was actually
     * written rather than what was allocated. */
    uint64_t outBytes = (uint64_t)(out - s->data);
    if (outBytes == 0) {
        fprintf(stderr, "DSF: no usable audio data.\n");
        free(s->data); s->data = NULL; free(buf); return -1;
    }

    free(buf);

    /*
     * DoP PCM sample rate = dsdRate / 16. The formula is rate-independent, so
     * the whole DoP chain handles higher DSD rates automatically:
     *   DSD64  (2 822 400 Hz) -> DoP 176 400 Hz
     *   DSD128 (5 644 800 Hz) -> DoP 352 800 Hz
     *   DSD256 (11 289 600 Hz)-> DoP 705 600 Hz
     * Bit reversal, block de-interleave and DoP frame building are all
     * rate-independent (they only repack bytes), so they stay correct.
     * NOTE: the higher DoP rates (352.8/705.6 kHz) do not pass on every
     * DAC/USB chain — if the DAC cannot take the rate, set_sample_rate fails
     * honestly and the caller reports it.
     */
    s->sampleRate = dsdRate / 16;
    s->channels   = 2;
    s->bits       = 24;               /* the signal path carries it as 24-bit integer */
    s->isFloat    = 0;
    s->dataBytes  = outBytes;
    return 0;
}

/* The DSD rate in human terms: 64 / 128 / 256 (based on DSD64 = 2.8224 MHz).
 * Returns the multiple (64, 128, 256, ...), or 0 if it is not recognised. */
static inline unsigned dsd_multiple(uint32_t dsdRate)
{
    /* DSD64 base rate = 44100 * 64 = 2 822 400 Hz */
    if (dsdRate != 0 && dsdRate % 2822400u == 0) return 64u * (dsdRate / 2822400u);
    return 0;
}

#endif /* DSD_H */
