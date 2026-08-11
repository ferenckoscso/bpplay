bpplay 0.9.1 — Olvasd el, mielőtt elindítod
============================================

A lemezképben két dolog van: a "bpplay" parancssori program, és a
"bpplay drop" app — ez utóbbi ikonjára zenefájlokat vagy egy
albummappát húzva, Terminal-parancsok nélkül is elindul a lejátszás.

1. lépés — Másold mindkettőt egy állandó helyre
--------------------------------------------------
Húzd a "bpplay" fájlt ÉS a "bpplay drop" appot is (mindkettő ebben a
lemezképben van) pl. az Asztalra (Desktop), vagy egy általad
választott mappába. Tartsd őket egymás mellett — a "bpplay drop" a
saját, belül tárolt bpplay-jét használja, de kényelmesebb, ha a nyers
"bpplay" is elérhető marad Terminálból való, kézi használatra.

2. lépés — Az első indítás (Gatekeeper-figyelmeztetés)
--------------------------------------------------------
Sem a "bpplay", sem a "bpplay drop" NINCS Apple által hitelesítve
(notarizálva), ezért macOS első indításkor mindkettőnél figyelmeztetni
fog, hogy "ismeretlen fejlesztőtől" származik. Ezek a programok
biztonságosak, csak az Apple hitelesítési folyamatán nem mentek még
keresztül. Kétféleképpen kerülheted meg (mindkét fájlnál külön-külön,
csak egyszer kell megtenni):

  A) Terminálból (a "bpplay" fájlra, egyszerűbb):
     xattr -d com.apple.quarantine ~/Desktop/bpplay

  B) Finderből (mindkettőre működik, "bpplay drop"-nál ez az egyetlen mód):
     Jobb klikk a fájlon/appon → "Megnyitás" → a felugró ablakban
     újra "Megnyitás".

3. lépés — Lejátszás húzással (nem kell Terminal)
-----------------------------------------------------
Húzz rá egy zenefájlt, több fájlt, vagy egy albummappát a "bpplay
drop" ikonjára. Egy Terminal-ablak nyílik, elindul a bit-perfect
lejátszás, és a billentyűparancsok is elérhetők benne:
  n = következő track, b = előző, space = szünet/folytatás, q = kilépés
A rendszer alapértelmezett hangkimenetére játszik (a macOS Rendszer-
beállítások → Hang menüjében állítható át, ha egy adott DAC-ot
szeretnél kiválasztani).

4. lépés (haladóknak) — Alap használat közvetlenül Terminálból
--------------------------------------------------------------------
Ha egy adott DAC-ot/kimeneti eszközt akarsz `-d` kapcsolóval kiválasztani
(nem a rendszer-alapértelmezettet), vagy szkriptből hívnád:

  cd ~/Desktop
  ./bpplay -l                      # kilistázza az elérhető hangkártyákat
  ./bpplay -d 1 zeneszam.flac      # lejátszás az 1-es eszközön

Részletes leírás, formátumok, hibaelhárítás: lásd a projekt README-jét:
https://github.com/ferenckoscso/bpplay

Licenc: GPL-3.0-or-later (lásd a mellékelt LICENSE fájlt).
