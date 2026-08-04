# About bpplay — Philosophy and Architecture
### *(Manual chapter — a condensed summary of Parts 1 and 2 of the blog series)*

---

## The starting point: analog minimalism

The guiding principle of My Reel Club® recordings is simple: the best thing you can do to a recording is often nothing at all. Few microphones, a direct stereo setup, no post-production — so it is known exactly what is in the groove or on the tape. bpplay translates this principle into the world of computer-based digital playback: a minimalist, bit-perfect macOS player, written in C, with a single goal — to deliver the samples of a music file to the DAC along the shortest, deterministic path, untouched.

bpplay does not promise that it "sounds better." It promises that you can **know what happens during playback**. Every step of the chain is visible and verifiable: one C file and three headers, around two thousand lines in total, plus a single embedded FLAC decoder — no external build dependencies, and short enough to be read end to end.

## What a player normally does — and what bpplay does not

With an ordinary macOS application, sound does not reach the DAC directly but passes through the system audio mixer, CoreAudio. There, resampling, floating-point mixing, and software volume control can occur. In most situations these are convenient and useful steps — but each is a place where the signal can change, and where processing can generate incidental noise.

bpplay bypasses this entire intermediate layer. It takes exclusive control of the DAC (**hog mode**), disables mixing, and hands over the samples through a direct, real-time callback (IOProc). There is no software volume, no dither, no sample-rate conversion during playback. Volume and every further step are left to the device designed precisely for them: the DAC and the amplifier.

## The bit-perfect signal path

bpplay's signal path consists of four steps that each run only once and all finish before the DAC is started:

1. **Device acquisition (hog mode).** The program becomes the sole owner of the DAC; no other application can interfere, and the system mixer is disabled.
2. **Format negotiation with the HAL.** bpplay asks the hardware (Hardware Abstraction Layer): is there an integer physical format the source fits into? If so, it sets it; if the device only offers float32 at the given rate, it accepts that too — and reports which route it takes.
3. **Loading and one-time format matching.** The whole file is read into memory; if needed, a single, lossless conversion is performed — before playback, on a non-real-time thread.
4. **mlock.** The music buffer stays physically in RAM and cannot be paged out by the operating system.

During playback, the **IOProc** performs exactly one operation: `memcpy`. It copies the pre-decoded data sitting in memory into the DAC's buffer. No floating-point math, no memory allocation, no file reads, no locks. Loading and playback are two entirely separate phases: what is permitted during loading (decoding, DoP packing, format matching) is forbidden on the real-time thread.

Click-free stopping and pausing are handled by a 1024-frame linear ramp (about 23 ms at 44.1 kHz). The ramp never touches a music sample: it repeats a copy of the last real frame written and scales that copy, so the audible content stays bit-exact and only material bpplay appended itself is ever scaled. The ramp state is held in the player, so a ramp longer than one CoreAudio block simply continues in the next callback rather than being cut short. For DoP/DSD streams the ramp is deliberately disabled — a DoP word carries the 0x05/0xFA marker in its top byte, and scaling it would destroy both the marker and the DSD bits; there, stopping is an immediate mute.

## Whole music into memory — the full-RAM model

One of bpplay's most distinctive features is its loading method: the whole track (or playlist) is read into memory before a single sound plays. There is no on-the-fly buffering, no disk reading during playback — the track sits in RAM, locked there (`mlock`), and reaches the DAC from there with a single memory copy.

This also has an electrical benefit. A traditional player continuously reads from storage, decodes, and manages buffers; each such operation imposes a brief, spiky load on the machine's power supply. A modern NVMe SSD's read spike can jump from 15 mA to 2 A in microseconds — with a rate of current change (di/dt) of nearly two million amps per second — which can generate high-frequency RF/EMI noise. The full-RAM model eliminates this noise source at its source: once the track is in memory, during playback there is no disk reading, no decoding, no paging — so the load spikes that could generate noise in the first place do not arise.

bpplay does not claim it "sounds better because of this." The full-RAM model removes the noise source itself; whether that has an audible consequence depends on the DAC's design.

## DoP and DSD

DSD (Direct Stream Digital) stores the signal not in PCM samples over time but in 1-bit samples at an extremely high frequency (11.2896 MHz per channel for DSD256). This bit density cannot be carried directly over USB audio interfaces, so bpplay uses the **DoP** (DSD over PCM, v1.1) protocol: it packs 16 DSD bits into a 24-bit PCM sample, with a marker byte alternating between 0x05 and 0xFA in the upper 8 bits. The carrier PCM rate is one-sixteenth of the original DSD rate (DSD256 → 705.6 kHz).

The alternating marker byte is a self-checking handshake: if the DAC supports DSD over DoP, it unpacks the bits and hands them to its own DSD decoder; if not, it stays passive and treats the data as plain PCM. The 16 DSD bits are bit-for-bit identical to those in the DSF file — the DoP frame is only the transport protocol, not the content. bpplay is validated up to DSD256.

## Why the memory requirement grows

For DSD files, memory usage can be strikingly large: a 4 GB DSD256 file can become as much as 7.5 GB in RAM after loading. This is not waste but the product of two lossless steps:

- **DoP packing: ×1.5** — one marker byte is added for every two DSD bytes.
- **float32 path: ×1.333** — if the HAL gives no integer path, bpplay converts the samples to IEEE 754 float32 (3 → 4 bytes/sample). This is lossless, because the float32 mantissa is 24 bits, so the 16/24-bit integer range is representable in it exactly.

Their product is exactly 2.0× relative to the raw DSD data. Both steps run once, at load time — during playback the data merely sits in memory and the IOProc reads from it with `memcpy`. The DSD content is bit-for-bit untouched.

## Format handling and gapless

Gapless playback is the default in bpplay: the IOProc immediately copies the next track's data as soon as the previous one runs out. This, however, only holds between files of the same format (same rate, bit depth, channel count). A sample-rate change requires a hardware reconfiguration of the DAC, which causes a brief gap.

bpplay therefore splits the playlist — before loading, with a lightweight header read — into **format segments**. Everything is gapless within a segment; at a segment boundary a clean configuration change happens, which the terminal reports immediately. bpplay does not hide what is unavoidable — it tells you why it exists and where.

bpplay handles **AIFF**'s big-endian byte order at load time, with a per-sample byte swap; this is arithmetically lossless and does not affect bit-perfectness. An **MQA**-bearing FLAC is passed through by bpplay as a plain FLAC; actual MQA decoding — if the DAC supports it — happens in the DAC, with bpplay not cutting into the signal.

## Supported formats — and what is deliberately omitted

**Supported:** WAV, FLAC, AIFF, DSF (via DoP, up to DSD256); integer and float variants; multichannel matched to the DAC for PCM (DSF is stereo only); recursive folder traversal; MQA pass-through.

**Not supported — by deliberate choice:**
- **ALAC** — lossless, like FLAC, so the decoded PCM is bit-identical: no sound-quality advantage. Its container (MP4/M4A) is moreover orders of magnitude more complex, and the "easy route" (native CoreAudio decoding) would lead through exactly the opaque layer bpplay bypasses. Anyone with an ALAC library can convert it losslessly to FLAC.
- **.dff / .wsd** — possible in principle (the DoP back end is format-agnostic), but lower priority due to their marginal practical occurrence; DST-compressed .dff would require a separate decoder.

The missing features (library management, graphical interface, network streaming, signal processing, volume) are not oversights: each would be a place where the signal could change or noise could arise between the file and the DAC.

## The computer as a noise source — and the clock question

The full-RAM model makes not only the signal path deterministic but also quiets the machine's electrical footprint: the disk-I/O transients, the processor ripple caused by decoding, the interrupt stream, and — through the absence of a graphical interface — the display activity all cease at their source.

The wording about the clock must be precise. Most of today's serious USB DACs use **asynchronous USB**: in that case **the DAC is the clock master**, its own crystal oscillator times the conversion, and its input buffer decouples all of the machine's timing imprecision. bpplay therefore does not give — and does not claim to give — a better clock. What it can genuinely help is the **electrical coupling path**: fewer di/dt transients on the machine side mean less EMI and power-rail noise, which could otherwise degrade the DAC's own clock phase noise and the noise floor — provided there is a coupling path (shared ground, bus-derived power, imperfect isolation). A galvanically well-isolated, separately powered DAC breaks this coupling anyway; bpplay quiets at the source, and isolation blocks the rest.

## A reference, not a competitor

bpplay forgoes a polished interface and convenience features, and in return gives a **reference**: a fixed point to measure against. Because the route from file to DAC is known, deterministic, and intervention-free, when getting to know a new DAC the result you hear is much more clearly the work of the DAC — the software does not interfere with the judgment. This role aligns exactly with My Reel Club® practice: a known source, a predictable player, and at the end the single unknown worth examining — the DAC itself.

bpplay was created with the assistance of the Claude Opus 4.8 language model; the architecture (full-RAM model, hog mode, intervention-free signal path) and the features followed from hi-fi principles and practical experience, not from the model. The model was a partner in the implementation — in the CoreAudio API, in the bit-exact code, in debugging.

## Validated state

bpplay is validated up to DSD256, via Gustard XMOS and Amanero Combo384/768 interfaces (with TT and Gustard DACs). The chain was tested in three connection topologies: the DAC plugged directly into the MacBook Air's port, through a Sonnet Tech Thunderbolt 5 hub, and through a no-name USB hub; with both expensive and cheap cabling. The bit-perfect behavior stayed the same in all three cases: the bit path is independent of the connection method. Requirements: macOS 10.15 or later, at least 8 GB RAM.

---
---

# A bpplay-ről — filozófia és architektúra
### *(Kézikönyv-fejezet — a blogsorozat 1. és 2. részének sűrített összefoglalása)*

---

## A kiindulópont: az analóg minimalizmus

A My Reel Club® felvételeinek vezérelve egyszerű: a legjobb, amit egy felvétellel tenni lehet, gyakran az, hogy nem történik vele semmi. Kevés mikrofon, közvetlen sztereó elrendezés, utómunka nélkül — így pontosan tudható, mi van a barázdában vagy a szalagon. A bpplay ezt az elvet fordítja le a számítógépes digitális lejátszás világára: egy minimalista, bithelyes macOS-lejátszó, C nyelven, egyetlen céllal, hogy a zenei fájl mintáit a lehető legrövidebb, determinisztikus úton, érintetlenül juttassa el a DAC-hoz.

A bpplay nem azt ígéri, hogy „jobban szól". Azt ígéri, hogy **tudható, mi történik lejátszás közben**. A lánc minden lépése látható és ellenőrizhető: egyetlen C fájl és három header, összesen mintegy kétezer sor, plusz egyetlen beágyazott FLAC-dekóder — külső build-függőség nélkül, és elég rövid ahhoz, hogy elejétől a végéig el lehessen olvasni.

## Mit csinál egy lejátszó általában — és mit nem a bpplay

Egy átlagos macOS-alkalmazással hallgatva a hang nem közvetlenül jut a DAC-hoz, hanem áthalad a rendszer hangkeverőjén, a CoreAudio-n. Ott újramintavételezés, lebegőpontos keverés és szoftveres hangerő-szabályzás történhet. A legtöbb helyzetben ezek kényelmes és hasznos lépések — de mindegyik egy-egy hely, ahol a jel megváltozhat, és ahol a feldolgozás járulékos zajt termelhet.

A bpplay ezt az egész köztes réteget megkerüli. Átveszi a DAC kizárólagos vezérlését (**hog mode**), kikapcsolja a keverést, és a mintákat egy közvetlen, valós idejű visszahíváson (IOProc) keresztül adja át. Nincs szoftveres hangerő, nincs dither, nincs mintavétel-konverzió lejátszás közben. A hangerő és minden további lépés arra az eszközre marad, amelyet pontosan erre terveztek: a DAC-ra és az erősítőre.

## A bithelyes jelút

A bpplay jelútja négy, csak egyszer lefutó lépésből áll, amelyek mind a DAC elindítása előtt befejeződnek:

1. **Eszközfoglalás (hog mode).** A program a DAC kizárólagos tulajdonosa lesz; más alkalmazás nem szólhat bele, a rendszerkeverő le van tiltva.
2. **Formátum-egyeztetés a HAL-lal.** A bpplay megkérdezi a hardvert (Hardware Abstraction Layer): van-e integer fizikai formátum, amelybe a forrásanyag illeszthető? Ha igen, azt állítja be; ha az eszköz csak float32-t tud az adott rátán, azt is elfogadja — és közli, melyik úton halad.
3. **Betöltés és egyszeri formátum-illesztés.** A teljes fájl a memóriába kerül; ha kell, egyszeri, veszteségmentes átalakítás történik — még a lejátszás előtt, egy nem valós idejű szálon.
4. **mlock.** A zenei buffer fizikailag a RAM-ban marad, az operációs rendszer nem lapozhatja ki.

Lejátszás közben az **IOProc** kizárólag egyetlen műveletet végez: `memcpy`. A memóriában ülő, előre dekódolt adatot átmásolja a DAC pufferébe. Nincs lebegőpontos számítás, nincs memóriafoglalás, nincs fájlolvasás, nincs lock. A betöltés és a lejátszás két teljesen különálló fázis: ami a betöltéskor szabad (dekódolás, DoP-csomagolás, formátum-illesztés), az a valós idejű szálon tilos.

A kattanásmentes le- és megállásról, valamint a szünetről egy 1024 frame-es lineáris rámpa gondoskodik (44,1 kHz-en kb. 23 ms). A rámpa egyetlen zenei mintát sem érint: az utoljára kiírt valódi frame másolatát ismétli és azt skálázza, így a hallható tartalom bitre pontos marad, és kizárólag olyan anyag skálázódik, amelyet a bpplay maga fűzött hozzá. A rámpa állapota a lejátszóban él, ezért ha egy rámpa nem fér bele egyetlen CoreAudio blokkba, a következő visszahívásban folytatódik, nem szakad félbe. DoP/DSD stream esetén a rámpa szándékosan ki van kapcsolva — a DoP szó felső bájtja hordozza a 0x05/0xFA jelzőt, és a skálázás mind a jelzőt, mind a DSD biteket tönkretenné; ott a leállás azonnali némítás.

## Teljes zene a memóriába — a teljes-RAM modell

A bpplay egyik legfontosabb megkülönböztető tulajdonsága a betöltés módja: a teljes szám (vagy lejátszási lista) a memóriába kerül, mielőtt egyetlen hang megszólalna. Nincs menet közbeni pufferelés, nincs lemezolvasás lejátszás alatt — a track a RAM-ban ül, oda rögzítve (`mlock`), és onnan, egyetlen memóriamásolással jut a DAC-hoz.

Ennek elektromos haszna is van. Egy hagyományos lejátszó folyamatosan olvas a háttértárról, dekódol és puffert kezel; minden ilyen művelet rövid, tüskeszerű terhelést ró a gép tápegységére. Egy modern NVMe SSD olvasási tüskéje mikroszekundumok alatt 15 mA-ről 2 A-re ugorhat — közel kétmillió amper per szekundumos áramváltozási sebességgel (di/dt) —, ami nagyfrekvenciás RF/EMI-zajt kelthet. A teljes-RAM modell ezt a zajforrást a forrásánál szünteti meg: ha a track már a memóriában van, lejátszás közben nincs lemezolvasás, nincs dekódolás, nincs lapozás — így nem keletkeznek azok a terhelési tüskék sem, amelyek egyáltalán zajt kelthetnének.

A bpplay nem állítja, hogy „ettől jobban szól". A teljes-RAM modell magát a zajforrást szünteti meg; hogy ennek hallható következménye van-e, az a DAC kialakításától függ.

## DoP és DSD

A DSD (Direct Stream Digital) nem időbeli PCM-mintákban, hanem 1 bites mintákban, extrém magas frekvencián tárolja a jelet (DSD256 esetén 11,2896 MHz csatornánként). Ez a bitsűrűség az USB audio-interfészeken nem illeszthető közvetlenül, ezért a bpplay a **DoP** (DSD over PCM, v1.1) protokollt használja: egy 24 bites PCM-mintába 16 DSD-bitet csomagol, a felső 8 bitbe pedig egy felváltva 0x05 és 0xFA értékű jelzőbájtot tesz. A hordozó PCM-ráta az eredeti DSD-ráta tizenhatoda (DSD256 → 705,6 kHz).

A jelzőbájt-váltakozás önellenőrző handshake: ha a DAC DoP-on keresztül támogatja a DSD-t, kicsomagolja a biteket és a saját DSD-dekóderének adja; ha nem, passzív marad és sima PCM-ként kezeli az adatot. A 16 DSD-bit bitről bitre ugyanaz, mint a DSF-fájlban — a DoP-keret csak a szállítás protokollja, nem a tartalom. A bpplay DSD256-ig validált.

## Miért nő meg a memóriaigény

DSD-fájlok esetén a memóriafoglalás feltűnően nagy lehet: egy 4 GB-os DSD256-fájlból a betöltés után akár 7,5 GB is lehet a RAM-ban. Ez nem pazarlás, hanem két veszteségmentes lépés szorzata:

- **DoP-csomagolás: ×1,5** — minden két DSD-bájthoz egy jelzőbájt kerül.
- **float32 út: ×1,333** — ha a HAL nem ad integer utat, a bpplay a mintákat IEEE 754 float32-re alakítja (3 → 4 bájt/minta). Ez veszteségmentes, mert a float32 mantisszája 24 bites, így a 16/24 bites egész tartomány pontosan ábrázolható benne.

A kettő szorzata pontosan 2,0× a nyers DSD-adathoz képest. Mindkét lépés betöltéskor, egyszer fut le — lejátszás közben az adat csak a memóriában ül, és az IOProc `memcpy`-vel olvas belőle. A DSD-tartalom bitről bitre érintetlen.

## Formátumkezelés és gapless

A gapless (szünetmentes) lejátszás a bpplay-ben alapból adott: az IOProc azonnal a következő track adatát másolja, amint az előzőé elfogy. Ez azonban csak azonos formátumú (azonos ráta, bitmélység, csatornaszám) fájlok között tartható. A mintavételi ráta váltása a DAC hardveres újrakonfigurálását igényli, ami rövid szünetet okoz.

A bpplay ezért a lejátszólistát betöltés előtt — könnyűsúlyú fejléc-olvasással — **formátumszakaszokra** osztja. Egy szakaszon belül minden gapless; a szakaszhatáron tiszta konfiguráció-váltás történik, amelyről a terminál azonnal tájékoztat. A bpplay nem titkolja el, ami nem megkerülhető — megmondja, miért és hol van.

Az **AIFF** big-endian bájtsorrendjét a bpplay betöltéskor, mintánkénti bájtfordítással kezeli; ez aritmetikailag veszteségmentes, a bithelyességet nem érinti. Az **MQA**-tartalmú FLAC-ot a bpplay sima FLAC-ként továbbítja; a tényleges MQA-dekódolás — ha a DAC támogatja — a DAC-ban történik, a bpplay nem vág bele a jelbe.

## Támogatott formátumok — és amit szándékosan kihagy

**Támogatott:** WAV, FLAC, AIFF, DSF (DoP-on keresztül, DSD256-ig); integer és float variánsok; PCM esetén több csatorna a DAC-hoz igazítva (a DSF kizárólag sztereó); rekurzív mappabejárás; MQA-átengedés.

**Nem támogatott — tudatos döntésből:**
- **ALAC** — veszteségmentes, akárcsak a FLAC, így a dekódolt PCM bitre azonos: nincs hangminőségi előnye. Konténere (MP4/M4A) ráadásul nagyságrendekkel összetettebb, és a „könnyű út" (CoreAudio natív dekódolás) épp azon az átláthatatlan rétegen vezetne át, amelyet a bpplay megkerül. Aki ALAC-könyvtárral rendelkezik, veszteségmentesen FLAC-ba konvertálhatja.
- **.dff / .wsd** — elvileg lehetségesek (a DoP-háttér formátum-független), de marginális gyakorlati előfordulásuk miatt alacsonyabb prioritásúak; a DST-tömörített .dff külön dekódert igényelne.

A hiányzó funkciók (könyvtárkezelés, grafikus felület, hálózati streaming, jelfeldolgozás, hangerő) nem mulasztás: mindegyik egy-egy hely volna, ahol a jel megváltozhatna vagy zaj keletkezhetne a fájl és a DAC között.

## A gép mint zajforrás — és az órajel kérdése

A teljes-RAM modell nemcsak a jelutat teszi determinisztikussá, hanem a gép elektromos lábnyomát is lecsendesíti: a lemez-I/O tranziensei, a dekódolás okozta processzor-hullámzás, a megszakítás-folyam és — a grafikus felület hiánya révén — a kijelző-aktivitás mind a forrásuknál szűnnek meg.

Fontos a pontos megfogalmazás az órajelről. A legtöbb mai, igényes USB-DAC **aszinkron USB**-t használ: ilyenkor **a DAC az órajel-mester**, a saját kristályoszcillátora időzíti a konverziót, és a bemeneti puffere leválasztja a gép összes időzítési pontatlanságát. A bpplay tehát nem ad — és nem is állít, hogy adna — jobb órajelet. Amin valóban segíthet, az az **elektromos csatolási út**: kevesebb di/dt-tranziens a gép oldalán kevesebb EMI- és tápsín-zajt jelent, amely egyébként a DAC saját órajelének fáziszaját és a zajküszöböt ronthatná — feltéve, hogy van csatolási út (közös föld, buszról vett táp, hiányos leválasztás). Egy galvanikusan jól leválasztott, saját tápellátású DAC ezt a csatolást amúgy is megszakítja; a bpplay a forrásánál csendesít, a maradékot a leválasztás blokkolja.

## Egy referencia, nem egy versenyző

A bpplay lemond a szép felületről és a kényelmi funkciókról, cserébe egy **referenciát** ad: egy fix pontot, amelyhez mérni lehet. Mivel a fájltól a DAC-ig vezető út ismert, determinisztikus és beavatkozás-mentes, egy új DAC megismerésekor a hallott eredmény jóval tisztábban a DAC műve — a szoftver nem szól bele az ítéletbe. Ez a szerep pontosan egybeér a My Reel Club® gyakorlatával: ismert forrásanyag, kiszámítható lejátszó, a végén az egyetlen vizsgálandó ismeretlen — maga a DAC.

A bpplay a Claude Opus 4.8 nyelvi modell közreműködésével készült; az architektúra (teljes-RAM modell, hog mode, beavatkozás-mentes jelút) és a funkciók a hi-fi elvekből és a gyakorlati tapasztalatból következtek, nem a modelltől. A modell a megvalósításban — a CoreAudio API-ban, a bitre pontos kódban, a hibakeresésben — volt partner.

## Validált állapot

A bpplay DSD256-ig validált, Gustard XMOS és Amanero Combo384/768 interfészeken keresztül (TT és Gustard DAC-okkal). A lánc három csatlakozási topológiában is megmérettetett: a DAC közvetlenül a MacBook Air portjára kötve, egy Sonnet Tech Thunderbolt 5 hubon keresztül, illetve egy no-name USB-hubon át; drága és olcsó kábelezéssel egyaránt. A bithelyes viselkedés mindhárom esetben azonos maradt: a bitút a csatlakozási módtól független. Követelmény: macOS 10.15 vagy újabb, legalább 8 GB RAM.
