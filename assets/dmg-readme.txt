bpplay 0.9.6 — Olvasd el, mielőtt elindítod
============================================

A lemezképben három dolog van:
  - "bpplay" — a parancssori program
  - "bpplay drop" — egy drag-and-drop app
  - "bpplay play.workflow" — egy Finder jobbklikk-menü ("Quick Action")

Ha korábbi bpplay-verziót frissítesz
---------------------------------------
Van egy negyedik fájl is a lemezképben: "Régi verzió eltávolítása.command".
Ha volt már bpplay a gépeden, futtasd ezt ELŐSZÖR (dupla kattintás) — a
szokásos helyeken megkeresi a régi "bpplay"/"bpplay drop"/"bpplay
play.workflow" példányokat, és rákérdezve a Kukába helyezi őket. Utána
következhet az 1. lépés az új verzióval. Ha most telepíted először,
kihagyhatod ezt a lépést. (Ez a fájl sincs notarizálva, ugyanaz a
jobbklikk→"Megnyitás" trükk kell hozzá első futtatáskor, mint a 2.
lépésben — a dupla kattintás önmagában figyelmeztetést adhat.)

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
  - Ha a "Services" mappa nem is létezik (ez normális egy vadonatúj
    felhasználói fiókban, ahol még sosem volt Quick Action/Service):
    nyiss egy Terminált, és futtasd: `mkdir -p ~/Library/Services`
    — utána már behúzhatod bele a "bpplay play.workflow"-t.

2. lépés — Az első indítás (Gatekeeper-figyelmeztetés)
--------------------------------------------------------
Egyik sincs Apple által hitelesítve (notarizálva), ezért macOS első
használatkor mindháromnál figyelmeztetni fog, hogy "ismeretlen
fejlesztőtől" származik. Mindhárom biztonságos, csak az Apple
hitelesítési folyamatán nem mentek még keresztül. Kerüld meg
elemenként, egyszer:

  A) Terminálból (AJÁNLOTT — mindhárom elemre működik, és nem törik
     el, ha Apple megint átalakítja a figyelmeztető ablakot):
     xattr -dr com.apple.quarantine ~/Desktop/bpplay
     xattr -dr com.apple.quarantine "$HOME/Desktop/bpplay drop.app"
     xattr -dr com.apple.quarantine "$HOME/Library/Services/bpplay play.workflow"
     (Ha máshova tetted őket, cseréld az útvonalat a sajátodra.)

  B) Rendszerbeállításokból (ha nem szeretnél Terminált használni):
     Próbáld megnyitni az adott elemet (dupla kattintás, a Quick
     Actionnél a jobbklikk-menüből kiválasztva) — ez egy "Nem
     nyitható meg" hibaüzenetet ad, ezt zárd be ("Kész"). Utána:
     Rendszerbeállítások → Adatvédelem és biztonság → görgess le →
     ott megjelenik egy "Megnyitás mégis" gomb az adott elem mellett.
     Próbáld meg utána újra megnyitni — egy második megerősítést
     kérhet.
     (A régebbi "jobbklikk → Megnyitás → Megnyitás" kétlépéses trükk
     az újabb macOS-verziókon — Sequoia és utána — már NEM működik:
     a felugró ablakban nincs benne "Megnyitás mégis" gomb. Ha ezt
     tapasztalod, az A) vagy a fenti B) módszerre van szükség.)

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
