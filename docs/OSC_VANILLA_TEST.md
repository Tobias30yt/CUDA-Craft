# Orbital Strike Cannon: Vanilla-Abnahmetest für 1.21.11

Stand 20.11.2026. **Die vollständige Schussfolge gelingt auf Vanilla 1.21.11.**
Auf CUDA Craft bestehen der Originalimport, die beiden Portal-Loader und
separate Verhaltenstests. Der native Zieltreffer ist offen.
Der Nutzer hat nach der Versionsprüfung ausdrücklich Minecraft **1.21.11** und
eine passende **Mk.6/MS** als Ziel gewählt.

## Originalquellen und Versionsauswahl

- Cubicmetres [Mk.6-Erklärung](https://www.youtube.com/watch?v=q78LRgHt_zU)
  verlinkt [seinen Downloadordner](https://www.mediafire.com/folder/p9dugihg28wnr/Orbital_Strike_Cannon_6.0).
  Darin liegt inzwischen **The Orbital Strike Cannon Mk.6.1.litematic**.
- Sein [MS-Update](https://www.youtube.com/watch?v=PI_xXZxV6tY) verlinkt
  [die Minimal-Signature-Dateien](https://www.mediafire.com/folder/ykwaocnz7cslg/Orbital_Strike_Minimal_Signature).
- Die [offizielle Feuerleitung](https://osc-fire-control.cubicmetre.net/) und ihr
  [Quellcode](https://github.com/cubicmetre/osc-fire-control) unterscheiden Mk.6 und MS.
  Für reproduzierbare Referenzen ist der untersuchte Quellstand
  `ebbdea909bd17621ff43e9dd229bdd942c3c5d32` festgehalten. Keine Berechnungswerte
  einer Variante ungeprüft für die andere verwenden.
- Die neuere [Orbital Skybender](https://www.youtube.com/watch?v=PPfJJH0_dBM)
  wurde ebenfalls untersucht. Der [Steuerungsmod](https://github.com/cubicmetre/skybender-anglesnap)
  unterstützt 1.21.11, **die aktuelle Original-Schematic enthält aber Potent Sulfur**.
  Dieser Block wurde laut [Mojang mit 26.2](https://www.minecraft.net/en-us/article/minecraft-java-edition-26-2)
  hinzugefügt. Mod-Kompatibilität beweist deshalb keine Bauwerkskompatibilität.
  Skybender wird nicht stillschweigend in den 1.21.11-Test übersetzt.

Die Dateien wurden aus den vom Autor verlinkten Ordnern heruntergeladen. Sie bleiben
unter `.tools/osc-research/` lokal und werden nicht im Git-Repository oder Release
weiterverteilt. [osc-fixture-audit.json](osc-fixture-audit.json) enthält SHA-256,
Blockzahlen, Datenversionen und Inventar-/Entitätstypen ohne Bauplan-Geometrie.

| Originaldatei | Datenversion | Blöcke ohne Luft | Ergebnis der Dateiprüfung |
|---|---:|---:|---|
| The Orbital Strike Cannon Mk.6.1 | 4556 | 13.501 | Alle verwendeten Blockzustände in 1.21.11 bekannt |
| OSC 6 Control Panel | 4189 | 3.407 | Alle verwendeten Blockzustände bekannt |
| OSC 6 Nether | 4189 | 44 | Alle verwendeten Blockzustände bekannt |
| OSC Minimal Signature | **4671** | **10.637** | Gleiche Datenversion wie 1.21.11; alle Zustände bekannt |
| OSC MS Nether | 4556 | 602 | Alle verwendeten Blockzustände bekannt |
| OSC MS Control Panel, aktuell angeboten | **4903** | 2.918 | Zustände bekannt, aber neuere Datenversion: Rückwärtskompatibilität offen |
| Orbital Skybender 1.0 | 4903 | 9.309 | Neuerer Block `minecraft:potent_sulfur`: inkompatibel |

**Baseline:** zuerst Mk.6.1 mit dem zugehörigen OSC-6-Panel und Nether-Aufbau in
Vanilla 1.21.11 bestätigen. Der Mk.6.1-Referenzschuss ist unten dokumentiert.
**Bevorzugtes erweitertes Ziel:** MS-Hauptkanone und
MS-Nether. Für das neu gespeicherte MS-Panel muss zuvor ein kompletter Vanilla-Lauf
in 1.21.11 gelingen oder eine originale ältere Panel-Version beschafft werden.
Ein bestandener Registry-Check allein reicht nicht für diese Freigabe.

## Import der Mk.6.1-Originaldateien

Der Prüfer setzt jede Datei separat in einer temporären Vanilla-Welt
zusammen und liest die gespeicherten Chunks zurück. Die Teilregionen des
Hauptaufbaus enthalten 807 identische Blocküberschneidungen. Für doppelte
Block-Entitäten werden die verschiedenen lokalen Koordinaten erst auf
dieselbe Weltposition bezogen und dann die Inhalte verglichen.

| Originaldatei | Exakte Blockzustände auf beiden Servern | Geprüfte Vanilla-Block-Entitäten | Vanilla-Fahrzeuge | Wiederhergestellte Vanilla-Ticks |
|---|---:|---:|---:|---:|
| Mk.6.1 Hauptaufbau | 12.694 | 587 | 4 | 4 Block |
| OSC 6 Control Panel | 3.407 | 239 | 1 | 53 Block, 1 Flüssigkeit |
| OSC 6 Nether | 44 | 0 | 0 | 0 |

Auf Vanilla werden Block-Entitäten anhand von Typ, gespeicherten Gegenständen,
Komparatorwert und Hopper-Cooldown geprüft. Beim Fahrzeugvergleich zählen Typ
und Position. Die Ticklisten im Anvil-Save enthalten Position, Typ, Verzögerung
und Priorität. Die Reihenfolge gleicher Ticks wird innerhalb eines Chunks
erhalten. Die Importtests prüfen separate Platzierungen und Save-Rundläufe.
Der spätere Tickvergleich führt die Ticks im verbundenen Aufbau aus.

Auf CUDA Craft bestätigt ein temporärer Save denselben Blockzustand an
jeder belegten Position sowie im Hauptaufbau 316 unterstützte Inventare,
70 aktive Komparatorwerte und vier geplante Komparator-Tick-Verzögerungen.
Im Bedienpanel werden 65 Inventare, fünf Komparatorwerte, 53 geplante Blockticks
und ein Wassertick übernommen. Die Blockticks behalten ihre relative
Einfügereihenfolge und Priorität. Beim erneuten Öffnen stimmen außerdem die
gespeicherten Fahrzeugdaten für vier Fahrzeuge im Hauptaufbau und eines im
Bedienpanel. Weitere Block-Entity-Felder, insbesondere Texte und Komponenten,
bleiben im nativen Import offen.

```powershell
python tools/check-vanilla-litematic.py ".tools/osc-research/The Orbital Strike Cannon Mk.6.1.litematic" --java "C:/Program Files/Java/jdk-23/bin/java.exe"
python tools/check-native-litematic.py ".tools/osc-research/The Orbital Strike Cannon Mk.6.1.litematic" --probe build/cuda/redstone-probe.exe
```

Für einen Lauf mit fest vorgegebenen Eingaben kann man beiden Prüfern mit
`--sha256` den Hash aus [osc-fixture-audit.json](osc-fixture-audit.json) übergeben.

## Tickvergleich des Original-Hauptaufbaus

Der Starttest lädt die fest per SHA-256 gebundene Mk.6.1-Datei auf beiden Servern
in einer lokalen flachen Testwelt mit Seed 0 und Ursprung `0 96 0`. Ein
JDK-Helfer setzt den eingebauten Freeze-Schalter des offiziellen Servers vor
dessen erstem Tick. Die Spielzeitprüfung deckt einen vorzeitig gelaufenen Tick
auf. Hopper-Inventare und Cooldowns werden jeweils direkt aus den laufenden
Block-Entitäten gelesen, da ein Anvil-Save reine Cooldown-Änderungen auslassen
kann.

Ohne Eingabe stimmen an allen 20 Spielticks die Zustände der 12.694 belegten
Originalpositionen und die Hopper-Daten überein. Der optionale
`--button X Y Z`-Lauf verwendet die echte Button-Interaktion beider Engines
an einer Position relativ zur Schematic. Bei Abweichung liefert der Test
einen Fehlercode und schreibt Position, erwartete und beobachtete Daten in
einen lokalen JSON-Trace.

```powershell
python tools/check-mk61-startup.py ".tools/osc-research/The Orbital Strike Cannon Mk.6.1.litematic" --java "C:/Program Files/Java/jdk-23/bin/java.exe" --probe build/cuda/redstone-probe.exe --ticks 20 --output .tools/osc-research/mk61-startup.json
```

Mit `--nether` wird der Hauptaufbau bei `0 128 0` mit den Originalportalen
bei `0 142 0` im Nether verbunden. Die Tests für Nordost
(`--button 15 12 -17`) und Südwest (`--button -17 12 15`) prüfen jeweils
den Start bis zum fertigen Rückweg. Das Boot erscheint in
Tick 4, erreicht den Nether in Tick 5, brennt in Tick 6, kehrt in Tick 8 als
Item zurück und erreicht den Overworld-Hopper in Tick 9. Der Test prüft beide
zufällig gewählten seitlichen Startgeschwindigkeiten innerhalb der Vanilla-Grenzen
und verfolgt die daraus resultierende Bewegung sowie den Portalversatz je Lauf.

Der verknüpfte Aufbau kann mit `--panel` auch das Original-Bedienpanel laden.
Das Panel steht für den Test bei `192 128 0` und erhält einen eigenen
Gegenrahmen über dem Netherdach nach dem Originalmodul. `--grid` liest
Cubicmetres 5×13-Bitmatrix aus einer lokalen JSON-Datei und betätigt die
zugehörigen Originaltasten. `--submit-at` drückt „Input Settings“.
`--remote-at` stellt einen Gegenstand auf die Gewichtsplatte des
Angel-Fernauslösers. Bei `--target` bleiben TNT-Ankunft und Blockänderungen
in der Zielregion verpflichtende Prüfungen.

### Reproduzierbarer Mk.6.1-Referenzschuss

Die Einrichtung startet die beiden Original-Loader, betätigt die Originalhebel
zu festen Spielticks und entfernt die anfänglichen Force-Load-Tickets in Tick
100. Die Portal-Tickets halten die Maschine in Betrieb. Das Ziel bleibt geladen.
Beim durchgehend erzwungenen Laden erhielt die Kanone in den Vergleichsläufen
kein Startsignal. Die drei Tageslichtzustände, die Statuslampe und alle
65 Eingabebits können im Trace geprüft werden.

```powershell
python tools/check-mk61-startup.py ".tools/osc-research/The Orbital Strike Cannon Mk.6.1.litematic" `
  --java "C:/Program Files/Java/jdk-23/bin/java.exe" --probe build/cuda/redstone-probe.exe `
  --nether ".tools/osc-research/OSC 6 Nether.litematic" --panel ".tools/osc-research/OSC 6 Control Panel.litematic" `
  --button 15 12 -17 --button -17 12 15 --control-at 1 205 132 17 `
  --control-at 35 18 141 -14 --control-at 35 -14 141 18 --control-at 50 7 141 -22 --control-at 50 3 143 -24 `
  --portal-only-at 100 --grid tests/fixtures/mk61-target-80-64-32.json --grid-at 400 --submit-at 460 `
  --watch 6 141 -24 --watch 6 141 -23 --watch 197 135 -1 --watch 200 135 2 `
  --target 80 64 32 --sample-every 20 --ticks 2100 --output .tools/osc-research/mk61-shot.json
```

Die Matrix gilt für den Ursprung `0 128 0`, Ziel `80 64 32`, Passcode `940`,
Nuke-Größe `1` und Magazinschacht `0`. Im ersten erfolgreichen Referenzlauf
erschien TNT im Hauptaufbau in der Stichprobe bei Tick 760 und am Ziel bei
Tick 1500. In der Zielregion waren am Ende 72 Blockzustände verändert. Die
Referenzprüfung bleibt ein Fehler, bis CUDA Craft TNT am selben Ziel erzeugt
und der Vergleich der Blockänderungen besteht.

`--reference-cache <Datei.json.gz>` zeichnet die vollständigen Referenzdaten
auf und kann dieselben Vanilla-Frames nach einem nativen Neubau wiederverwenden.
Der Test bindet den Cache an die Originaldateien, Eingaben, Beobachtungspositionen
und lokalen Referenzwerkzeuge. Ein Lauf ohne Cache zeichnet eine neue Referenz
auf. Wiederholte Vanilla-Läufe trafen das Ziel mit unterschiedlicher Kraterform.

## Was implementiert werden muss

Diese Liste kombiniert gemessene Schematic-Inhalte mit einer Prüfung des aktuellen
CUDA-Craft-Codes. Wo eine spezielle Schaltungsabhängigkeit noch nicht durch einen
Vanilla-Trace belegt ist, steht sie als zu prüfende Anforderung, nicht als Befund.

| Reihenfolge | Arbeit | Konkreter Nachweis |
|---|---|---|
| 1 | Vollständiger Fixture-Import: Zustände, Block-Entity-NBT, Inventarslots, Comparator-Ausgänge, Entitäten, Passagiere, anstehende Ticks und Regionsursprung | Nach Import identische Inventare und Zustände; keine still übersprungenen Daten. Der bestehende `/place template`-Pfad überspringt Block-Entity-Daten und Entitäten und eignet sich dafür noch nicht. |
| 2 | Vanilla-Reihenfolge für Nachbarupdates, Blockevents und geplante Ticks; Tickpriorität und Einfügereihenfolge | Derselbe Eingabepuls erzeugt denselben geordneten Trace. Der aktuelle globale `Simulation::redstone()`-Durchlauf und `map<BlockPos, Event>` bilden das noch nicht ab. |
| 3 | Exakte Redstone-Leitung und Geräte: gerichtete starke/schwache Versorgung, Repeater-Locking und Kurzpulse, Torch-Burnout, Observer-Pulse, Comparator-Modi und Kupferlampen-Flanken | Kleine Referenzschaltungen einschließlich Änderungen im selben Tick bestehen vor dem Gesamttest. MS enthält 724 Staubblöcke, 522 Repeater, 678 Observer, 257 Comparatoren und 80 gewachste Kupferlampen. |
| 4 | Kolben/Sticky Pistons: Push-Limit, Schleim/Honig, Blockevents, bewegte Blockzustände, Quasi-Connectivity, kurze Pulse, Update-Reihenfolge | Normale/sticky Kolben, BUD/QC und ein einzelnes originales Duper-Modul verhalten sich wie 1.21.11. MS enthält 227 normale und 235 klebrige Kolben, 138 Schleim- und 14 Honigblöcke. |
| 5 | Hopper, Dropper, Dispenser, Item-Entitäten, Auswahl/Cooldowns und Analogsignale | Gleiche Itemzahl und gleiche Ausgabefolge über viele Zyklen, auch nach Neustart. MS enthält 111 Dropper und 63 Hopper; deren gespeicherte Inventare gehören zur Schaltung. |
| 6 | Rails, Minecarts und Nicht-Vollwürfel-Kollisionen | Activator-/Powered-/Detector-Rails, Minecart-Aufnahme und -Bewegung sowie exakte Kollisionsformen testen. Die MS-Hauptkanone enthält 842 Activator-Rails; Mk.6.1 enthält gespeicherte Hopper-/Chest-Minecarts. |
| 7 | TNT als eigene Entität: Zündung/Duping, Fuse, Reihenfolge, Gravitation, Drag, Explosionsexposition, Widerstand und Impulsaddition | Duper erzeugt die Referenzzahl mit identischen Fuses; Einzelbeschleuniger trifft dieselben Positionen/Geschwindigkeiten. Nicht als generisches Mob-Profil behandeln. |
| 8 | Flüssigkeits-/Blockeinflüsse auf TNT und Entitäten: Wasser, Bubble Columns, Powder Snow, Slime/Honey und die verwendeten Kollisionsformen | Bewegungs-Traces je Komponente vergleichen. Der originale Mk.6-Leitfaden nennt ausdrücklich das Kollisionsproblem [MC-260743](https://bugs.mojang.com/browse/MC/issues/MC-260743); den tatsächlichen 1.21.11-Pfad prüfen, keinen fremden Fix ungeprüft übernehmen. |
| 9 | Chunk-Tickets und Lebenszyklus: block-ticking/entity-ticking/unloaded, nicht tickende TNT, Portal-Entity-Transfer und Nether-Loader | Laden, Beschleunigen, Transfer und Wiederaufnahme außerhalb der Spielersicht mit Vanilla vergleichen. Aktuelle Spieler-Portale und geladener Terrain-Cache reichen nicht. |
| 10 | Originale Fernsteuerung und Speicher: Panel-Eingaben, Passcode, Quittierung, Zielregister, Reset | Den tatsächlichen Kommunikationspfad aus dem Originalaufbau isolieren und messen. Keine direkte Backend-Abkürzung, die aus einem Ziel sofort TNT erzeugt. Bei Mk.6 außerdem Daylight Detector/Jukebox und ihre Analogdaten prüfen. |
| 11 | Welt-/Entity-Grenzen und deterministische Lastverarbeitung | Native Grenze ±1024 und 4096-Entity-Limit dürfen Fernschüsse nicht abschneiden. Raumindex und begrenzte Arbeit pro Tick dürfen die logische Update-Reihenfolge nicht verändern. GPU-Batching erst nach passenden CPU-Traces. |
| 12 | Kompletter Schuss und Wiederholbarkeit | Positives/negatives X/Z, Nah-/Fernziel, Nuke/Stab, mehrere Schüsse, Restart und Chunk-Unload vergleichen; Details unten. |

## Messbarer Gesamttest

1. Original-1.21.11-Referenz und nativen Server getrennt starten. Gleiche Welt,
   Bauposition, Tageszeit, Wetter, Schwierigkeit, Simulationseinstellungen und
   Initialzustände verwenden. Hilfsmods und jede nicht standardmäßige Regel mit
   Version und Hash protokollieren; nicht nur den Minecraft-Versionsnamen notieren.
2. Ganze Fixture einschließlich Nether-Seite importieren. Vor dem Start Block- und
   Inventarzustände vergleichen. Fehlerhafte Palette, fehlende NBT-Felder oder
   nicht unterstützte Entitäten brechen die Fixture-Vorbereitung ab.
3. Eingaben aus Cubicmetres zur Variante passender Feuerleitung verwenden. Deren
   [Validierung](https://github.com/cubicmetre/osc-fire-control/blob/ebbdea909bd17621ff43e9dd229bdd942c3c5d32/src/utils/validation.ts)
   verlangt bei Mk.6/MS einen in X/Y/Z durch 16 teilbaren Ursprung, gültigen Passcode,
   mindestens 64 Blöcke horizontale Entfernung und begrenzte Beschleunigerzähler.
   Zunächst kleine gültige Ladung; anschließend Grenzwerte und Serienbetrieb.
4. Pro Tick und Update-Schritt Blockänderungen, geplante Ticks, Kolbenevents,
   Inventaränderungen, TNT-Erzeugung/Fuse/Position/Geschwindigkeit, Portaltransfer
   und Chunk-Ticketzustände aufzeichnen. Identitäten zwischen Prozessen über
   Erzeugungsreihenfolge zuordnen, nicht über zufällige UUIDs.
5. **Bestanden** erst bei gleichen diskreten Ereignissen/Zählwerten, gleicher
   TNT-Zahl, gleichem Ziel- und Einschlagszeitpunkt sowie reproduzierbarem
   Explosions-/Blockergebnis. Numerische Toleranzen vor dem Lauf anhand der
   Vanilla-Datentypen festlegen; nicht nachträglich so erweitern, dass Abweichungen
   verschwinden. Nach Fehlschuss, Reset und Neustart darf kein alter Auftrag
   unerwartet feuern und kein Inventarinhalt dupliziert werden.
6. TPS/MSPT, Peak-RAM und aktive Entitäten zusätzlich messen. Performance ist ein
   separates Kriterium; ein schneller falscher Treffer besteht den Vanilla-Test nicht.

Der derzeitige Stand ist **Dateiaudit und automatisierte Redstone-Teilabnahme bestanden**.
Ein Observer an der Chunkgrenze zeigte auf dem offiziellen 1.21.11-Server
nach einer Änderung vor seiner Vorderseite über vier Spielticks die Folge
`aus, an, an, aus`. Die hintere Lampe leuchtete beim Einschalten, die seitliche
blieb aus. Dieselbe Schaltung ist im nativen Simulationstest abgedeckt. Dort
werden zusätzlich zusammengefasste Änderungen und Neustarts während des Impulses
geprüft. Der Referenzlauf lässt sich mit
`python tools/check-vanilla-observer.py --java PATH` wiederholen.

Die gewachste Kupferlampe schaltet auf der ersten steigenden Flanke ein, bleibt
nach dem Signalverlust an und schaltet auf der nächsten steigenden Flanke aus.
Der native Test prüft alle acht Varianten sowie die Kopplung an einen Observer.
Für den Vanilla-Lauf `--copper-bulb` an den Referenzbefehl anhängen. Er erfasst
zusätzlich den Komparator-Ausgang: 15 bei leuchtender und 0 bei dunkler Lampe.
Der native Komparator liefert diese Werte nach zwei Ticks. Lichtänderungen von
Kupferlampen werden auch an Clients mit bereits geladenen Chunks gesendet.

## Automatisierte Teilabnahme

Die [gemeinsamen Schaltungen](../tests/redstone_fixtures.py) verwenden auf beiden
Servern dieselben Blocknamen, Eigenschaften, Eingaben und Sollwerte. Für den
Vanilla-Lauf wird nach jedem Schritt die Spielzeit abgefragt. Dadurch wird eine
Messung nach genau zwei Ticks von einem Test nach bloßer Wartezeit unterschieden.
Der native Lauf nutzt die tatsächliche Weltsimulation und meldet für jede
Abweichung die erwartete Eigenschaft und den gefundenen Blockzustand.

| Testgruppe | Vanilla 1.21.11 | CUDA Craft |
|---|---:|---:|
| Observer und acht Kupferlampenvarianten | 9/9 | 9/9 |
| Komparator mit Lampe, Analogwert und beiden Modi | 5/5 | 5/5 |
| Fassinventar durch Vollblock, Hopper- und Dropper-Transfer | 3/3 | 3/3 |
| Antriebs-, Aktivierungs- und Sensorschienen, Truhenlore und Dispenser | 6/6 | 6/6 |
| TNT-Zündzeit, freie Bewegung, Explosion, auf einer Stufe und am bewegten Schleimblock | 3/3 | 3/3 |
| Tageslichtsensor, Notenblock und Eisenfalltür | 2/2 | 2/2 |
| Kolben, klebriger Kolben, Zwischenzustände, 12/13 Blöcke, Schleim/Honig und Quasi-Connectivity | 9/9 | 9/9 |
| Kurzer Repeater-Puls bei vier Ticks Verzögerung | 1/1 | 1/1 |

Beim Grenzfall mit 13 Blöcken muss derselbe Kolben anschließend die verkürzte
Reihe bewegen. So fällt ein unbeweglicher Kolben auch in diesem Test durch.

Der [Licht-Netzwerktest](../tests/bulb_light_network_test.py) liest die
Blocklichtdaten aus den Spielpaketen. Beim Laden des Chunks sind die Werte
15, 12, 8 und 4 samt Abschwächung im Nachbarblock korrekt. Der optionale
Live-Lauf prüft Ein- und Ausschalten im bereits geladenen Chunk unabhängig
voneinander. Der Client erhält jeweils den neuen Blockzustand und ein Lichtpaket,
das den Wert über der Lampe von 0 auf 14 oder von 14 auf 0 ändert.

Nach dem nativen Build führt CTest alle 38 Schaltungen und alle drei Lichtprüfungen
regulär aus. Der folgende Befehl wiederholt auch den Vanilla-Vergleich:

```powershell
python tests/redstone_fixtures.py build/cuda/redstone-probe.exe --all --java "C:/Program Files/Java/jdk-23/bin/java.exe"
python tests/bulb_light_network_test.py build/cuda/cuda-craft.exe build/cuda/login-test-server.exe --live
```

Schienenkurven, TNT-Duplizierung, die Explosionsexposition, der Chunk-Lebenszyklus
und die komplette originale Mk.6.1/MS-Schussfolge stehen noch aus.
Kolben setzen bewegte Zwischenblöcke und prüfen das Ende der Bewegung nach
zwei weiteren Ticks. Die Tests erfassen einen TNT-Schub durch bewegten Schleim.
Die genaue Reihenfolge komplexer Kolbenkreise und der Original-Duper stehen
noch aus.
Komparatoren lesen in der Teilabnahme Kupferlampen, Komposter und Inventare.
Weitere analoge Quellen und der Zufallszustand der Dropper sind offen. Licht wird innerhalb
eines Chunks berechnet. Die Ausbreitung über Chunkgrenzen bleibt offen.

Noch wurde weder eine vollständige Vanilla-Referenzschussfolge aufgezeichnet noch
die Kanone im nativen Backend ausgeführt. Sie darf daher nicht als Beleg für
Release-Reife oder allgemeine Vanilla-Parität genannt werden.

## Reproduzieren

Nach lokalem Download aus den Originalordnern:

```powershell
python tools/audit-litematic.py --require-compatible --output .tools/osc-research/mk6-baseline.json ".tools/osc-research/The Orbital Strike Cannon Mk.6.1.litematic" ".tools/osc-research/OSC 6 Control Panel.litematic" ".tools/osc-research/OSC 6 Nether.litematic"
python tests/litematic_audit_test.py
```

`--require-compatible` liefert einen Fehler bei
neuerer Datenversion oder unbekannten Zuständen. Es behauptet keine Gameplay-Parität.
Die kommenden Implementierungen und ihre Abnahme stehen auch in
[implementation-backlog.json](implementation-backlog.json), B056–B059.
