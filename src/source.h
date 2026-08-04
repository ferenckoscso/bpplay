/*
 * source.h — part of bpplay (macOS Core edition)
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
 * source.h — the source layer of the bit-perfect player
 *
 * A single AudioSource abstraction: WAV, FLAC, AIFF and DSF all decode into
 * it, and the signal path (convert_to_virtual, IOProc) works with that one
 * common structure regardless of format. The bit-perfect principle stays
 * intact:
 *   - For FLAC, dr_flac returns integer (s32) samples, which we shift back
 *     to the true source bit depth WITHOUT dither and WITHOUT volume.
 *   - The metadata layer only READS; it never writes the file.
 *   - MQA handling DETECTS, it does not decode (it does not touch the
 *     licensed process).
 */

#ifndef SOURCE_H
#define SOURCE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Common source structure                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t sampleRate;
    uint16_t channels;
    uint16_t bits;          /* the TRUE source bit depth: 16 / 24 / 32 */
    int      isFloat;       /* 1 = the source is IEEE float (e.g. 32-bit float WAV) */
    uint8_t *data;          /* decoded, interleaved samples             */
    uint64_t dataBytes;     /* length of data in bytes                  */
} AudioSource;

/* Descriptive metadata (for the UI) */
typedef struct {
    char     title[256];
    char     artist[256];
    char     album[256];
    /* MQA detection */
    int      mqaDetected;          /* 1 = MQA candidate (based on metadata)   */
    uint32_t mqaOriginalRate;      /* ORIGINALSAMPLERATE if present, else 0   */
    char     mqaEncoder[128];      /* MQAENCODER field if present             */
} AudioMeta;

/*
 * Sanity check for a decoded source. Every loader runs this before handing
 * the buffer on: the fields below are used as divisors and as loop bounds
 * further down the chain, so a zero or absurd value read out of a malformed
 * file must be caught here rather than causing a division by zero or an
 * out-of-bounds access later.
 */
static int audio_source_valid(const AudioSource *s)
{
    if (!s || !s->data) return 0;
    if (s->channels == 0 || s->channels > 64) return 0;
    if (s->sampleRate == 0) return 0;
    if (s->bits != 16 && s->bits != 24 && s->bits != 32) return 0;
    if (s->dataBytes < (uint64_t)(s->bits / 8) * s->channels) return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* FLAC loading (dr_flac)                                              */
/* ------------------------------------------------------------------ */
/*
 * dr_flac returns s32 (32-bit signed) samples in which the meaningful data
 * sits in the top 'bitsPerSample' bits, MSB-aligned. We pack that back down
 * to the true source bit depth (16/24 bits), little-endian and packed —
 * exactly what our signal path expects. This is lossless: the sample dr_flac
 * hands back is exact within the source bit depth.
 */

#include "dr_flac.h"

/* forward declaration (definition further down) */
static void read_flac_metadata(const char *path, AudioMeta *m);

static int load_flac(const char *path, AudioSource *s, AudioMeta *m)
{
    memset(s, 0, sizeof(*s));

    drflac *pf = drflac_open_file(path, NULL);
    if (!pf) {
        fprintf(stderr, L(MSG_NO_FLAC), path);
        return -1;
    }

    s->sampleRate = pf->sampleRate;
    s->channels   = (uint16_t)pf->channels;
    s->bits       = (uint16_t)pf->bitsPerSample;   /* the true source depth */

    if (s->bits != 16 && s->bits != 24) {
        /* dr_flac also knows 8/12/20/32 bit; this build handles the two
         * depths that occur in practice. */
        fprintf(stderr, "FLAC bit depth %u — this build handles 16 and 24-bit.\n", s->bits);
        drflac_close(pf);
        return -1;
    }
    if (s->channels == 0 || s->channels > 64 || s->sampleRate == 0) {
        fprintf(stderr, "FLAC: implausible stream header (%u ch, %u Hz).\n",
                s->channels, s->sampleRate);
        drflac_close(pf);
        return -1;
    }

    uint64_t frames = pf->totalPCMFrameCount;
    if (frames == 0) {
        fprintf(stderr, "FLAC: empty stream.\n");
        drflac_close(pf);
        return -1;
    }
    /* Guard the two multiplications below against overflow on absurd headers. */
    if (frames > (UINT64_MAX / 4) / s->channels) {
        fprintf(stderr, "FLAC: stream too large.\n");
        drflac_close(pf);
        return -1;
    }
    uint64_t nsamp = frames * s->channels;

    /* temporary s32 buffer for dr_flac's output */
    drflac_int32 *tmp = (drflac_int32 *)malloc((size_t)(nsamp * sizeof(drflac_int32)));
    if (!tmp) { fprintf(stderr, "Out of memory (FLAC tmp).\n"); drflac_close(pf); return -1; }

    uint64_t got = drflac_read_pcm_frames_s32(pf, frames, tmp);
    drflac_close(pf);
    if (got != frames) {
        fprintf(stderr, "FLAC decode truncated (%llu/%llu frames).\n",
                (unsigned long long)got, (unsigned long long)frames);
        /* not fatal: carry on with the frames that were actually read */
        frames = got;
        nsamp  = frames * s->channels;
    }
    if (frames == 0) { fprintf(stderr, "FLAC: no frames decoded.\n"); free(tmp); return -1; }

    const uint32_t bps = s->bits / 8;            /* target bytes per sample: 2 or 3 */
    s->dataBytes = nsamp * bps;
    s->data = (uint8_t *)malloc((size_t)s->dataBytes);
    if (!s->data) { fprintf(stderr, "Out of memory (FLAC data).\n"); free(tmp); return -1; }

    /*
     * s32 -> source bit depth: shift the MSB-aligned data down.
     * In dr_flac's s32 the useful data is in the TOP bitsPerSample bits.
     * 16-bit: >>16, 24-bit: >>8. Little-endian, packed output.
     */
    const int shift = 32 - s->bits;
    uint8_t *d = s->data;
    for (uint64_t i = 0; i < nsamp; i++) {
        int32_t v = tmp[i] >> shift;
        d[0] = (uint8_t)(v & 0xFF);
        d[1] = (uint8_t)((v >> 8) & 0xFF);
        if (bps == 3) d[2] = (uint8_t)((v >> 16) & 0xFF);
        d += bps;
    }
    free(tmp);

    /* metadata: dr_flac exposes Vorbis comments through a separate API — below */
    if (m) read_flac_metadata(path, m);
    return 0;
}

/* ------------------------------------------------------------------ */
/* FLAC metadata + MQA detection (Vorbis comment)                      */
/* ------------------------------------------------------------------ */
/*
 * dr_flac delivers the Vorbis comments through a metadata callback. We pick
 * out the display fields (TITLE/ARTIST/ALBUM) and the MQA markers
 * (MQAENCODER, ORIGINALSAMPLERATE). This is purely a read.
 */

static void vorbis_field(const char *comment, uint32_t len,
                         const char *key, char *out, size_t outsz)
{
    size_t klen = strlen(key);
    if (len > klen &&
        strncasecmp(comment, key, klen) == 0 &&
        comment[klen] == '=') {
        uint32_t vlen = len - (uint32_t)klen - 1;
        if (vlen >= outsz) vlen = (uint32_t)outsz - 1;
        memcpy(out, comment + klen + 1, vlen);
        out[vlen] = '\0';
    }
}

static void meta_callback(void *pUserData, drflac_metadata *pMeta)
{
    AudioMeta *m = (AudioMeta *)pUserData;
    if (pMeta->type != DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) return;

    drflac_vorbis_comment_iterator it;
    drflac_init_vorbis_comment_iterator(&it,
        pMeta->data.vorbis_comment.commentCount,
        pMeta->data.vorbis_comment.pComments);

    drflac_uint32 clen;
    const char *c;
    char tmp[256];
    while ((c = drflac_next_vorbis_comment(&it, &clen)) != NULL) {
        vorbis_field(c, clen, "TITLE",  m->title,  sizeof(m->title));
        vorbis_field(c, clen, "ARTIST", m->artist, sizeof(m->artist));
        vorbis_field(c, clen, "ALBUM",  m->album,  sizeof(m->album));
        vorbis_field(c, clen, "MQAENCODER", m->mqaEncoder, sizeof(m->mqaEncoder));

        tmp[0] = '\0';
        vorbis_field(c, clen, "ORIGINALSAMPLERATE", tmp, sizeof(tmp));
        if (tmp[0]) m->mqaOriginalRate = (uint32_t)strtoul(tmp, NULL, 10);
    }
    if (m->mqaEncoder[0] || m->mqaOriginalRate) m->mqaDetected = 1;
}

static void read_flac_metadata(const char *path, AudioMeta *m)
{
    /* a separate open with the metadata callback (the main decode does not
     * hand these back) */
    drflac *pf = drflac_open_file_with_metadata(path, meta_callback, m, NULL);
    if (pf) drflac_close(pf);
}

#endif /* SOURCE_H */
