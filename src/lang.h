/*
 * lang.h — part of bpplay (macOS Core edition)
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
 * lang.h — the bilingual (English default / Hungarian) message layer
 *
 * Usage:
 *   - g_lang = LANG_EN (default) or LANG_HU (via the -hu flag)
 *   - L(MSG_KEY) returns the string in the current language
 *
 * To add a language: extend the Lang enum and every row of the table.
 *
 * Note: low-level loader diagnostics (malformed file headers and the like)
 * are deliberately English-only and printed directly, because they address a
 * broken file rather than a normal playback event. Everything the user sees
 * during ordinary use goes through this table.
 */

#ifndef LANG_H
#define LANG_H

typedef enum { LANG_EN = 0, LANG_HU = 1, LANG_COUNT } Lang;

static Lang g_lang = LANG_EN;

/* Message keys — the order must match the rows of the table below */
typedef enum {
    MSG_USAGE,
    MSG_DEVICES_HEADER,
    MSG_NO_DEVICE,
    MSG_UNKNOWN_DEVICE,
    MSG_FILE_INFO,            /* "File: %s — %u Hz, %u bit, %u ch, %.1f s" */
    MSG_TITLE,                /* "Title: " prefix */
    MSG_MQA_DETECTED,         /* "MQA source detected" */
    MSG_MQA_ORIGINAL,         /* " · original %.1f kHz" */
    MSG_MQA_DAC,              /* " · decoding in the DAC\n" */
    MSG_MQA_WARN_BITS,        /* "Warning: output path < 24-bit..." */
    MSG_DEVICE,               /* "Device: " */
    MSG_HOG_OK,               /* "Hog mode: exclusive access acquired." */
    MSG_HOG_TAKEN,            /* "Device already owned by another process (pid %d)." */
    MSG_HOG_UNAVAIL,          /* "Note: hog mode unavailable on this device (st=%d)." */
    MSG_MIXING_OFF,           /* "Mixing disabled on device." */
    MSG_SRATE_SET,            /* "Sample rate: %.0f Hz." */
    MSG_SRATE_REJECT,         /* "Device rejected %.0f Hz sample rate (st=%d)." */
    MSG_SRATE_TIMEOUT,        /* "Timeout switching sample rate." */
    MSG_PHYS_FMT,             /* "Physical format: %u-bit integer." */
    MSG_VFMT,                 /* "Virtual format: %s, %u-bit, %u B/frame %s" */
    MSG_VFMT_INT_ON,          /* "— INTEGER MODE ACTIVE, signal path truly conversion-free" */
    MSG_VFMT_FLOAT,           /* "— float virtual format: the HAL gave no integer path" */
    MSG_PATH_INT,             /* bit-perfect integer path */
    MSG_PATH_FLOAT,           /* bit-perfect via lossless float intermediate */
    MSG_PATH_LOSSY,           /* one deterministic conversion */
    MSG_RAM,                  /* "RAM: %.1f MB loaded (%d track, %.1f s)%s — no disk I/O during playback." */
    MSG_RAM_MLOCK,            /* ", mlock active" */
    MSG_RAM_MLOCK_PART,       /* " (mlock partial)" */
    MSG_FMT_BREAK,            /* format change breaks gapless */
    MSG_SKIP_UNREADABLE,      /* "Skipped (unreadable): %s" */
    MSG_NOFMT,                /* "Cannot read stream format." */
    MSG_NO_FLAC,              /* "Cannot open as FLAC: %s" */
    MSG_PLAYING_KEYS,         /* "Playing...  [n] next  [b] prev  [space] pause  [q] quit" */
    MSG_TRACK_SOR,            /* "Track" / "Queue" labels via format */
    MSG_PATH_FLOAT32_SRC,     /* 32-bit float source → single HAL conversion */
    MSG_PATH_FLOAT32_DIRECT,  /* 32-bit float source → float virtual, direct copy */
    MSG_DSD_NEEDS_DOP,        /* .dsf needs explicit -dop flag */
    MSG_DOP_NEEDS_DSF,        /* -dop only valid with .dsf */
    MSG_DSD_INFO,             /* DSD info line: rate, DSDxx, DoP PCM rate */
    MSG_DIR_EMPTY,            /* no playable files in directory */
    MSG_DOP_RATE_HIGH,        /* DoP rate too high for the DAC */
    MSG_DONE,                 /* "Done, device released." */
    MSG__COUNT
} MsgKey;

static const char *MSG_TABLE[MSG__COUNT][LANG_COUNT] = {
    /* MSG_USAGE */            { "Usage: %s [-l] [-hu] [-d devindex] [-dir folder] file1 [file2 ...]\n  .dsf files play as DSD (DoP) automatically. Multiple files / folder:\n  gapless within identical format, clean switch between formats.\n",
                                "Használat: %s [-l] [-hu] [-d eszközindex] [-dir mappa] fájl1 [fájl2 ...]\n  A .dsf fájlok automatikusan DSD-ként (DoP) szólnak. Több fájl / mappa esetén:\n  gapless azonos formátumon belül, tiszta váltás formátumok közt.\n" },
    /* MSG_DEVICES_HEADER */   { "Output devices:\n", "Kimeneti eszközök:\n" },
    /* MSG_NO_DEVICE */        { "No such device.\n", "Nincs ilyen eszköz.\n" },
    /* MSG_UNKNOWN_DEVICE */   { "(unknown device)", "(ismeretlen eszköz)" },
    /* MSG_FILE_INFO */        { "File: %s — %u Hz, %u-bit, %u ch, %.1f s\n",
                                "Fájl: %s — %u Hz, %u bit, %u csatorna, %.1f mp\n" },
    /* MSG_TITLE */            { "Title: ", "Cím: " },
    /* MSG_MQA_DETECTED */     { "MQA source detected", "MQA forrás felismerve" },
    /* MSG_MQA_ORIGINAL */     { " · original %.1f kHz", " · eredeti %.1f kHz" },
    /* MSG_MQA_DAC */          { " · decoding in the DAC\n", " · dekódolás a DAC-ban\n" },
    /* MSG_MQA_WARN_BITS */    { "  Warning: output path < 24-bit — the MQA signalling may be corrupted.\n",
                                "  Figyelem: a kimeneti út < 24 bit — az MQA-jelzés sérülhet.\n" },
    /* MSG_DEVICE */           { "Device: ", "Eszköz: " },
    /* MSG_HOG_OK */           { "Hog mode: exclusive access acquired.\n", "Hog mode: exkluzív hozzáférés megszerezve.\n" },
    /* MSG_HOG_TAKEN */        { "Device already owned by another process (pid %d).\n",
                                "Az eszközt már birtokolja egy másik folyamat (pid %d).\n" },
    /* MSG_HOG_UNAVAIL */      { "Note: hog mode unavailable on this device (st=%d).\n",
                                "Figyelem: hog mode nem érhető el ezen az eszközön (st=%d).\n" },
    /* MSG_MIXING_OFF */       { "Mixing disabled on device.\n", "Mixing kikapcsolva az eszközön.\n" },
    /* MSG_SRATE_SET */        { "Sample rate: %.0f Hz.\n", "Mintavételi frekvencia: %.0f Hz.\n" },
    /* MSG_SRATE_REJECT */     { "Device rejected %.0f Hz (st=%d).\n", "A %.0f Hz mintavételt az eszköz nem fogadta el (st=%d).\n" },
    /* MSG_SRATE_TIMEOUT */    { "Timeout switching sample rate.\n", "Időtúllépés a mintavétel-váltásnál.\n" },
    /* MSG_PHYS_FMT */         { "Physical format: %u-bit integer.\n", "Fizikai formátum: %u bit integer.\n" },
    /* MSG_VFMT */             { "Virtual format: %s, %u-bit, %u B/frame %s\n",
                                "Virtuális formátum: %s, %u bit, %u B/frame %s\n" },
    /* MSG_VFMT_INT_ON */      { "— INTEGER MODE ACTIVE, signal path truly conversion-free",
                                "— INTEGER MODE AKTÍV, a jelút valóban konverziómentes" },
    /* MSG_VFMT_FLOAT */       { "— float virtual format: the HAL gave no integer path",
                                "— float virtuális formátum: a HAL nem adott integer utat" },
    /* MSG_PATH_INT */         { "Signal path: bit-perfect to the DAC, integer route (zero conversion).\n",
                                "Jelút: bitre pontos a DAC-ig, integer úton (nulla konverzió).\n" },
    /* MSG_PATH_FLOAT */       { "Signal path: bit-perfect to the DAC — the 16/24-bit content fits the\n       float32 intermediate losslessly; the HAL does the final (int) step to the DAC.\n",
                                "Jelút: bitre pontos a DAC-ig — a 16/24 bites tartalom veszteségmentesen\n       fér a float32 köztesbe, a HAL végzi az utolsó (int) lépést a DAC felé.\n" },
    /* MSG_PATH_LOSSY */       { "Signal path: one deterministic conversion (source bit-depth exceeds the\n       output format — minimal, controlled loss).\n",
                                "Jelút: egyszeri, determinisztikus konverzióval (a forrás bitmélysége\n       meghaladja a kimeneti formátumét — minimális, kontrollált veszteség).\n" },
    /* MSG_RAM */              { "RAM: %.1f MB loaded (%d track, %.1f s)%s — no disk I/O during playback.\n",
                                "RAM: %.1f MB betöltve (%d track, %.1f mp)%s — lejátszás alatt nincs lemez I/O.\n" },
    /* MSG_RAM_MLOCK */        { ", mlock active", ", mlock aktív" },
    /* MSG_RAM_MLOCK_PART */   { " (mlock partial)", " (mlock részleges)" },
    /* MSG_FMT_BREAK */        { "\nFormat change in queue (%s: %u Hz/%u-bit) — gapless chain breaks here.\nA sample-rate switch cannot be gapless. Start the remainder separately.\n",
                                "\nFormátumváltás a sorban (%s: %u Hz/%u bit) — itt megszakad a\ngapless lánc. A track-váltás mintavétel-váltást igényelne, ami\nnem lehet hézagmentes. A maradékot külön indítsd.\n" },
    /* MSG_SKIP_UNREADABLE */  { "Skipped (unreadable): %s\n", "Kihagyva (nem olvasható): %s\n" },
    /* MSG_NOFMT */            { "Cannot read stream format.\n", "Nem olvasható a stream formátuma.\n" },
    /* MSG_NO_FLAC */          { "Cannot open as FLAC: %s\n", "Nem nyitható meg FLAC-ként: %s\n" },
    /* MSG_PLAYING_KEYS */     { "Playing...  [n] next  [b] prev  [space] pause  [q] quit\n",
                                "Lejátszás...  [n] következő  [b] előző  [szóköz] szünet  [q] kilépés\n" },
    /* MSG_TRACK_SOR */        { "Track %d/%d  %d:%02d / -%d:%02d   │   Queue  %d:%02d / -%d:%02d     ",
                                "Track %d/%d  %d:%02d / -%d:%02d   │   Sor  %d:%02d / -%d:%02d     " },
    /* MSG_PATH_FLOAT32_SRC */ { "Signal path: 32-bit float source → the HAL performs a single conversion\n       to the DAC's physical format; the full audible range is preserved.\n",
                                "Jelút: 32 bit float forrás → a DAC fizikai formátumára a HAL végzi az\n       egyetlen konverziót; a teljes hallható tartomány megőrződik.\n" },
    /* MSG_PATH_FLOAT32_DIRECT */ { "Signal path: 32-bit float source → float32 virtual format: direct copy,\n       zero conversion on our side; the HAL does the final step to the DAC.\n",
                                "Jelút: 32 bit float forrás → float32 virtuális formátum: közvetlen másolás,\n       a mi oldalunkon nulla konverzió; a HAL végzi az utolsó lépést a DAC felé.\n" },
    /* MSG_DSD_NEEDS_DOP */    { "This is a DSD (.dsf) file. DSD playback requires the explicit -dop flag,\nand a DoP-capable DAC. Wrong routing can produce loud noise — start with:\n  bpplay -dop -d <dac> file.dsf\n",
                                "Ez egy DSD (.dsf) fájl. A DSD lejátszás explicit -dop kapcsolót igényel,\nés DoP-képes DAC-ot. Rossz útra küldve hangos zaj lehet — így indítsd:\n  bpplay -dop -d <dac> fájl.dsf\n" },
    /* MSG_DOP_NEEDS_DSF */    { "The -dop flag is only valid with a .dsf (DSD) file.\n",
                                "A -dop kapcsoló csak .dsf (DSD) fájllal használható.\n" },
    /* MSG_DSD_INFO */         { "DSD source: %.4f MHz (DSD%u) → DoP PCM carrier %u Hz · decoding in the DAC\n",
                                "DSD forrás: %.4f MHz (DSD%u) → DoP PCM hordozó %u Hz · dekódolás a DAC-ban\n" },
    /* MSG_DIR_EMPTY */        { "No playable files (.wav/.flac/.aiff/.dsf) found in: %s\n",
                                "Nincs lejátszható fájl (.wav/.flac/.aiff/.dsf) a mappában: %s\n" },
    /* MSG_DOP_RATE_HIGH */    { "Could not set sample rate for DSD%u (DoP carrier %.1f kHz).\n  This DAC / USB chain may not support this DoP rate. DSD64 (176.4 kHz DoP) is the safest.\n",
                                "Nem sikerült a mintavételt beállítani DSD%u-hoz (DoP hordozó %.1f kHz).\n  Ez a DAC / USB-lánc lehet, hogy nem viszi ezt a DoP-rátát. A DSD64 (176,4 kHz DoP) a legbiztosabb.\n" },
    /* MSG_DONE */             { "Done, device released.\n", "Kész, az eszköz felszabadítva.\n" },
};

static inline const char *L(MsgKey k) { return MSG_TABLE[k][g_lang]; }

#endif /* LANG_H */
