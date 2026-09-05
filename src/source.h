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

/* ------------------------------------------------------------------ */
/* M4A / ALAC loading                                                   */
/* ------------------------------------------------------------------ */
/*
 * ALAC (Apple Lossless) frames are, like FLAC, a lossless predictive codec —
 * the decoded PCM is bit-exact to what the encoder was given. Unlike FLAC,
 * an ALAC bitstream has no self-delimiting frame sync codes, so it can only
 * be decoded from inside its container (here, M4A/MP4): the container's
 * sample tables (stsz/stsc/stco) are what tell us where each compressed
 * frame starts and ends in the file.
 *
 * This is therefore two things layered together: a minimal MP4 atom walker
 * (just enough to find the 'alac' track's magic cookie and sample tables —
 * no video/chapter track or edit-list support), and the alac.h decoder
 * vendored above. Gapless edit lists (elst) are not applied: a track may
 * carry a few dozen milliseconds of encoder padding at its boundaries. The
 * codec itself remains lossless regardless.
 */

#include "alac.h"

static uint32_t m4a_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint64_t m4a_be64(const uint8_t *p) {
    return ((uint64_t)m4a_be32(p) << 32) | (uint64_t)m4a_be32(p + 4);
}
static uint16_t m4a_be16(const uint8_t *p) {
    return (uint16_t)(((uint32_t)p[0] << 8) | (uint32_t)p[1]);
}

/* Advances *cursor to the next box in [*cursor, end); returns 1 and fills
 * outType/outOff/outSize on success, 0 when there is no further box. */
static int m4a_next_box(const uint8_t *buf, uint64_t *cursor, uint64_t end,
                         char outType[5], uint64_t *outOff, uint64_t *outSize)
{
    uint64_t p = *cursor;
    if (p + 8 > end) return 0;
    uint64_t boxSize = m4a_be32(buf + p);
    uint64_t headerSize = 8;
    if (boxSize == 1) {
        if (p + 16 > end) return 0;
        boxSize = m4a_be64(buf + p + 8);
        headerSize = 16;
    } else if (boxSize == 0) {
        boxSize = end - p;
    }
    if (boxSize < headerSize || p + boxSize > end || p + boxSize < p) return 0;
    memcpy(outType, buf + p + 4, 4); outType[4] = '\0';
    *outOff  = p + headerSize;
    *outSize = boxSize - headerSize;
    *cursor  = p + boxSize;
    return 1;
}

/* First child box of `type` directly inside [start, start+size). */
static int m4a_find_child(const uint8_t *buf, uint64_t start, uint64_t size, const char *type,
                           uint64_t *outOff, uint64_t *outSize)
{
    uint64_t cur = start, end = start + size;
    char t[5];
    uint64_t off, sz;
    while (m4a_next_box(buf, &cur, end, t, &off, &sz)) {
        if (memcmp(t, type, 4) == 0) { *outOff = off; *outSize = sz; return 1; }
    }
    return 0;
}

/* Reads one ILST-style text tag ("©nam"/"©ART"/"©alb"): a box containing a
 * nested 'data' box, itself version+flags(4) + reserved(4) + UTF-8 text. */
static void m4a_read_ilst_tag(const uint8_t *buf, uint64_t off, uint64_t size, char *out, size_t outsz)
{
    uint64_t dataOff, dataSize;
    if (!m4a_find_child(buf, off, size, "data", &dataOff, &dataSize)) return;
    if (dataSize < 8) return;
    uint64_t textOff = dataOff + 8, textLen = dataSize - 8;
    if (textLen >= outsz) textLen = outsz - 1;
    memcpy(out, buf + textOff, (size_t)textLen);
    out[textLen] = '\0';
}

static void m4a_read_metadata(const uint8_t *buf, uint64_t moovOff, uint64_t moovSize, AudioMeta *m)
{
    uint64_t udtaOff, udtaSize, metaOff, metaSize, ilstOff, ilstSize;
    if (!m4a_find_child(buf, moovOff, moovSize, "udta", &udtaOff, &udtaSize)) return;
    if (!m4a_find_child(buf, udtaOff, udtaSize, "meta", &metaOff, &metaSize)) return;
    /* 'meta' is a FullBox: 4 bytes of version/flags precede its children. */
    if (metaSize < 4) return;
    if (!m4a_find_child(buf, metaOff + 4, metaSize - 4, "ilst", &ilstOff, &ilstSize)) return;

    uint64_t cur = ilstOff, end = ilstOff + ilstSize;
    char t[5]; uint64_t off, sz;
    while (m4a_next_box(buf, &cur, end, t, &off, &sz)) {
        if (memcmp(t, "\xA9" "nam", 4) == 0) m4a_read_ilst_tag(buf, off, sz, m->title, sizeof(m->title));
        else if (memcmp(t, "\xA9" "ART", 4) == 0) m4a_read_ilst_tag(buf, off, sz, m->artist, sizeof(m->artist));
        else if (memcmp(t, "\xA9" "alb", 4) == 0) m4a_read_ilst_tag(buf, off, sz, m->album, sizeof(m->album));
    }
}

static int load_alac(const char *path, AudioSource *s, AudioMeta *m)
{
    memset(s, 0, sizeof(*s));

    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    if (fseek(f, 0, SEEK_END) != 0) { fprintf(stderr, "M4A: seek error.\n"); fclose(f); return -1; }
    long fsizeL = ftell(f);
    if (fsizeL < 16) { fprintf(stderr, "M4A: file too short.\n"); fclose(f); return -1; }
    rewind(f);
    const uint64_t fsize = (uint64_t)fsizeL;

    uint8_t *buf = (uint8_t *)malloc((size_t)fsize);
    if (!buf) { fprintf(stderr, "Out of memory (M4A file).\n"); fclose(f); return -1; }
    if (fread(buf, 1, (size_t)fsize, f) != fsize) {
        fprintf(stderr, "M4A: read error.\n"); fclose(f); free(buf); return -1;
    }
    fclose(f);

    /* top level: find 'moov' and 'mdat' (order varies by encoder) */
    uint64_t moovOff = 0, moovSize = 0, mdatOff = 0, mdatSize = 0;
    int haveMoov = 0, haveMdat = 0;
    {
        uint64_t cur = 0; char t[5]; uint64_t off, sz;
        while (m4a_next_box(buf, &cur, fsize, t, &off, &sz)) {
            if (memcmp(t, "moov", 4) == 0) { moovOff = off; moovSize = sz; haveMoov = 1; }
            else if (memcmp(t, "mdat", 4) == 0) { mdatOff = off; mdatSize = sz; haveMdat = 1; }
        }
    }
    if (!haveMoov || !haveMdat) {
        fprintf(stderr, "M4A: not a valid MP4 container (missing moov/mdat): %s\n", path);
        free(buf); return -1;
    }

    /* walk each trak until one has an 'alac' sample description */
    ALACSpecificConfig cfg; memset(&cfg, 0, sizeof(cfg));
    uint64_t stszOff = 0, stszSize = 0, stscOff = 0, stscSize = 0;
    uint64_t stcoOff = 0, stcoSize = 0; int stcoIs64 = 0;
    int haveTrack = 0;
    int sawAac = 0;   /* seen an 'mp4a' (AAC) track — for a clearer error message */

    {
        uint64_t trakCur = moovOff, trakEnd = moovOff + moovSize;
        char tt[5]; uint64_t trakOff, trakSize;
        while (!haveTrack && m4a_next_box(buf, &trakCur, trakEnd, tt, &trakOff, &trakSize)) {
            if (memcmp(tt, "trak", 4) != 0) continue;

            uint64_t mdiaOff, mdiaSize, minfOff, minfSize, stblOff, stblSize, stsdOff, stsdSize;
            if (!m4a_find_child(buf, trakOff, trakSize, "mdia", &mdiaOff, &mdiaSize)) continue;
            if (!m4a_find_child(buf, mdiaOff, mdiaSize, "minf", &minfOff, &minfSize)) continue;
            if (!m4a_find_child(buf, minfOff, minfSize, "stbl", &stblOff, &stblSize)) continue;
            if (!m4a_find_child(buf, stblOff, stblSize, "stsd", &stsdOff, &stsdSize)) continue;
            if (stsdSize < 8) continue;

            /* stsd is a FullBox (4 bytes version/flags + 4 bytes entry_count)
             * whose entries are themselves box-shaped ('alac' SampleEntry). */
            uint64_t entryCur = stsdOff + 8, entryEnd = stsdOff + stsdSize;
            char et[5]; uint64_t entryOff, entrySize;
            int foundAlac = 0;
            while (m4a_next_box(buf, &entryCur, entryEnd, et, &entryOff, &entrySize)) {
                if (memcmp(et, "mp4a", 4) == 0) sawAac = 1;
                if (memcmp(et, "alac", 4) != 0) continue;
                /* AudioSampleEntry fixed fields: reserved(6)+data_ref_index(2)
                 * +reserved(8)+channelcount(2)+samplesize(2)+pre_defined(2)
                 * +reserved(2)+samplerate(4) = 28 bytes, then child boxes. */
                if (entrySize < 28) continue;
                uint64_t cookieOff, cookieSize;
                if (!m4a_find_child(buf, entryOff + 28, entrySize - 28, "alac", &cookieOff, &cookieSize)) continue;
                /* the nested 'alac' box is itself a FullBox: 4 bytes of
                 * version/flags precede the 24-byte ALACSpecificConfig */
                if (cookieSize < 4 + 24) continue;

                const uint8_t *c = buf + cookieOff + 4;
                cfg.frameLength         = m4a_be32(c + 0);
                cfg.compatibleVersion   = c[4];
                cfg.bitDepth            = c[5];
                cfg.pb                  = c[6];
                cfg.mb                  = c[7];
                cfg.kb                  = c[8];
                cfg.numChannels         = c[9];
                cfg.maxRun              = m4a_be16(c + 10);
                cfg.maxFrameBytes       = m4a_be32(c + 12);
                cfg.avgBitRate          = m4a_be32(c + 16);
                cfg.sampleRate          = m4a_be32(c + 20);
                foundAlac = 1;
                break;
            }
            if (!foundAlac) continue;

            if (!m4a_find_child(buf, stblOff, stblSize, "stsz", &stszOff, &stszSize)) continue;
            if (!m4a_find_child(buf, stblOff, stblSize, "stsc", &stscOff, &stscSize)) continue;
            if (m4a_find_child(buf, stblOff, stblSize, "stco", &stcoOff, &stcoSize)) stcoIs64 = 0;
            else if (m4a_find_child(buf, stblOff, stblSize, "co64", &stcoOff, &stcoSize)) stcoIs64 = 1;
            else continue;

            haveTrack = 1;
        }
    }
    if (!haveTrack) {
        if (sawAac) {
            /* The single most likely real-world case: an iTunes/Music.app
             * purchase or an AAC rip. M4A is just a container — it does not
             * imply lossless. Say so plainly instead of a codec-jargon error,
             * since this path is reachable straight from a Finder right-click. */
            fprintf(stderr,
                "This M4A file is AAC (a lossy format), not Apple Lossless (ALAC).\n"
                "bpplay only decodes lossless audio — it does not play AAC: %s\n", path);
        } else {
            fprintf(stderr, "M4A: no ALAC audio track found: %s\n", path);
        }
        free(buf); return -1;
    }
    if (cfg.bitDepth != 16 && cfg.bitDepth != 24) {
        /* ALAC also allows 20/32-bit; this build handles the two depths that
         * occur in practice (iTunes/Music.app only ever produce 16 or 24). */
        fprintf(stderr, "ALAC bit depth %u — this build handles 16 and 24-bit.\n", cfg.bitDepth);
        free(buf); return -1;
    }
    if (cfg.numChannels == 0 || cfg.sampleRate == 0) {
        fprintf(stderr, "ALAC: implausible stream header (%u ch, %u Hz).\n", cfg.numChannels, cfg.sampleRate);
        free(buf); return -1;
    }

    /* stsz: version+flags(4) + sample_size(4) + sample_count(4) [+ per-sample
     * sizes if sample_size==0, which ALAC always uses]. */
    if (stszSize < 12) { fprintf(stderr, "M4A: malformed stsz.\n"); free(buf); return -1; }
    const uint8_t *stsz = buf + stszOff;
    uint32_t uniformSize = m4a_be32(stsz + 4);
    uint32_t nSamples    = m4a_be32(stsz + 8);
    if (uniformSize != 0 || nSamples == 0) {
        fprintf(stderr, "M4A: unsupported stsz layout.\n"); free(buf); return -1;
    }
    if (stszSize < 12 + (uint64_t)nSamples * 4) { fprintf(stderr, "M4A: truncated stsz.\n"); free(buf); return -1; }
    const uint8_t *sizeTable = stsz + 12;

    /* stco/co64: version+flags(4) + entry_count(4) + offsets */
    if (stcoSize < 8) { fprintf(stderr, "M4A: malformed stco.\n"); free(buf); return -1; }
    const uint8_t *stco = buf + stcoOff;
    uint32_t nChunks = m4a_be32(stco + 4);
    uint32_t chunkEntryBytes = stcoIs64 ? 8 : 4;
    if (nChunks == 0 || stcoSize < 8 + (uint64_t)nChunks * chunkEntryBytes) {
        fprintf(stderr, "M4A: truncated stco.\n"); free(buf); return -1;
    }
    const uint8_t *chunkTable = stco + 8;

    /* stsc: version+flags(4) + entry_count(4) + (first_chunk, samples_per_chunk,
     * sample_description_index) triples, 12 bytes each, run-length encoded. */
    if (stscSize < 8) { fprintf(stderr, "M4A: malformed stsc.\n"); free(buf); return -1; }
    const uint8_t *stsc = buf + stscOff;
    uint32_t nStsc = m4a_be32(stsc + 4);
    if (nStsc == 0 || stscSize < 8 + (uint64_t)nStsc * 12) {
        fprintf(stderr, "M4A: truncated stsc.\n"); free(buf); return -1;
    }
    const uint8_t *stscTable = stsc + 8;

    /* --- allocate output PCM buffer (upper bound: nSamples full frames) --- */
    s->sampleRate = cfg.sampleRate;
    s->channels   = cfg.numChannels;
    s->bits       = cfg.bitDepth;
    s->isFloat    = 0;

    const uint32_t bps = s->bits / 8;
    if ((uint64_t)nSamples > (UINT64_MAX / cfg.frameLength) / s->channels / bps) {
        fprintf(stderr, "M4A: stream too large.\n"); free(buf); return -1;
    }
    uint64_t maxBytes = (uint64_t)nSamples * cfg.frameLength * s->channels * bps;
    uint8_t *pcm = (uint8_t *)malloc((size_t)maxBytes);
    if (!pcm) { fprintf(stderr, "Out of memory (ALAC data).\n"); free(buf); return -1; }

    ALACDecoder dec;
    alac_decoder_init(&dec);
    if (alac_decoder_configure(&dec, &cfg) != ALAC_noErr) {
        fprintf(stderr, "ALAC: decoder configure failed: %s\n", path);
        free(pcm); free(buf); return -1;
    }

    uint64_t totalPcmBytes = 0;
    uint32_t sampleIdx = 0;
    int decodeError = 0;

    for (uint32_t chunk = 0; chunk < nChunks && sampleIdx < nSamples && !decodeError; chunk++) {
        /* samples_per_chunk for this (1-based) chunk index, from the
         * run-length stsc table */
        uint32_t chunkNo = chunk + 1;
        uint32_t samplesPerChunk = 1;
        for (uint32_t e = 0; e < nStsc; e++) {
            uint32_t firstChunk = m4a_be32(stscTable + e * 12);
            uint32_t nextFirst  = (e + 1 < nStsc) ? m4a_be32(stscTable + (e + 1) * 12) : 0xFFFFFFFFu;
            if (chunkNo >= firstChunk && chunkNo < nextFirst) {
                samplesPerChunk = m4a_be32(stscTable + e * 12 + 4);
                break;
            }
        }

        uint64_t chunkOffset = stcoIs64 ? m4a_be64(chunkTable + (uint64_t)chunk * 8)
                                         : m4a_be32(chunkTable + (uint64_t)chunk * 4);

        for (uint32_t si = 0; si < samplesPerChunk && sampleIdx < nSamples; si++, sampleIdx++) {
            uint32_t frameSize = m4a_be32(sizeTable + (uint64_t)sampleIdx * 4);
            if (frameSize == 0 || chunkOffset < mdatOff ||
                chunkOffset + frameSize > mdatOff + mdatSize) {
                fprintf(stderr, "M4A: sample %u out of range in %s\n", sampleIdx, path);
                decodeError = 1; break;
            }

            BitBuffer bb;
            BitBufferInit(&bb, buf + chunkOffset, frameSize);
            uint32_t gotSamples = 0;
            int32_t rc = alac_decoder_decode(&dec, &bb, pcm + totalPcmBytes,
                                              cfg.frameLength, s->channels, &gotSamples);
            if (rc != ALAC_noErr) {
                fprintf(stderr, "ALAC: decode error in frame %u of %s\n", sampleIdx, path);
                decodeError = 1; break;
            }
            totalPcmBytes += (uint64_t)gotSamples * s->channels * bps;
            chunkOffset += frameSize;
        }
    }

    alac_decoder_free(&dec);

    if (decodeError || totalPcmBytes == 0) {
        free(pcm); free(buf); return -1;
    }

    s->data      = pcm;
    s->dataBytes = totalPcmBytes;

    if (m) m4a_read_metadata(buf, moovOff, moovSize, m);
    free(buf);
    return 0;
}

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
