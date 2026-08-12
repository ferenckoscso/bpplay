bpplay 0.9.5 — Olvasd el, mielőtt elindítod
============================================

A lemezképben három dolog van:
  - "bpplay" — a parancssori program
  - "bpplay drop" — egy drag-and-drop app
  - "bpplay play.workflow" — egy Finder jobbklikk-menü ("Quick Action")

1. lépés — Másold mindet egy állandó helyre
------------------------------------------------
A "bpplay" és a "bpplay drop" bárhova mehet, pl. az Asztalra — húzd
oda mindkettőt. A "bpplay play.workflow" más: hogy megjelenjen a
Finder jobbklikk-menüjében, egy konkrét rendszermappába kell
másolni: `~/Library/Services/`. Ez a mappa alapból rejtett:
  - Finderben tartsd lenyomva az Option billentyűt, kattints a "Go"
    (Ugrás) menüre → megjelenik a "Library" (Könyvtár) → nyisd meg →
    nyisd meg a "Services" mappát → húzd bele a "bpplay play.workflow"-t.
  - Vagy Finderben: Cmd+Shift+G, majd írd be: ~/Library/Services

2. lépés — Az első indítás (Gatekeeper-figyelmeztetés)
--------------------------------------------------------
Egyik sincs Apple által hitelesítve (notarizálva), ezért macOS első
használatkor mindháromnál figyelmeztetni fog, hogy "ismeretlen
fejlesztőtől" származik. Mindhárom biztonságos, csak az Apple
hitelesítési folyamatán nem mentek még keresztül. Kerüld meg
elemenként, egyszer:

  A) Terminálból (a "bpplay" fájlra, egyszerűbb):
     xattr -d com.apple.quarantine ~/Desktop/bpplay

  B) Finderből (mindháromra működik, a "bpplay drop"-nál és a
     "bpplay play.workflow"-nál ez az egyetlen mód):
     Jobb klikk a fájlon/appon → "Megnyitás" → a felugró ablakban
     újra "Megnyitás". A Quick Action-nél ezt MÉG A `~/Library/
     Services/`-en belül, magán a "bpplay play.workflow"-n végezd el,
     MIELŐTT a jobbklikk-menüből használnád.

3. lépés — Lejátszás húzással (nem kell Terminal)
-----------------------------------------------------
Húzz rá egy zenefájlt, több fájlt, vagy egy albummappát a "bpplay
drop" ikonjára. Egy Terminal-ablak nyílik, elindul a bit-perfect
lejátszás, és a billentyűparancsok is elérhetők benne:
  n = következő track, b = előző, space = szünet/folytatás, q = kilépés

4. lépés — Lejátszás jobbklikkel (nincs ikon, amire húzni kellene)
--------------------------------------------------------------------
Ha a "bpplay play.workflow" telepítve van (1. lépés), jelölj ki egy
vagy több zenefájlt, vagy egy albummappát bárhol a Finderben, kattints
jobb gombbal, és válaszd a "bpplay play"-t (macOS-verziótól függően
lehet, hogy a "Quick Actions" almenüben van). Ugyanaz az eredmény,
mint a 3. lépésnél — Terminal-ablak nyílik, indul a lejátszás.

Mind a 3., mind a 4. lépés a rendszer alapértelmezett hangkimenetére
játszik (a macOS Rendszerbeállítások → Hang menüjében állítható át,
ha egy adott DAC-ot szeretnél kiválasztani).

5. lépés (haladóknak) — Alap használat közvetlenül Terminálból
--------------------------------------------------------------------
Ha egy adott DAC-ot/kimeneti eszközt akarsz `-d` kapcsolóval kiválasztani
(nem a rendszer-alapértelmezettet), vagy szkriptből hívnád:

  cd ~/Desktop
  ./bpplay -l                      # kilistázza az elérhető hangkártyákat
  ./bpplay -d 1 zeneszam.flac      # lejátszás az 1-es eszközön

Részletes leírás, formátumok, hibaelhárítás: lásd a projekt README-jét:
https://github.com/ferenckoscso/bpplay

Licenc: GPL-3.0-or-later (lásd a mellékelt LICENSE fájlt).
