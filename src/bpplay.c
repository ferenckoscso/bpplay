/*
 * bpplay.c — part of bpplay (macOS Core edition)
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
 * bpplay.c — a bit-perfect audio player for macOS (CoreAudio HAL)
 *
 * Philosophy: the shortest possible deterministic signal path.
 *   - The whole file is decoded into RAM and pinned there with mlock
 *   - Hog mode: exclusive device access, the system mixer is excluded
 *   - Integer mode: the stream's physical AND virtual format are integer
 *     where the device supports it — then zero runtime conversion happens
 *   - The single format-matching step runs once, at load time, losslessly
 *     (integer bit shift); the realtime IOProc does nothing but memcpy
 *   - No software volume, dither, sample-rate conversion or DSP anywhere
 *     in the chain
 *
 * Build:
 *   clang -O2 bpplay.c -o bpplay \
 *     -framework CoreAudio -framework CoreFoundation -framework IOKit
 *
 * Usage:
 *   ./bpplay -l                  list output devices
 *   ./bpplay music.wav           play on the default output
 *   ./bpplay -d 3 music.wav      play on device index 3
 *   ./bpplay -d 3 -dir ~/Album   play a whole folder, recursively
 *
 * Supported input: WAV (16/24/32-bit integer and 32-bit float), FLAC
 * (16/24-bit), AIFF/AIFC (uncompressed), DSF (DSD64–DSD256 over DoP).
 * Any channel count and sample rate the DAC accepts.
 */

/* --------------------------------------------------------------- */
/* Version — the single source of truth (bump this at release time) */
/* --------------------------------------------------------------- */
#define BPPLAY_VERSION "0.9.1"
#define BPPLAY_EDITION "macOS Core"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <termios.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread/qos.h>

#include <AvailabilityMacros.h>
#if !defined(MAC_OS_VERSION_12_0) || MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_VERSION_12_0
#define kAudioObjectPropertyElementMain kAudioObjectPropertyElementMaster
#endif

/* The FLAC decoder implementation is pulled in ONCE, here. source.h uses the
 * dr_flac API afterwards. */
#define DR_FLAC_IMPLEMENTATION
#include "lang.h"
#include "source.h"
#include "dsd.h"

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

/*
 * Cut the sample data down to a whole number of frames. A truncated or badly
 * edited file can end mid-frame; every size calculation downstream assumes
 * dataBytes is an exact multiple of the frame size, so we enforce that here
 * rather than letting a partial frame turn into an out-of-bounds access.
 */
static void trim_to_whole_frames(AudioSource *w)
{
    if (!w || w->channels == 0 || w->bits == 0) return;
    uint64_t frameSize = (uint64_t)(w->bits / 8) * w->channels;
    if (frameSize == 0) return;
    w->dataBytes -= (w->dataBytes % frameSize);
}

/* ------------------------------------------------------------------ */
/* WAV reading (loads into the shared AudioSource)                     */
/* ------------------------------------------------------------------ */

static int parse_wav(const char *path, AudioSource *w)
{
    memset(w, 0, sizeof(*w));

    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    if (fseek(f, 0, SEEK_END) != 0) { fprintf(stderr, "WAV: seek error.\n"); fclose(f); return -1; }
    long fsizeL = ftell(f);
    if (fsizeL < 44) { fprintf(stderr, "WAV: file too short.\n"); fclose(f); return -1; }
    rewind(f);
    const uint64_t fsize = (uint64_t)fsizeL;

    uint8_t *buf = (uint8_t *)malloc((size_t)fsize);
    if (!buf) { fprintf(stderr, "Out of memory (WAV file buffer).\n"); fclose(f); return -1; }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        fprintf(stderr, "WAV: read error.\n");
        free(buf); fclose(f); return -1;
    }
    fclose(f);

    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "Not a RIFF/WAVE file.\n");
        free(buf); return -1;
    }

    int haveFmt = 0;
    uint64_t pos = 12;
    while (pos + 8 <= fsize) {
        char id[4];
        uint32_t csz;
        memcpy(id, buf + pos, 4);
        memcpy(&csz, buf + pos + 4, 4);
        uint64_t body = pos + 8;
        if (body + csz > fsize) break;

        if (memcmp(id, "fmt ", 4) == 0 && csz >= 16) {
            uint16_t tag;
            memcpy(&tag,            buf + body,      2);
            memcpy(&w->channels,    buf + body + 2,  2);
            memcpy(&w->sampleRate,  buf + body + 4,  4);
            memcpy(&w->bits,        buf + body + 14, 2);
            if (tag == 0xFFFE && csz >= 40)          /* WAVE_FORMAT_EXTENSIBLE */
                memcpy(&tag, buf + body + 24, 2);    /* the real format is the first 2 bytes of the subformat */
            /* tag 1 = integer PCM, tag 3 = IEEE float. Both are supported. */
            if (tag == 3) {
                w->isFloat = 1;
            } else if (tag == 1) {
                w->isFloat = 0;
            } else {
                fprintf(stderr, "Unsupported WAV format tag=%u (only PCM=1 and float=3).\n", tag);
                free(buf); return -1;
            }
            haveFmt = 1;
        } else if (memcmp(id, "data", 4) == 0) {
            /* A badly edited WAV can contain more than one data chunk. If we
             * already have data, free it before overwriting the pointer, or it
             * would leak. (The first data chunk is the one that counts; the
             * rest would simply be overwritten by the malloc.) */
            if (w->data) { free(w->data); w->data = NULL; w->dataBytes = 0; }
            w->data = (uint8_t *)malloc(csz ? csz : 1);
            if (!w->data) { fprintf(stderr, "Out of memory (WAV data).\n"); free(buf); return -1; }
            memcpy(w->data, buf + body, csz);
            w->dataBytes = csz;
        }
        pos = body + csz + (csz & 1);                /* chunks are padded to even byte counts */
    }
    free(buf);

    if (!haveFmt || !w->data) {
        fprintf(stderr, "WAV: missing fmt or data chunk.\n");
        free(w->data); w->data = NULL; return -1;
    }
    if (w->bits != 16 && w->bits != 24 && w->bits != 32) {
        fprintf(stderr, "WAV: unsupported bit depth: %u.\n", w->bits);
        free(w->data); w->data = NULL; return -1;
    }
    /* channels and sampleRate come straight out of the file header and are
     * used as divisors downstream — a zero here must not reach that code. */
    if (w->channels == 0 || w->channels > 64 || w->sampleRate == 0) {
        fprintf(stderr, "WAV: implausible header (%u ch, %u Hz).\n",
                w->channels, w->sampleRate);
        free(w->data); w->data = NULL; return -1;
    }
    trim_to_whole_frames(w);
    if (!audio_source_valid(w)) {
        fprintf(stderr, "WAV: no usable audio data.\n");
        free(w->data); w->data = NULL; return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* AIFF (.aif / .aiff) reading                                         */
/*                                                                     */
/* AIFF is the uncompressed-PCM twin of WAV, but:                      */
/*   - the container is made of FORM/COMM/SSND chunks (not RIFF/fmt/data),
 *   - EVERY value is big-endian (WAV is little-endian),               */
/*   - the sample rate is an 80-bit IEEE extended float,               */
/*   - the samples are big-endian too.                                 */
/*                                                                     */
/* CoreAudio expects little-endian PCM, so we byte-swap the samples at  */
/* load time. That is LOSSLESS and happens once (never on the realtime  */
/* thread) — the bit-perfect principle is intact: the same sample, only */
/* with its bytes in the other order.                                   */
/* ------------------------------------------------------------------ */

/* big-endian read helpers */
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* 80-bit IEEE-754 extended (long double) -> uint32 sample rate.
 * AIFF stores the rate in this form. In practice the rate is an integer, so
 * we recover it by shifting the mantissa according to the exponent. */
static uint32_t aiff_extended_to_rate(const uint8_t *p)
{
    int exponent = ((p[0] & 0x7F) << 8) | p[1];   /* 15-bit exponent (unsigned) */
    uint64_t mantissa = ((uint64_t)be32(p + 2) << 32) | be32(p + 6);
    if (exponent == 0 && mantissa == 0) return 0;
    exponent -= 16383;                            /* remove the bias */
    /* the mantissa is 64-bit; its MSB is the explicit integer unit bit */
    int shift = exponent - 63;
    if (shift > 63 || shift < -63) return 0;      /* implausible — reject */
    double val;
    if (shift >= 0) val = (double)mantissa * (double)(1ULL << shift);
    else            val = (double)mantissa / (double)(1ULL << (-shift));
    if (val < 0.0 || val > 4294967295.0) return 0;
    return (uint32_t)(val + 0.5);
}

static int parse_aiff(const char *path, AudioSource *w)
{
    memset(w, 0, sizeof(*w));

    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    if (fseek(f, 0, SEEK_END) != 0) { fprintf(stderr, "AIFF: seek error.\n"); fclose(f); return -1; }
    long fsizeL = ftell(f);
    if (fsizeL < 12) { fprintf(stderr, "AIFF too short.\n"); fclose(f); return -1; }
    rewind(f);
    const uint64_t fsize = (uint64_t)fsizeL;

    uint8_t *buf = (uint8_t *)malloc((size_t)fsize);
    if (!buf) { fprintf(stderr, "Out of memory (AIFF file buffer).\n"); fclose(f); return -1; }
    if (fread(buf, 1, (size_t)fsize, f) != (size_t)fsize) {
        fprintf(stderr, "AIFF read error.\n"); free(buf); fclose(f); return -1;
    }
    fclose(f);

    /* FORM ... AIFF (plain) — AIFF-C (AIFC) may be compressed, which we do
     * not take on, because it is no longer necessarily uncompressed PCM */
    if (memcmp(buf, "FORM", 4) != 0) {
        fprintf(stderr, "Not an AIFF file (missing 'FORM').\n"); free(buf); return -1;
    }
    int isAifc = (memcmp(buf + 8, "AIFC", 4) == 0);
    if (memcmp(buf + 8, "AIFF", 4) != 0 && !isAifc) {
        fprintf(stderr, "Not an AIFF/AIFC file.\n"); free(buf); return -1;
    }

    int haveComm = 0;
    int alreadyLittleEndian = 0;    /* set for the 'sowt' AIFC variant */
    uint64_t pos = 12;
    while (pos + 8 <= fsize) {
        char id[4];
        memcpy(id, buf + pos, 4);
        uint32_t csz = be32(buf + pos + 4);          /* big-endian chunk size */
        uint64_t body = pos + 8;
        if (body + csz > fsize) break;

        if (memcmp(id, "COMM", 4) == 0 && csz >= 18) {
            w->channels   = be16(buf + body);
            /* body+2: numSampleFrames (4 bytes) — not needed separately */
            w->bits       = be16(buf + body + 6);
            w->sampleRate = aiff_extended_to_rate(buf + body + 8);  /* 80-bit */
            w->isFloat    = 0;     /* AIFF PCM is always integer (float is a rare AIFC case) */
            /* For AIFC the compression type sits at body+18; anything other
             * than 'NONE'/'sowt' is compressed and we do not take it on */
            if (isAifc && csz >= 22) {
                if (memcmp(buf + body + 18, "NONE", 4) != 0 &&
                    memcmp(buf + body + 18, "sowt", 4) != 0) {
                    fprintf(stderr, "AIFC: only uncompressed (NONE/sowt) is supported.\n");
                    free(buf); return -1;
                }
                /* 'sowt' = little-endian samples (skip the big->little swap) */
                if (memcmp(buf + body + 18, "sowt", 4) == 0)
                    alreadyLittleEndian = 1;
            }
            haveComm = 1;
        } else if (memcmp(id, "SSND", 4) == 0) {
            /* SSND: offset (4) + blockSize (4) + the samples */
            if (csz < 8) { fprintf(stderr, "AIFF: bad SSND chunk.\n"); free(buf); return -1; }
            uint32_t offset = be32(buf + body);
            /* offset comes from the file: guard the subtraction below against
             * an unsigned wrap-around and the pointer against running past EOF */
            if ((uint64_t)offset + 8 > csz) {
                fprintf(stderr, "AIFF: bad SSND offset.\n"); free(buf); return -1;
            }
            uint64_t sndStart = body + 8 + offset;
            uint64_t sndBytes = (uint64_t)csz - 8 - offset;
            if (sndStart > fsize) { fprintf(stderr, "AIFF: SSND past end of file.\n"); free(buf); return -1; }
            if (sndStart + sndBytes > fsize) sndBytes = fsize - sndStart;
            if (w->data) { free(w->data); w->data = NULL; w->dataBytes = 0; }
            w->data = (uint8_t *)malloc((size_t)(sndBytes ? sndBytes : 1));
            if (!w->data) { fprintf(stderr, "Out of memory (AIFF).\n"); free(buf); return -1; }
            memcpy(w->data, buf + sndStart, (size_t)sndBytes);
            w->dataBytes = sndBytes;
        }
        pos = body + csz + (csz & 1);                /* padded to even byte counts */
    }
    free(buf);

    if (!haveComm || !w->data) {
        fprintf(stderr, "AIFF: missing COMM or SSND chunk.\n");
        free(w->data); w->data = NULL; return -1;
    }
    if (w->bits != 16 && w->bits != 24 && w->bits != 32) {
        fprintf(stderr, "AIFF: unsupported bit depth: %u.\n", w->bits);
        free(w->data); w->data = NULL; return -1;
    }
    if (w->channels == 0 || w->channels > 64 || w->sampleRate == 0) {
        fprintf(stderr, "AIFF: implausible header (%u ch, %u Hz).\n",
                w->channels, w->sampleRate);
        free(w->data); w->data = NULL; return -1;
    }
    trim_to_whole_frames(w);
    if (!audio_source_valid(w)) {
        fprintf(stderr, "AIFF: no usable audio data.\n");
        free(w->data); w->data = NULL; return -1;
    }

    /* The samples are big-endian (except for the 'sowt' AIFC variant, which is
     * already little-endian). CoreAudio wants little-endian, so we swap per
     * sample. Lossless, one-off, off the realtime thread. */
    if (!alreadyLittleEndian) {
        int bytesPerSample = w->bits / 8;
        uint64_t nSamples = w->dataBytes / bytesPerSample;
        uint8_t *d = w->data;
        for (uint64_t i = 0; i < nSamples; i++) {
            uint8_t *s = d + i * bytesPerSample;
            for (int a = 0, b = bytesPerSample - 1; a < b; a++, b--) {
                uint8_t t = s[a]; s[a] = s[b]; s[b] = t;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* CoreAudio helpers                                                   */
/* ------------------------------------------------------------------ */

static OSStatus get_prop(AudioObjectID obj, AudioObjectPropertySelector sel,
                         AudioObjectPropertyScope scope, void *out, UInt32 *ioSize)
{
    AudioObjectPropertyAddress a = { sel, scope, kAudioObjectPropertyElementMain };
    return AudioObjectGetPropertyData(obj, &a, 0, NULL, ioSize, out);
}

static OSStatus set_prop(AudioObjectID obj, AudioObjectPropertySelector sel,
                         AudioObjectPropertyScope scope, const void *in, UInt32 size)
{
    AudioObjectPropertyAddress a = { sel, scope, kAudioObjectPropertyElementMain };
    return AudioObjectSetPropertyData(obj, &a, 0, NULL, size, in);
}

static void print_device_name(AudioObjectID dev)
{
    CFStringRef name = NULL;
    UInt32 sz = sizeof(name);
    if (get_prop(dev, kAudioObjectPropertyName,
                 kAudioObjectPropertyScopeGlobal, &name, &sz) == noErr && name) {
        char cname[256] = {0};
        CFStringGetCString(name, cname, sizeof(cname), kCFStringEncodingUTF8);
        printf("%s", cname);
        CFRelease(name);
    } else {
        printf("%s", L(MSG_UNKNOWN_DEVICE));
    }
}

static int list_devices(void)
{
    AudioObjectPropertyAddress a = { kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, NULL, &sz) != noErr || sz == 0)
        return -1;
    UInt32 n = sz / sizeof(AudioObjectID);
    AudioObjectID *devs = (AudioObjectID *)malloc(sz);
    if (!devs) { fprintf(stderr, "Out of memory (device list).\n"); return -1; }
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, devs) != noErr) {
        free(devs); return -1;
    }

    printf("%s", L(MSG_DEVICES_HEADER));
    for (UInt32 i = 0; i < n; i++) {
        /* only show devices that actually have an output stream */
        AudioObjectPropertyAddress sa = { kAudioDevicePropertyStreams,
            kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain };
        UInt32 ssz = 0;
        if (AudioObjectGetPropertyDataSize(devs[i], &sa, 0, NULL, &ssz) != noErr || ssz == 0)
            continue;
        printf("  [%u]  ", i);
        print_device_name(devs[i]);
        printf("\n");
    }
    free(devs);
    return 0;
}

static AudioObjectID device_by_index(UInt32 want)
{
    AudioObjectPropertyAddress a = { kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, NULL, &sz) != noErr || sz == 0)
        return kAudioObjectUnknown;
    UInt32 n = sz / sizeof(AudioObjectID);
    AudioObjectID *devs = (AudioObjectID *)malloc(sz);
    if (!devs) return kAudioObjectUnknown;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &sz, devs) != noErr) {
        free(devs); return kAudioObjectUnknown;
    }
    AudioObjectID dev = (want < n) ? devs[want] : kAudioObjectUnknown;
    free(devs);
    return dev;
}

static AudioObjectID default_output_device(void)
{
    AudioObjectID dev = kAudioObjectUnknown;
    UInt32 sz = sizeof(dev);
    get_prop(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice,
             kAudioObjectPropertyScopeGlobal, &dev, &sz);
    return dev;
}

/* ------------------------------------------------------------------ */
/* Player state + the realtime IOProc                                  */
/* ------------------------------------------------------------------ */

/* One preloaded track, already in the output (virtual) format */
typedef struct {
    uint8_t  *buf;        /* samples in the output (virtual) format */
    uint64_t  bytes;      /* buffer length in bytes                 */
} Track;

#define MAX_TRACKS       256
#define RAMP_FRAMES      1024   /* ~23 ms @ 44.1 kHz, ~1.5 ms @ 705.6 kHz */
#define MAX_FRAME_BYTES  64     /* 8 channels x 4 bytes is the practical ceiling */

/*
 * Playback state machine. Everything except PS_PLAYING exists so that the
 * output never steps discontinuously to or from zero — a voltage step is
 * exactly what a speaker reproduces as a click.
 *
 * The fades never touch a music sample: they repeat the LAST REAL FRAME that
 * was written and scale that copy. So the audible content stays bit-perfect,
 * and the only thing being scaled is material we appended ourselves.
 */
typedef enum {
    PS_PLAYING = 0,   /* normal playback (must be 0 — Player is memset) */
    PS_FADE_OUT,      /* fading down into a pause                       */
    PS_PAUSED,        /* paused: writing silence, position frozen       */
    PS_FADE_IN,       /* fading back up out of a pause                  */
    PS_FADE_END,      /* fading down at the end of the queue            */
    PS_ENDED          /* queue finished, silence from here              */
} PlayState;

typedef struct {
    Track             tracks[MAX_TRACKS];
    int               nTracks;
    uint32_t          frameBytes;    /* one frame (channels x bytes per sample) */
    /* format info for the fades — filled in by play_one_segment from the
     * negotiated virtual format */
    int               sampleIsFloat; /* 1 = float samples, 0 = signed integer   */
    int               sampleBytes;   /* one sample in bytes (2/3/4)             */
    int               channels;      /* channel count                           */
    int               isDop;         /* 1 = DoP/DSD stream — see below          */

    /* IOProc-private state: only the realtime thread reads or writes these,
     * so they need no atomics. */
    uint8_t           lastFrame[MAX_FRAME_BYTES];
    int               haveLastFrame;
    int               fadeIdx;
    int               state;         /* PlayState */

    _Atomic int       cur;        /* current track index                  */
    _Atomic uint64_t  pos;        /* position within the current track    */
    _Atomic int       done;       /* the whole queue has been played      */
    _Atomic int       silence;    /* 1 = write silence only (clean stop)  */
    _Atomic int       paused;     /* 1 = pause requested                  */
    _Atomic int       seekReq;    /* track jump: new index + 1, 0 = none  */
} Player;

/* ------------------------------------------------------------------ */
/* Fade helper (realtime-safe: arithmetic only)                        */
/*                                                                     */
/* Scales one frame's samples by a linear factor (0.0–1.0). It is only  */
/* ever applied to a COPY of the last real frame, appended beyond the   */
/* musical content — never to the content itself.                       */
/* ------------------------------------------------------------------ */
static inline void scale_frame(uint8_t *dst, int sampleIsFloat,
                               int sampleBytes, int channels, double gain)
{
    for (int c = 0; c < channels; c++) {
        uint8_t *s = dst + c * sampleBytes;
        if (sampleIsFloat) {
            float f;
            memcpy(&f, s, 4);
            f = (float)(f * gain);
            memcpy(s, &f, 4);
        } else if (sampleBytes == 2) {
            int16_t v;
            memcpy(&v, s, 2);
            v = (int16_t)((double)v * gain);
            memcpy(s, &v, 2);
        } else if (sampleBytes == 3) {
            /* 24-bit little-endian signed */
            int32_t v = (int32_t)((uint32_t)s[0] << 8 |
                                  (uint32_t)s[1] << 16 |
                                  (uint32_t)s[2] << 24);
            v >>= 8;                       /* back to 24 bits, sign-extended */
            v = (int32_t)((double)v * gain);
            s[0] = (uint8_t)(v & 0xFF);
            s[1] = (uint8_t)((v >> 8) & 0xFF);
            s[2] = (uint8_t)((v >> 16) & 0xFF);
        } else { /* 4 bytes, signed integer */
            int32_t v;
            memcpy(&v, s, 4);
            v = (int32_t)((double)v * gain);
            memcpy(s, &v, 4);
        }
    }
}

/*
 * Writes whole frames of the held last frame into dst, scaled by a linear
 * ramp, advancing p->fadeIdx as it goes. dir > 0 fades up, dir < 0 fades down.
 * Returns the number of bytes written (always a whole number of frames), which
 * may be less than 'want' if the ramp finished or the space ran out.
 *
 * Because fadeIdx lives in the Player, a ramp that does not fit into a single
 * CoreAudio block simply continues in the next callback — the ramp is never
 * cut short.
 *
 * REALTIME-SAFE: memcpy and arithmetic only.
 */
static uint32_t ramp_hold(Player *p, uint8_t *dst, uint32_t want, int dir)
{
    const uint32_t fb = p->frameBytes;
    uint32_t off = 0;
    if (fb == 0) return 0;
    while (off + fb <= want && p->fadeIdx < RAMP_FRAMES) {
        double t = (double)p->fadeIdx / (double)RAMP_FRAMES;
        double gain = (dir > 0) ? t : (1.0 - t);
        memcpy(dst + off, p->lastFrame, fb);
        if (gain > 0.0)
            scale_frame(dst + off, p->sampleIsFloat, p->sampleBytes, p->channels, gain);
        else
            memset(dst + off, 0, fb);
        off += fb;
        p->fadeIdx++;
    }
    return off;
}

/*
 * REALTIME RULES: no allocation, no locks, no I/O, no system calls. Only
 * memcpy, memset and arithmetic.
 *
 * GAPLESS: if the current track runs out mid-block, we step to the beginning
 * of the next track WITHIN THE SAME CALLBACK and fill the remainder from
 * there. The track boundary therefore never coincides with a callback
 * boundary — not a single empty frame is produced. (Precondition: the tracks
 * in the queue share one output format; the loader guarantees that.)
 *
 * DoP/DSD: a DoP frame carries the 0x05/0xFA marker in its top byte, and
 * scaling that word would destroy both the marker and the DSD bits, so the
 * DAC would drop out of DSD mode and emit noise. For DoP streams the fades
 * are therefore disabled deliberately, and stopping is an immediate mute —
 * which is exactly the behaviour that has been hardware-validated.
 */
static OSStatus io_proc(AudioObjectID dev,
                        const AudioTimeStamp *now,
                        const AudioBufferList *inData,
                        const AudioTimeStamp *inTime,
                        AudioBufferList *outData,
                        const AudioTimeStamp *outTime,
                        void *ctx)
{
    (void)dev; (void)now; (void)inData; (void)inTime; (void)outTime;
    Player *p = (Player *)ctx;

    /* Hard stop phase: write silence only, so the DAC output settles at zero
     * before the stream actually stops. */
    if (atomic_load_explicit(&p->silence, memory_order_relaxed)) {
        for (UInt32 i = 0; i < outData->mNumberBuffers; i++)
            memset(outData->mBuffers[i].mData, 0, outData->mBuffers[i].mDataByteSize);
        return noErr;
    }

    const uint32_t fb = p->frameBytes;
    /* Fades are only possible when we can safely scale a held frame: PCM (not
     * DoP), a sane frame size, and a last frame to hold. */
    const int canFade = (!p->isDop && fb > 0 && fb <= MAX_FRAME_BYTES &&
                         p->sampleBytes > 0 && p->channels > 0 && p->haveLastFrame);

    /* Track jump request (n/b): the main thread sets seekReq (new index + 1),
     * we apply it here atomically and rewind to the start of that track. */
    int sr = atomic_load_explicit(&p->seekReq, memory_order_relaxed);
    if (sr != 0) {
        int target = sr - 1;
        if (target < 0) target = 0;
        if (target >= p->nTracks) target = p->nTracks - 1;
        atomic_store_explicit(&p->cur, target, memory_order_relaxed);
        atomic_store_explicit(&p->pos, 0, memory_order_relaxed);
        atomic_store_explicit(&p->seekReq, 0, memory_order_relaxed);
        if (p->state == PS_FADE_END || p->state == PS_ENDED) {
            p->state = PS_PLAYING;      /* a jump revives a finished queue */
            p->fadeIdx = 0;
        }
    }

    /* Pause transitions requested by the main thread. A transition that is
     * already in flight is allowed to finish first — a fade lasts a few
     * milliseconds, so this is not perceptible, and it keeps the state machine
     * free of half-completed ramps. */
    int wantPause = atomic_load_explicit(&p->paused, memory_order_relaxed);
    if (wantPause && p->state == PS_PLAYING) {
        p->state = canFade ? PS_FADE_OUT : PS_PAUSED;
        p->fadeIdx = 0;
    } else if (!wantPause && p->state == PS_PAUSED) {
        p->state = canFade ? PS_FADE_IN : PS_PLAYING;
        p->fadeIdx = 0;
    }

    for (UInt32 i = 0; i < outData->mNumberBuffers; i++) {
        AudioBuffer *b = &outData->mBuffers[i];
        uint8_t *out   = (uint8_t *)b->mData;
        UInt32   want  = b->mDataByteSize;     /* this many bytes must be filled */
        UInt32   filled = 0;

        if (p->state == PS_PAUSED || p->state == PS_ENDED) {
            memset(out, 0, want);
            continue;
        }

        if (p->state == PS_FADE_OUT) {
            filled = ramp_hold(p, out, want, -1);
            if (p->fadeIdx >= RAMP_FRAMES) p->state = PS_PAUSED;
            if (filled < want) memset(out + filled, 0, want - filled);
            continue;
        }

        if (p->state == PS_FADE_IN) {
            filled = ramp_hold(p, out, want, +1);
            if (p->fadeIdx >= RAMP_FRAMES) p->state = PS_PLAYING;
            /* If the ramp completed mid-block we fall through and fill the
             * remainder with real audio — no gap is introduced. */
        }

        if (p->state == PS_PLAYING) {
            int      cur = atomic_load_explicit(&p->cur, memory_order_relaxed);
            uint64_t pos = atomic_load_explicit(&p->pos, memory_order_relaxed);
            const UInt32 before = filled;

            /* fill the output block, crossing track boundaries if needed */
            while (filled < want && cur < p->nTracks) {
                const Track *t = &p->tracks[cur];
                uint64_t remain = (pos < t->bytes) ? (t->bytes - pos) : 0;
                UInt32   need   = want - filled;
                UInt32   n      = (remain < need) ? (UInt32)remain : need;

                if (n) { memcpy(out + filled, t->buf + pos, n); filled += n; pos += n; }

                if (pos >= t->bytes) {   /* end of track — move to the next */
                    cur++;
                    pos = 0;
                }
            }

            /* Remember the last REAL frame written; the fades hold on to it.
             * Only update it if this block actually contained audio. */
            if (filled > before && filled >= fb && fb > 0 && fb <= MAX_FRAME_BYTES) {
                memcpy(p->lastFrame, out + filled - fb, fb);
                p->haveLastFrame = 1;
            }

            atomic_store_explicit(&p->cur, cur, memory_order_relaxed);
            atomic_store_explicit(&p->pos, pos, memory_order_relaxed);

            if (cur >= p->nTracks) {
                /*
                 * The queue is exhausted. Zeroing the remainder here would put
                 * a voltage step into the output, because the last musical
                 * sample is rarely zero — that step is what clicks. Instead we
                 * fade down from the last real frame over RAMP_FRAMES, and only
                 * report 'done' once that fade has finished. Because fadeIdx
                 * lives in the Player, the fade continues across callbacks.
                 */
                const int fadeable = (!p->isDop && fb > 0 && fb <= MAX_FRAME_BYTES &&
                                      p->sampleBytes > 0 && p->channels > 0 &&
                                      p->haveLastFrame);
                p->state   = fadeable ? PS_FADE_END : PS_ENDED;
                p->fadeIdx = 0;
                if (p->state == PS_ENDED)
                    atomic_store_explicit(&p->done, 1, memory_order_relaxed);
            }
        }

        if (p->state == PS_FADE_END && filled < want) {
            filled += ramp_hold(p, out + filled, want - filled, -1);
            if (p->fadeIdx >= RAMP_FRAMES) {
                p->state = PS_ENDED;
                atomic_store_explicit(&p->done, 1, memory_order_relaxed);
            }
        }

        if (filled < want) memset(out + filled, 0, want - filled);
    }
    return noErr;
}

/* ------------------------------------------------------------------ */
/* Resource helpers (for the central cleanup path)                     */
/* ------------------------------------------------------------------ */

static void free_audio_source(AudioSource *s)
{
    if (!s) return;
    if (s->data) { free(s->data); s->data = NULL; }
    s->dataBytes = 0;
}

static void free_track(Track *t)
{
    if (!t || !t->buf) return;
    munlock(t->buf, t->bytes);
    free(t->buf);
    t->buf = NULL;
    t->bytes = 0;
}

static void free_player_tracks(Player *p)
{
    if (!p) return;
    for (int i = 0; i < p->nTracks; i++)
        free_track(&p->tracks[i]);
    p->nTracks = 0;
}

/* Ask the IOProc for a clean stop: write silence, then allow a short run-out
 * so the DAC settles at zero level. */
static void request_silence(Player *p)
{
    if (!p) return;
    atomic_store(&p->silence, 1);
    usleep(120 * 1000);
}

/* ------------------------------------------------------------------ */
/* Format matching: source -> negotiated virtual format                */
/*                                                                     */
/* The source sample is left-aligned to 32 bits (lossless) and then     */
/* packed into the target virtual format. If the target is integer and  */
/* at least as deep as the source, the operation is bit-exact.          */
/* ------------------------------------------------------------------ */

static int32_t read_sample_left32(const uint8_t *src, uint16_t bits)
{
    switch (bits) {
    case 16: {
        int16_t s; memcpy(&s, src, 2);
        return (int32_t)s << 16;
    }
    case 24: {
        int32_t v = (int32_t)((uint32_t)src[0] << 8 |
                              (uint32_t)src[1] << 16 |
                              (uint32_t)src[2] << 24);
        return v;
    }
    case 32: {
        int32_t s; memcpy(&s, src, 4);
        return s;
    }
    }
    return 0;
}

static uint8_t *convert_to_virtual(const AudioSource *w,
                                   const AudioStreamBasicDescription *vf,
                                   uint64_t *outBytes,
                                   int *outBitPerfect)
{
    if (!audio_source_valid(w)) {
        fprintf(stderr, "Invalid audio source.\n");
        return NULL;
    }
    if (vf->mChannelsPerFrame == 0 || vf->mBytesPerFrame == 0) {
        fprintf(stderr, "Invalid virtual format.\n");
        return NULL;
    }

    const uint32_t srcBpS   = w->bits / 8;
    const uint64_t frames   = w->dataBytes / ((uint64_t)srcBpS * w->channels);
    const uint32_t dstBpF   = vf->mBytesPerFrame;
    const uint32_t dstBpS   = dstBpF / vf->mChannelsPerFrame;
    const int      isFloat  = (vf->mFormatFlags & kAudioFormatFlagIsFloat) != 0;
    const int      hiAlign  = (vf->mFormatFlags & kAudioFormatFlagIsAlignedHigh) != 0;
    const uint32_t dstBits  = vf->mBitsPerChannel;

    if (vf->mFormatFlags & kAudioFormatFlagIsBigEndian) {
        fprintf(stderr, "Big-endian virtual format is not supported.\n");
        return NULL;
    }
    if (vf->mChannelsPerFrame != w->channels) {
        fprintf(stderr, "Channel count mismatch (file: %u, device: %u).\n",
                w->channels, (unsigned)vf->mChannelsPerFrame);
        return NULL;
    }
    if (dstBits == 0 || dstBits > 32 || dstBpS == 0 || dstBpS > 4) {
        fprintf(stderr, "Unsupported virtual sample size (%u-bit, %u B).\n",
                (unsigned)dstBits, (unsigned)dstBpS);
        return NULL;
    }
    if (frames == 0) {
        fprintf(stderr, "No complete audio frames in source.\n");
        return NULL;
    }

    uint64_t total = frames * dstBpF;
    uint8_t *out = (uint8_t *)malloc((size_t)total);
    if (!out) { fprintf(stderr, "Out of memory for the decoded buffer.\n"); return NULL; }

    const uint8_t *s = w->data;
    uint8_t *d = out;
    uint64_t nsamp = frames * w->channels;

    /* fast path: the virtual format is byte-identical to the source.
     * NOTE: we copy exactly 'total' bytes, not w->dataBytes. The two are equal
     * for a well-formed file, but a source that ends mid-frame would otherwise
     * overrun this buffer. */
    if (!isFloat && !w->isFloat && dstBits == w->bits && dstBpS == srcBpS && !hiAlign) {
        memcpy(out, w->data, (size_t)total);
        *outBytes = total;
        *outBitPerfect = 1;
        return out;
    }

    /* 32-bit FLOAT source (e.g. a Pyramix/Anubis master). The source already
     * holds IEEE float samples. If the target is float32 as well, this is a
     * DIRECT COPY — zero conversion, the float sample reaches the HAL
     * untouched. If the target is integer, we convert float->int at the output
     * bit depth. */
    if (w->isFloat) {
        if (w->bits != 32) {
            fprintf(stderr, "Only 32-bit float WAV is supported on the float path.\n");
            free(out); return NULL;
        }
        for (uint64_t i = 0; i < nsamp; i++, s += 4, d += dstBpS) {
            float f; memcpy(&f, s, 4);
            if (isFloat) {
                memcpy(d, &f, 4);                 /* float -> float: untouched */
            } else {
                /* float [-1,1) -> signed integer at the target bit depth */
                double scaled = (double)f * (double)(1u << (dstBits - 1));
                long v = (long)scaled;
                long lim = (1L << (dstBits - 1));
                if (v >  lim - 1) v = lim - 1;    /* clip to range */
                if (v < -lim)     v = -lim;
                int32_t o = (int32_t)v;
                if (dstBpS == 2)      { memcpy(d, &o, 2); }
                else if (dstBpS == 3) { d[0]=o&0xFF; d[1]=(o>>8)&0xFF; d[2]=(o>>16)&0xFF; }
                else                  { memcpy(d, &o, 4); }
            }
        }
        *outBytes = total;
        /* float->float: bit-perfect. float->int: the full audible range is
         * preserved (a 24-bit DAC carries the real dynamic range), but strictly
         * speaking the part of the float beyond 24 bits is rounded — so we do
         * not call that bit-perfect, only "full audible range preserved". */
        *outBitPerfect = (isFloat ? 1 : 0);
        return out;
    }

    for (uint64_t i = 0; i < nsamp; i++, s += srcBpS, d += dstBpS) {
        int32_t v = read_sample_left32(s, w->bits);
        if (isFloat) {
            float f = (float)v / 2147483648.0f;
            memcpy(d, &f, 4);
        } else {
            int32_t o;
            if (hiAlign || dstBpS * 8 == dstBits)
                o = v >> (32 - dstBpS * 8);          /* MSB-aligned in the container */
            else
                o = v >> (32 - dstBits);             /* LSB-aligned */
            memcpy(d, &o, dstBpS);                   /* little-endian truncation */
        }
    }

    *outBytes = total;
    /* The chain is bit-perfect (lossless) if:
     *  - the target is integer and at least as deep as the source, OR
     *  - the target is float and float32's 24-bit mantissa can hold the source
     *    (i.e. the source is <= 24-bit). 16/24-bit integers are exactly
     *    representable in float32, so the intermediate step loses nothing. */
    if (!isFloat)
        *outBitPerfect = (dstBits >= w->bits);
    else
        *outBitPerfect = (w->bits <= 24);
    return out;
}

/* ------------------------------------------------------------------ */
/* Device configuration: hog, mixing, sample rate, integer mode        */
/* ------------------------------------------------------------------ */

static AudioObjectID g_dev = kAudioObjectUnknown;
static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig) { (void)sig; g_stop = 1; }

/* ------------------------------------------------------------------ */
/* Raw terminal mode for the transport keys (n/b/space/q)              */
/* ------------------------------------------------------------------ */

static struct termios g_origTermios;
static int g_rawActive = 0;
static int g_rawAtexitRegistered = 0;

static void raw_mode_off(void)
{
    if (g_rawActive) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_origTermios);
        g_rawActive = 0;
    }
}

static void raw_mode_on(void)
{
    if (!isatty(STDIN_FILENO)) return;        /* not a terminal: skip */
    if (tcgetattr(STDIN_FILENO, &g_origTermios) != 0) return;
    struct termios raw = g_origTermios;
    raw.c_lflag &= ~(ICANON | ECHO);          /* no line buffering, no echo */
    raw.c_cc[VMIN]  = 0;                       /* non-blocking read */
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
        g_rawActive = 1;
        /* Register the restore handler once only. raw_mode_on runs per
         * segment, and atexit has a bounded slot count. */
        if (!g_rawAtexitRegistered) {
            atexit(raw_mode_off);
            g_rawAtexitRegistered = 1;
        }
    }
}

/* one key if there is one, otherwise 0 */
static int read_key(void)
{
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    return (n == 1) ? (int)c : 0;
}

static void release_device(void)
{
    if (g_dev == kAudioObjectUnknown) return;
    pid_t off = -1;
    set_prop(g_dev, kAudioDevicePropertyHogMode,
             kAudioObjectPropertyScopeGlobal, &off, sizeof(off));
}

/* ------------------------------------------------------------------ */
/* Sleep prevention while playing (IOPMAssertion)                      */
/*                                                                     */
/* Keeps the Mac from going to sleep during playback and interrupting  */
/* a long album or DSD file. This is a RELIABILITY feature (continuous,
 * uninterrupted playback) — NOT a sound-quality setting. We do not     */
/* interfere with system housekeeping, we only signal: while music is   */
/* playing, do not put the machine to sleep.                            */
/* ------------------------------------------------------------------ */
static IOPMAssertionID g_pmAssertion = 0;

static void prevent_sleep(void)
{
    if (g_pmAssertion != 0) return;     /* already active */
    /* kIOPMAssertionTypeNoIdleSleep: keep the system out of idle sleep. The
     * display may still sleep — we do not block that, only system sleep. */
    CFStringRef reason = CFSTR("bpplay audio playback");
    IOPMAssertionCreateWithName(kIOPMAssertionTypeNoIdleSleep,
                                kIOPMAssertionLevelOn, reason, &g_pmAssertion);
}

static void allow_sleep(void)
{
    if (g_pmAssertion == 0) return;
    IOPMAssertionRelease(g_pmAssertion);
    g_pmAssertion = 0;
}

static int acquire_hog(AudioObjectID dev)
{
    pid_t hog = -1;
    UInt32 sz = sizeof(hog);
    get_prop(dev, kAudioDevicePropertyHogMode, kAudioObjectPropertyScopeGlobal, &hog, &sz);
    if (hog != -1 && hog != getpid()) {
        fprintf(stderr, L(MSG_HOG_TAKEN), (int)hog);
        return -1;
    }
    pid_t me = getpid();
    OSStatus st = set_prop(dev, kAudioDevicePropertyHogMode,
                           kAudioObjectPropertyScopeGlobal, &me, sizeof(me));
    if (st != noErr) {
        fprintf(stderr, L(MSG_HOG_UNAVAIL), (int)st);
        return 0;   /* not fatal — e.g. built-in speakers or aggregate devices */
    }
    printf("%s", L(MSG_HOG_OK));
    return 0;
}

static void try_disable_mixing(AudioObjectID dev)
{
    AudioObjectPropertyAddress a = { kAudioDevicePropertySupportsMixing,
        kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain };
    Boolean settable = false;
    if (AudioObjectIsPropertySettable(dev, &a, &settable) == noErr && settable) {
        UInt32 mix = 0;
        if (set_prop(dev, kAudioDevicePropertySupportsMixing,
                     kAudioObjectPropertyScopeOutput, &mix, sizeof(mix)) == noErr)
            printf("%s", L(MSG_MIXING_OFF));
    }
}

static int set_sample_rate(AudioObjectID dev, double rate)
{
    Float64 cur = 0;
    UInt32 sz = sizeof(cur);
    get_prop(dev, kAudioDevicePropertyNominalSampleRate,
             kAudioObjectPropertyScopeGlobal, &cur, &sz);
    if (cur == rate) return 0;

    Float64 r = rate;
    OSStatus st = set_prop(dev, kAudioDevicePropertyNominalSampleRate,
                           kAudioObjectPropertyScopeGlobal, &r, sizeof(r));
    if (st != noErr) {
        fprintf(stderr, L(MSG_SRATE_REJECT), rate, (int)st);
        return -1;
    }
    /* the switch is asynchronous — wait until it has really taken effect */
    for (int i = 0; i < 300; i++) {
        sz = sizeof(cur);
        get_prop(dev, kAudioDevicePropertyNominalSampleRate,
                 kAudioObjectPropertyScopeGlobal, &cur, &sz);
        if (cur == rate) {
            usleep(150 * 1000);          /* let the clock settle */
            printf(L(MSG_SRATE_SET), rate);
            return 0;
        }
        usleep(10 * 1000);
    }
    fprintf(stderr, "%s", L(MSG_SRATE_TIMEOUT));
    return -1;
}

static AudioStreamID first_output_stream(AudioObjectID dev)
{
    AudioObjectPropertyAddress a = { kAudioDevicePropertyStreams,
        kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain };
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(dev, &a, 0, NULL, &sz) != noErr || sz == 0)
        return kAudioObjectUnknown;
    AudioStreamID *st = (AudioStreamID *)malloc(sz);
    if (!st) return kAudioObjectUnknown;
    if (AudioObjectGetPropertyData(dev, &a, 0, NULL, &sz, st) != noErr) {
        free(st); return kAudioObjectUnknown;
    }
    AudioStreamID s = st[0];
    free(st);
    return s;
}

/* pick the smallest integer physical format that is still sufficient */
static int pick_integer_format(AudioStreamID stream, double rate, uint16_t bits,
                               uint16_t channels, AudioStreamBasicDescription *out)
{
    AudioObjectPropertyAddress a = { kAudioStreamPropertyAvailablePhysicalFormats,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(stream, &a, 0, NULL, &sz) != noErr || sz == 0)
        return -1;
    UInt32 n = sz / sizeof(AudioStreamRangedDescription);
    AudioStreamRangedDescription *fmts = (AudioStreamRangedDescription *)malloc(sz);
    if (!fmts) return -1;
    if (AudioObjectGetPropertyData(stream, &a, 0, NULL, &sz, fmts) != noErr) {
        free(fmts); return -1;
    }

    int best = -1;
    for (UInt32 i = 0; i < n; i++) {
        const AudioStreamBasicDescription *f = &fmts[i].mFormat;
        if (f->mFormatID != kAudioFormatLinearPCM) continue;
        if (!(f->mFormatFlags & kAudioFormatFlagIsSignedInteger)) continue;
        if (f->mFormatFlags & kAudioFormatFlagIsBigEndian) continue;
        if (f->mChannelsPerFrame != channels) continue;

        double lo = fmts[i].mSampleRateRange.mMinimum;
        double hi = fmts[i].mSampleRateRange.mMaximum;
        double fr = f->mSampleRate;
        int rateOK = (fr == rate) || (fr == kAudioStreamAnyRate && rate >= lo && rate <= hi);
        if (!rateOK) continue;
        if (f->mBitsPerChannel < bits) continue;

        if (best < 0 ||
            f->mBitsPerChannel < fmts[best].mFormat.mBitsPerChannel)
            best = (int)i;
    }

    int ok = -1;
    if (best >= 0) {
        *out = fmts[best].mFormat;
        out->mSampleRate = rate;
        ok = 0;
    }
    free(fmts);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Lightweight format probe for playlist segmentation                  */
/* Reads the header only (sample rate, bits, channels) — no decoding.  */
/* ------------------------------------------------------------------ */

typedef struct { uint32_t rate; uint16_t bits; uint16_t ch; int dsd; int isFloat; } AudioFmt;

static int fmt_equal(const AudioFmt *a, const AudioFmt *b)
{
    return a->rate == b->rate && a->bits == b->bits && a->ch == b->ch &&
           a->dsd == b->dsd && a->isFloat == b->isFloat;
}

static int probe_format(const char *path, AudioFmt *out)
{
    memset(out, 0, sizeof(*out));
    const char *ext = strrchr(path, '.');
    int isFlac = (ext && strcasecmp(ext, ".flac") == 0);
    int isDsf  = (ext && strcasecmp(ext, ".dsf")  == 0);
    int isAiff = (ext && (strcasecmp(ext, ".aif") == 0 || strcasecmp(ext, ".aiff") == 0));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    if (isDsf) {
        /* DSF: the fmt chunk is at byte 28, sample rate at +28, channels at +24 */
        uint8_t h[92];
        if (fread(h, 1, 92, f) != 92) { fclose(f); return -1; }
        fclose(f);
        if (memcmp(h, "DSD ", 4) != 0 || memcmp(h + 28, "fmt ", 4) != 0) return -1;
        uint32_t chNum, dsdRate;
        memcpy(&chNum,   h + 28 + 24, 4);
        memcpy(&dsdRate, h + 28 + 28, 4);
        if (dsdRate == 0) return -1;
        out->rate = dsdRate / 16;     /* DoP PCM carrier rate */
        out->bits = 24; out->ch = (uint16_t)chNum; out->dsd = 1;
        return 0;
    }

    if (isFlac) {
        /* FLAC: "fLaC" + STREAMINFO metadata block (rate/channels/bits live in
         * bit fields inside STREAMINFO's 34 bytes) */
        uint8_t h[8];
        if (fread(h, 1, 4, f) != 4 || memcmp(h, "fLaC", 4) != 0) { fclose(f); return -1; }
        /* metadata block header: 1 byte (type+last), 3 bytes length */
        uint8_t bh[4];
        if (fread(bh, 1, 4, f) != 4) { fclose(f); return -1; }
        if ((bh[0] & 0x7F) != 0) { fclose(f); return -1; }   /* 0 = STREAMINFO */
        uint8_t si[34];
        if (fread(si, 1, 34, f) != 34) { fclose(f); return -1; }
        fclose(f);
        /* sample rate: 20 bits in si[10..12]; channels: 3 bits; bits: 5 bits */
        uint32_t sr = ((uint32_t)si[10] << 12) | ((uint32_t)si[11] << 4) | (si[12] >> 4);
        uint16_t ch = (uint16_t)(((si[12] >> 1) & 0x07) + 1);
        uint16_t bits = (uint16_t)((((si[12] & 0x01) << 4) | (si[13] >> 4)) + 1);
        out->rate = sr; out->bits = bits; out->ch = ch; out->dsd = 0;
        return 0;
    }

    if (isAiff) {
        /* AIFF: FORM ... find the COMM chunk (big-endian values) */
        uint8_t hdr[12];
        if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "FORM", 4) != 0) { fclose(f); return -1; }
        for (;;) {
            uint8_t ch[8];
            if (fread(ch, 1, 8, f) != 8) { fclose(f); return -1; }
            uint32_t csz = ((uint32_t)ch[4] << 24) | ((uint32_t)ch[5] << 16) |
                           ((uint32_t)ch[6] << 8) | (uint32_t)ch[7];
            if (memcmp(ch, "COMM", 4) == 0) {
                uint8_t c[18];
                if (fread(c, 1, 18, f) != 18) { fclose(f); return -1; }
                fclose(f);
                out->ch   = (uint16_t)((c[0] << 8) | c[1]);
                out->bits = (uint16_t)((c[6] << 8) | c[7]);
                out->rate = aiff_extended_to_rate(c + 8);
                out->dsd  = 0;
                return 0;
            }
            if (fseek(f, (long)(csz + (csz & 1)), SEEK_CUR) != 0) { fclose(f); return -1; }
        }
    }

    /* WAV: find the fmt chunk */
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) != 0 ||
        memcmp(hdr + 8, "WAVE", 4) != 0) { fclose(f); return -1; }
    for (;;) {
        uint8_t ch[8];
        if (fread(ch, 1, 8, f) != 8) { fclose(f); return -1; }
        uint32_t csz; memcpy(&csz, ch + 4, 4);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (fread(fmt, 1, 16, f) != 16) { fclose(f); return -1; }
            uint16_t channels, bits, tag; uint32_t rate;
            memcpy(&tag,      fmt,      2);
            memcpy(&channels, fmt + 2,  2);
            memcpy(&rate,     fmt + 4,  4);
            memcpy(&bits,     fmt + 14, 2);
            /* WAVE_FORMAT_EXTENSIBLE keeps the real format tag in the
             * subformat GUID, 24 bytes into the chunk body. */
            if (tag == 0xFFFE && csz >= 40) {
                uint8_t ext[24];
                if (fread(ext, 1, 24, f) == 24) memcpy(&tag, ext + 8, 2);
            }
            fclose(f);
            out->rate = rate; out->bits = bits; out->ch = channels; out->dsd = 0;
            /* Integer and float sources of the same width need different
             * conversion paths, so they must not share a segment. */
            out->isFloat = (tag == 3) ? 1 : 0;
            return 0;
        }
        if (fseek(f, (long)(csz + (csz & 1)), SEEK_CUR) != 0) { fclose(f); return -1; }
    }
}

/* ------------------------------------------------------------------ */
/* Playing one format segment                                          */
/*                                                                     */
/* The 'seg' array holds the paths of segCount files that share one    */
/* format. The function configures the DAC for that format, loads the  */
/* segment, plays it gapless and then stops cleanly. Key handling and  */
/* g_stop carry across segments.                                       */
/*                                                                     */
/* Returns:  0 = normal end,  1 = the user quit (q / Ctrl+C),          */
/*          -1 = error.                                                */
/* *prevRate is the previous segment's sample rate (so we can skip a   */
/* redundant reconfiguration); the function updates it.                */
/* ------------------------------------------------------------------ */
static int play_one_segment(AudioObjectID dev, const char **seg, int segCount,
                            double *prevRate, int segIndex, int segTotal)
{
    AudioSource w; memset(&w, 0, sizeof(w));
    AudioMeta   meta; memset(&meta, 0, sizeof(meta));
    Player p; memset(&p, 0, sizeof(p));
    AudioDeviceIOProcID procID = NULL;
    int rc = -1;                 /* default: error */
    int w_loaded = 0;
    uint64_t totalBytes = 0;
    double totalSec = 0;
    AudioStreamBasicDescription vf; memset(&vf, 0, sizeof(vf));

    const char *path = seg[0];
    const char *ext = strrchr(path, '.');
    int isFlac = (ext && strcasecmp(ext, ".flac") == 0);
    int isDsf  = (ext && strcasecmp(ext, ".dsf")  == 0);
    int isAiff = (ext && (strcasecmp(ext, ".aif") == 0 || strcasecmp(ext, ".aiff") == 0));

    uint32_t dsdRate = 0;
    if (isDsf) {
        if (load_dsf_as_dop(path, &w, &dsdRate) != 0) goto cleanup;
    } else if (isFlac) {
        if (load_flac(path, &w, &meta) != 0) goto cleanup;
        trim_to_whole_frames(&w);
    } else if (isAiff) {
        if (parse_aiff(path, &w) != 0) goto cleanup;
    } else {
        if (parse_wav(path, &w) != 0) goto cleanup;
    }
    if (!audio_source_valid(&w)) {
        fprintf(stderr, "Unusable audio data: %s\n", path);
        free_audio_source(&w);
        goto cleanup;
    }
    w_loaded = 1;

    /* audio_source_valid guarantees non-zero bits, channels and sample rate,
     * so this division is safe. */
    double durSec = (double)w.dataBytes / (w.bits / 8) / w.channels / w.sampleRate;
    if (segTotal > 1)
        printf("\n[%d/%d] ", segIndex + 1, segTotal);
    printf(L(MSG_FILE_INFO), path, w.sampleRate, w.bits, w.channels, durSec);
    if (isDsf)
        printf(L(MSG_DSD_INFO), dsdRate / 1000000.0,
               dsd_multiple(dsdRate), w.sampleRate);
    if (meta.title[0] || meta.artist[0] || meta.album[0]) {
        printf("%s%s%s%s%s%s\n", L(MSG_TITLE),
               meta.artist[0] ? meta.artist : "",
               (meta.artist[0] && meta.title[0]) ? " — " : "",
               meta.title[0] ? meta.title : "",
               meta.album[0] ? "  ·  " : "", meta.album[0] ? meta.album : "");
    }
    if (meta.mqaDetected) {
        printf("%s", L(MSG_MQA_DETECTED));
        if (meta.mqaOriginalRate) printf(L(MSG_MQA_ORIGINAL), meta.mqaOriginalRate / 1000.0);
        printf("%s", L(MSG_MQA_DAC));
        if (w.bits < 24) printf("%s", L(MSG_MQA_WARN_BITS));
    }

    /* set the sample rate (only if it changed since the previous segment) */
    if (*prevRate != (double)w.sampleRate) {
        if (set_sample_rate(dev, (double)w.sampleRate) != 0) {
            /* For DSD the high DoP rate can be the cause (DSD128 -> 352.8 kHz,
             * DSD256 -> 705.6 kHz): not every DAC/USB chain carries it. Say so
             * in plain words. */
            if (isDsf)
                fprintf(stderr, L(MSG_DOP_RATE_HIGH),
                        dsd_multiple(dsdRate), w.sampleRate / 1000.0);
            goto cleanup;
        }
        *prevRate = (double)w.sampleRate;
    }

    /* negotiate the integer / virtual format */
    {
        AudioStreamID stream = first_output_stream(dev);
        if (stream != kAudioObjectUnknown) {
            AudioStreamBasicDescription phys;
            if (pick_integer_format(stream, w.sampleRate, w.bits, w.channels, &phys) == 0) {
                if (set_prop(stream, kAudioStreamPropertyPhysicalFormat,
                             kAudioObjectPropertyScopeGlobal, &phys, sizeof(phys)) == noErr) {
                    printf(L(MSG_PHYS_FMT), (unsigned)phys.mBitsPerChannel);
                    AudioStreamBasicDescription want = phys;
                    want.mFormatID         = kAudioFormatLinearPCM;
                    want.mFormatFlags      = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
                    want.mBitsPerChannel   = phys.mBitsPerChannel;
                    want.mChannelsPerFrame = w.channels;
                    want.mBytesPerFrame    = (phys.mBitsPerChannel / 8) * w.channels;
                    want.mFramesPerPacket  = 1;
                    want.mBytesPerPacket   = want.mBytesPerFrame;
                    want.mSampleRate       = w.sampleRate;
                    const AudioStreamBasicDescription cand[2] = { want, phys };
                    for (int k = 0; k < 2; k++) {
                        set_prop(stream, kAudioStreamPropertyVirtualFormat,
                                 kAudioObjectPropertyScopeGlobal, &cand[k], sizeof(cand[k]));
                        usleep(60 * 1000);
                        UInt32 sz2 = sizeof(vf);
                        if (get_prop(stream, kAudioStreamPropertyVirtualFormat,
                                     kAudioObjectPropertyScopeGlobal, &vf, &sz2) == noErr
                            && !(vf.mFormatFlags & kAudioFormatFlagIsFloat)) break;
                    }
                }
            }
            UInt32 sz = sizeof(vf);
            get_prop(stream, kAudioStreamPropertyVirtualFormat,
                     kAudioObjectPropertyScopeGlobal, &vf, &sz);
        }
    }
    if (vf.mBytesPerFrame == 0 || vf.mChannelsPerFrame == 0) {
        fprintf(stderr, "%s", L(MSG_NOFMT)); goto cleanup;
    }
    int integerMode = !(vf.mFormatFlags & kAudioFormatFlagIsFloat);
    printf(L(MSG_VFMT),
           (vf.mFormatFlags & kAudioFormatFlagIsFloat) ? "float" : "integer",
           (unsigned)vf.mBitsPerChannel, (unsigned)vf.mBytesPerFrame,
           integerMode ? L(MSG_VFMT_INT_ON) : L(MSG_VFMT_FLOAT));

    /* build the track list for this segment */
    p.frameBytes = vf.mBytesPerFrame;
    /* format info for the fades */
    p.sampleIsFloat = (vf.mFormatFlags & kAudioFormatFlagIsFloat) ? 1 : 0;
    p.channels      = (int)vf.mChannelsPerFrame;
    p.sampleBytes   = (int)(vf.mBytesPerFrame / vf.mChannelsPerFrame);
    /* A DoP stream must never be scaled — see the note above io_proc. */
    p.isDop         = isDsf ? 1 : 0;
    int bitPerfect = 0; uint64_t t0bytes = 0;
    uint8_t *t0buf = convert_to_virtual(&w, &vf, &t0bytes, &bitPerfect);
    int wIsFloat = w.isFloat;
    free_audio_source(&w); w_loaded = 0;
    if (!t0buf) goto cleanup;
    p.tracks[0].buf = t0buf; p.tracks[0].bytes = t0bytes; p.nTracks = 1;
    totalBytes = t0bytes;

    for (int ti = 1; ti < segCount; ti++) {
        AudioSource s2; memset(&s2, 0, sizeof(s2));
        AudioMeta m2; memset(&m2, 0, sizeof(m2));
        const char *e2 = strrchr(seg[ti], '.');
        int f2 = (e2 && strcasecmp(e2, ".flac") == 0);
        int d2 = (e2 && strcasecmp(e2, ".dsf")  == 0);
        int a2 = (e2 && (strcasecmp(e2, ".aif") == 0 || strcasecmp(e2, ".aiff") == 0));
        int lrc = d2 ? load_dsf_as_dop(seg[ti], &s2, NULL)
                     : (f2 ? load_flac(seg[ti], &s2, &m2)
                          : (a2 ? parse_aiff(seg[ti], &s2) : parse_wav(seg[ti], &s2)));
        if (lrc == 0 && f2) trim_to_whole_frames(&s2);
        if (lrc != 0 || !audio_source_valid(&s2)) {
            fprintf(stderr, L(MSG_SKIP_UNREADABLE), seg[ti]);
            free_audio_source(&s2);
            continue;
        }
        uint64_t b2 = 0; int bp2 = 0;
        uint8_t *buf2 = convert_to_virtual(&s2, &vf, &b2, &bp2);
        free_audio_source(&s2);
        if (!buf2) continue;
        if (p.nTracks < MAX_TRACKS) {
            p.tracks[p.nTracks].buf = buf2; p.tracks[p.nTracks].bytes = b2;
            p.nTracks++; totalBytes += b2;
        } else {
            free(buf2);
        }
    }

    uint64_t lockedBytes = 0;
    for (int ti = 0; ti < p.nTracks; ti++)
        if (mlock(p.tracks[ti].buf, p.tracks[ti].bytes) == 0)
            lockedBytes += p.tracks[ti].bytes;

    totalSec = (double)totalBytes / vf.mBytesPerFrame / vf.mSampleRate;
    printf(L(MSG_RAM), totalBytes / 1048576.0, p.nTracks, totalSec,
           (lockedBytes == totalBytes) ? L(MSG_RAM_MLOCK) : L(MSG_RAM_MLOCK_PART));
    if (wIsFloat) {
        if (vf.mFormatFlags & kAudioFormatFlagIsFloat) printf("%s", L(MSG_PATH_FLOAT32_DIRECT));
        else printf("%s", L(MSG_PATH_FLOAT32_SRC));
    } else if (bitPerfect && integerMode) printf("%s", L(MSG_PATH_INT));
    else if (bitPerfect && !integerMode)  printf("%s", L(MSG_PATH_FLOAT));
    else                                  printf("%s", L(MSG_PATH_LOSSY));

    if (AudioDeviceCreateIOProcID(dev, io_proc, &p, &procID) != noErr) {
        fprintf(stderr, "IOProc create failed.\n");
        procID = NULL;
        goto cleanup;
    }
    if (AudioDeviceStart(dev, procID) != noErr) {
        fprintf(stderr, "Device start failed.\n");
        goto cleanup;
    }
    printf("%s", L(MSG_PLAYING_KEYS));

    {
        const double bpsec = vf.mBytesPerFrame * vf.mSampleRate;
        raw_mode_on();
        int tick = 0;
        while (!atomic_load(&p.done) && !g_stop) {
            int k;
            while ((k = read_key()) != 0) {
                if (k == 'n' || k == 'N') {
                    int c = atomic_load(&p.cur);
                    if (c + 1 < p.nTracks) atomic_store(&p.seekReq, (c + 1) + 1);
                } else if (k == 'b' || k == 'B') {
                    int c = atomic_load(&p.cur);
                    int target = (c - 1 >= 0) ? (c - 1) : 0;
                    atomic_store(&p.seekReq, target + 1);
                } else if (k == ' ') {
                    atomic_store(&p.paused, !atomic_load(&p.paused));
                } else if (k == 'q' || k == 'Q') {
                    g_stop = 1;
                }
            }
            if (tick++ % 5 == 0 && p.nTracks > 0 && bpsec > 0.0) {
                int cur = atomic_load(&p.cur);
                uint64_t pos = atomic_load(&p.pos);
                int pausedNow = atomic_load(&p.paused);
                if (cur >= p.nTracks) cur = p.nTracks - 1;
                if (cur < 0) cur = 0;
                double tElapsed = (double)pos / bpsec;
                double tTotal   = (double)p.tracks[cur].bytes / bpsec;
                double tRemain  = tTotal - tElapsed; if (tRemain < 0) tRemain = 0;
                uint64_t allPos = pos;
                for (int ti = 0; ti < cur; ti++) allPos += p.tracks[ti].bytes;
                double qElapsed = (double)allPos / bpsec;
                double qRemain  = totalSec - qElapsed; if (qRemain < 0) qRemain = 0;
                int te_m=(int)tElapsed/60, te_s=(int)tElapsed%60;
                int tr_m=(int)tRemain/60,  tr_s=(int)tRemain%60;
                int qe_m=(int)qElapsed/60, qe_s=(int)qElapsed%60;
                int qr_m=(int)qRemain/60,  qr_s=(int)qRemain%60;
                printf("\r  %s ", pausedNow ? "⏸" : "▶");
                printf(L(MSG_TRACK_SOR), (cur + 1), p.nTracks, te_m, te_s, tr_m, tr_s,
                       qe_m, qe_s, qr_m, qr_s);
                fflush(stdout);
            }
            usleep(40 * 1000);
        }
        printf("\n");
        raw_mode_off();
    }

    rc = g_stop ? 1 : 0;        /* finished normally (or the user quit) */

cleanup:
    /* === CENTRAL CLEANUP === every error path and the normal end land here */
    if (procID) {
        request_silence(&p);            /* clean, zero-level stop */
        AudioDeviceStop(dev, procID);
        AudioDeviceDestroyIOProcID(dev, procID);
    }
    free_player_tracks(&p);
    if (w_loaded) free_audio_source(&w);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Folder traversal for -dir                                           */
/* Collects the playable files (.wav/.flac/.aif/.aiff/.dsf), sorted by */
/* name. Full paths are held in a static buffer.                       */
/* ------------------------------------------------------------------ */

static char  g_dirPaths[MAX_TRACKS][1024];   /* storage for the full paths */

static int is_playable_ext(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".wav")  == 0 ||
            strcasecmp(ext, ".flac") == 0 ||
            strcasecmp(ext, ".dsf")  == 0 ||
            strcasecmp(ext, ".aif")  == 0 ||
            strcasecmp(ext, ".aiff") == 0);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/* Is a directory entry a folder? (d_type fast path, stat fallback) */
static int entry_is_dir(const char *fullpath, const struct dirent *e)
{
#ifdef DT_DIR
    if (e->d_type == DT_DIR) return 1;
    if (e->d_type != DT_UNKNOWN && e->d_type != DT_LNK) return 0;
#endif
    struct stat st;
    if (stat(fullpath, &st) == 0) return S_ISDIR(st.st_mode);
    return 0;
}

/*
 * Recursive folder walk in ALBUM ORDER:
 *   1. the playable files of the current folder, sorted by name
 *   2. then the subfolders, sorted by name, each descended into
 * So dropping a collection folder plays every album in its own track order,
 * one after another. The depth is unlimited.
 *
 * Writes into 'paths' (through the g_dirPaths static buffer); '*pn' is the
 * running count carried through the recursion.
 */
static void collect_dir_rec(const char *dir, const char **paths, int maxPaths, int *pn)
{
    DIR *d = opendir(dir);
    if (!d) { perror(dir); return; }

    /* Entries are gathered into two groups — files and subfolders — each
     * sorted by name (album-like ordering). The buffers live on the heap so
     * that deep recursion does not stress the stack. */
    enum { MAX_SUBDIRS = 1024 };
    char (*filesBuf)[1024] = malloc(sizeof(char[MAX_TRACKS][1024]));
    char (*subdirs)[1024]  = malloc(sizeof(char[MAX_SUBDIRS][1024]));
    if (!filesBuf || !subdirs) { free(filesBuf); free(subdirs); closedir(d); return; }
    int nf = 0, nd = 0;

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;                 /* skip hidden entries (and . / ..) */

        char full[1024];
        size_t dl = strlen(dir);
        int hasSlash = (dl > 0 && dir[dl - 1] == '/');
        snprintf(full, sizeof(full), "%s%s%s", dir, hasSlash ? "" : "/", e->d_name);

        if (entry_is_dir(full, e)) {
            if (nd < MAX_SUBDIRS) { snprintf(subdirs[nd], 1024, "%s", full); nd++; }
        } else if (is_playable_ext(e->d_name)) {
            if (nf < MAX_TRACKS) { snprintf(filesBuf[nf], 1024, "%s", full); nf++; }
        }
    }
    closedir(d);

    /* 1) this level's files, sorted, into the output */
    qsort(filesBuf, nf, sizeof(filesBuf[0]), cmp_str);
    for (int i = 0; i < nf && *pn < maxPaths; i++) {
        snprintf(g_dirPaths[*pn], sizeof(g_dirPaths[0]), "%s", filesBuf[i]);
        paths[*pn] = g_dirPaths[*pn];
        (*pn)++;
    }
    free(filesBuf);
    filesBuf = NULL;

    /*
     * 2) the subfolders, sorted, each descended into.
     * The two fixed-size scratch buffers are ~1.25 MB per level, and the
     * recursion happens inside this function, so holding them across the
     * descent would multiply that by the depth of the tree. We therefore
     * compact the subdirectory list into an exactly-sized allocation and
     * release the big buffer BEFORE recursing.
     */
    qsort(subdirs, nd, sizeof(subdirs[0]), cmp_str);
    char (*subCompact)[1024] = NULL;
    if (nd > 0) {
        subCompact = (char (*)[1024])malloc((size_t)nd * 1024);
        if (subCompact) memcpy(subCompact, subdirs, (size_t)nd * 1024);
    }
    if (subCompact) {
        free(subdirs);
        subdirs = NULL;
        for (int i = 0; i < nd && *pn < maxPaths; i++)
            collect_dir_rec(subCompact[i], paths, maxPaths, pn);
        free(subCompact);
    } else {
        /* allocation failed: fall back to recursing from the original buffer */
        for (int i = 0; i < nd && *pn < maxPaths; i++)
            collect_dir_rec(subdirs[i], paths, maxPaths, pn);
        free(subdirs);
    }
}

/* Returns the number of files collected; sets the paths[] pointers into the
 * g_dirPaths static buffer. Unlimited depth, album-like ordering. */
static int collect_dir(const char *dir, const char **paths, int maxPaths, int startAt)
{
    int n = startAt;
    collect_dir_rec(dir, paths, maxPaths, &n);
    return n;
}

static void print_version(void)
{
    printf("bpplay %s (%s edition)\n", BPPLAY_VERSION, BPPLAY_EDITION);
    printf("Copyright (C) 2026 Koscsó Ferenc\n");
    printf("License GPLv3+: GNU GPL version 3 or later "
           "<https://gnu.org/licenses/gpl.html>\n");
    printf("This is free software: you are free to change and "
           "redistribute it.\n");
    printf("There is NO WARRANTY, to the extent permitted by law.\n");
    printf("FLAC decoding: dr_flac by David Reid "
           "(public domain / MIT-0)\n");
}

int main(int argc, char **argv)
{
    /* The main thread (UI refresh + key reading) is NOT critical: the actual
     * audio runs on CoreAudio's realtime (time-constraint) thread, which the
     * system already prioritises. We therefore put the main thread on
     * QOS_CLASS_UTILITY so it can never compete with realtime audio
     * processing.
     * NOTE: this is pure resource management, NOT a sound-quality setting —
     * the bit-perfect data is identical either way. */
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);

    const char *paths[MAX_TRACKS];
    int   nPaths = 0;
    long  devIndex = -1;
    int   dopMode = 0;
    int   truncated = 0;

    /* language first, so that -l already lists in the right language */
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "-hu") == 0) g_lang = LANG_HU;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 ||
            strcmp(argv[i], "-version") == 0 ||
            strcmp(argv[i], "--version") == 0) { print_version(); return 0; }
        if (strcmp(argv[i], "-l") == 0) return list_devices();
        if (strcmp(argv[i], "-hu") == 0) continue;
        if (strcmp(argv[i], "-dop") == 0) { dopMode = 1; continue; }
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) { devIndex = atol(argv[++i]); continue; }
        if (strcmp(argv[i], "-dir") == 0 && i + 1 < argc) {
            /* every playable file of a folder, sorted by name. The result is
             * APPENDED to whatever was listed before, so -dir can be combined
             * with individual files and with a second -dir. */
            int before = nPaths;
            nPaths = collect_dir(argv[++i], paths, MAX_TRACKS, nPaths);
            if (nPaths == before) {
                fprintf(stderr, L(MSG_DIR_EMPTY), argv[i]);
                if (nPaths == 0) return 1;
            }
            continue;
        }
        if (nPaths < MAX_TRACKS) paths[nPaths++] = argv[i];
        else truncated = 1;
    }
    if (nPaths == 0) {
        fprintf(stderr, L(MSG_USAGE), argv[0]);
        return 1;
    }
    if (truncated || nPaths >= MAX_TRACKS)
        fprintf(stderr, "Note: the queue is limited to %d files; the rest were skipped.\n",
                MAX_TRACKS);

    /* DSD handling: the .dsf extension ON ITS OWN switches to DoP — no separate
     * -dop flag is needed. Safety is preserved: a DoP stream is only ever
     * produced from a .dsf file (load_dsf_as_dop reads nothing else), so noise
     * can never be sent to a non-DSD file by accident. The -dop flag is still
     * accepted for backward compatibility, but it no longer changes anything;
     * the only rule is that giving -dop for a non-.dsf first file is treated as
     * an error, to avoid a misunderstanding. */
    {
        const char *e0 = strrchr(paths[0], '.');
        int firstIsDsf = (e0 && strcasecmp(e0, ".dsf") == 0);
        if (dopMode && !firstIsDsf) { fprintf(stderr, "%s", L(MSG_DOP_NEEDS_DSF)); return 1; }
    }

    AudioObjectID dev = (devIndex >= 0) ? device_by_index((UInt32)devIndex)
                                        : default_output_device();
    if (dev == kAudioObjectUnknown) { fprintf(stderr, "%s", L(MSG_NO_DEVICE)); return 1; }
    g_dev = dev;
    printf("%s", L(MSG_DEVICE));
    print_device_name(dev);
    printf("\n");

    signal(SIGINT, on_sigint);
    atexit(release_device);
    atexit(allow_sleep);                       /* make sure it is released on exit */

    if (acquire_hog(dev) != 0) return 1;
    try_disable_mixing(dev);
    prevent_sleep();                           /* do not fall asleep mid-playback */

    /* ---- Segmented playback: the queue is split into format segments ----
     * A segment = consecutive files of the SAME format (sample rate, bits,
     * channels). Playback is gapless within a segment; at a segment boundary a
     * clean sample-rate switch happens (with a short, unavoidable gap). */
    double prevRate = 0;
    int segStart = 0;
    int userQuit = 0;

    /* count the segments up front (for the [x/y] display) using the lightweight
     * header probe. An unreadable file becomes its own single-file segment;
     * play_one_segment then handles or skips it. */
    int segTotal = 0;
    {
        int i = 0;
        AudioFmt cur; int haveCur = 0;
        memset(&cur, 0, sizeof(cur));
        while (i < nPaths) {
            AudioFmt f;
            if (probe_format(paths[i], &f) != 0) { segTotal++; i++; haveCur = 0; continue; }
            if (!haveCur || !fmt_equal(&cur, &f)) { segTotal++; cur = f; haveCur = 1; }
            i++;
        }
    }

    int segIndex = 0;
    while (segStart < nPaths && !userQuit) {
        /* how far does the identical format run? */
        AudioFmt segFmt;
        int haveFmt = (probe_format(paths[segStart], &segFmt) == 0);
        int segEnd = segStart + 1;
        if (haveFmt) {
            while (segEnd < nPaths) {
                AudioFmt f;
                if (probe_format(paths[segEnd], &f) != 0) break;
                if (!fmt_equal(&segFmt, &f)) break;
                segEnd++;
            }
        }
        int segCount = segEnd - segStart;

        int rc = play_one_segment(dev, &paths[segStart], segCount,
                                  &prevRate, segIndex, segTotal);
        if (rc == 1) userQuit = 1;     /* q or Ctrl+C */
        /* on rc == -1 (error) we simply move on to the next segment */

        segStart = segEnd;
        segIndex++;
    }

    allow_sleep();                             /* playback over: sleep is fine again */
    release_device();
    printf("%s", L(MSG_DONE));
    return userQuit ? 1 : 0;
}
