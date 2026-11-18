# CUDA Craft

Eigenstaendiger Minecraft-Java-Server in C++20 und NVIDIA CUDA.
Zielversion: **Minecraft Java 1.21.11, Protokoll 774**.

Modrinth App: [Paket importieren oder manuell installieren](docs/MODRINTH.md).

**Stand 0.11.3: Inventar-Drag, Doppelklick und Offhand-Tausch.**
Stacks lassen sich mit linker/rechter Maustaste verteilen und per Doppelklick
einsammeln. Offhand-Tausch funktioniert in Menues und mit F im Spiel.
[Inventarverhalten und Grenzen](docs/INVENTORY.md).
Netherreisen suchen jetzt intakte Zielportale und erzeugen bei Bedarf einen
neuen Rahmen mit freiem Ankunftsbereich. [Details und Grenzen](docs/PORTALS.md).
Beschaedigte Netherportale werden jetzt deaktiviert; optionale Rahmenecken
bleiben erlaubt. [Naechste Implementierungen und Pruefziele](docs/TODO.md).
Nether und End besitzen getrennte Saves und eigene Chunk-Uebertragung. Portale,
Fallen von Sand/Kies, Betonpulver, Ackerbau, Knochenmehl, Knoepfe und Druckplatten
haben erste Implementierungen. [Umfang und Grenzen](docs/EXPANSION_0_11.md),
[Mob-Unterstuetzung](docs/MOBS.md). Dies erreicht noch keine 90 % vollstaendigen
Vanilla-Blockmechaniken. Die Dimensionsterraine und Mob-KI sind Prototypen.

Seit 0.10: gespeicherte Truhen, Doppeltruhen, Faesser und funktionierende Oefen.
Container unterstuetzen gemeinsame Inventare, Shift-Transfer und Hotbar-Tausch.
Ofen, Schmelzofen und Raeucherofen verarbeiten die originalen 1.21.11-Rezepte;
Inhalte und Brenn-/Garfortschritt bleiben beim Neustart erhalten.
Siehe [Container und Oefen](docs/CONTAINERS.md).
Schweine und Zombies erhalten jetzt bei ueberlebten Nahkampftreffern Rueckstoss.
**`Start CUDA Client.cmd`** startet den Fabric-1.21.11-Client. Im Titelmenue
**CUDA Singleplayer** oeffnen, Welt erstellen und spielen. Der Client startet,
pausiert und speichert seinen eigenen nativen Server. Details und Grenzen stehen
in [Fabric-Singleplayer](docs/FABRIC_CLIENT.md).
Vorhanden sind ausserdem `/perf`, `/monitor`, gemessene TPS/CPU/RAM/Netzwerkwerte sowie
Verwaltungsbefehle wie `op`, `tp`, `give`, `kick`, `save-all` und `stop`.
Die Spielbasis bietet fliessendes Wasser, Hunger/Schaden, begrenztes Crafting, Item-Drops,
Schweine/Zombies sowie Hebel, Staub, Fackeln, Repeater und Lampen.
Spieler, Inventare und laufende Simulation werden gespeichert. Dies ist noch keine vollstaendige
Vanilla-Engine. Die neue Oberwelt verwendet die 1.21.11-Dichtefunktionen,
Oberflaechenregeln, Aquifere, grosse Erzadern und Hoehlen-/Schlucht-Carver.
Die Klimabiome und der SHA-256-Biom-Zoom stammen ebenfalls aus nativen Implementierungen.
64 kontrollierte Referenzchunks mit **6.291.456 Blockzustaenden** stimmen bis zur
jeweils geprueften Generierungsstufe exakt mit dem offiziellen Server ueberein.

**Die komplette Welt ist weiterhin nicht Vanilla-identisch.** Baeume, Gras und
Blumen sind noch angenaehert. Automatische Bauwerksgenerierung, platzierte Erzvorkommen und weitere Features
fehlen. 1.202 originale Strukturvorlagen lassen sich manuell mit `/place template` setzen;
Zusammensetzung, Block-Entity-Daten und Loot fehlen noch. Laufende Wasserticks sind implementiert; die exakte Worldgen-Nachbearbeitung
ist weiterhin offen. Zwei entsprechende Abweichungen bleiben als separate Referenz erhalten.
Der neue praezise Generator laeuft derzeit auf der CPU. CUDA bleibt eingebunden
und erzeugt das Grundterrain der bisherigen Generatoren.

## Spielen auf diesem Rechner

1. **`Start CUDA Craft.cmd` doppelklicken** und das Serverfenster offen lassen.
2. Minecraft **Java 1.21.11** mit einem angemeldeten Minecraft-Konto starten.
3. Mehrspieler → Direkt verbinden → **`127.0.0.1:25565`**.
4. Neue Spieler starten in **Survival** mit leerem Inventar. Holz abbauen, im
   Inventar Planken und eine Werkbank craften. Linksklick abbauen, Rechtsklick bauen.
   Im Serverfenster nach dem ersten Beitritt `op DeinName` eingeben.
   Als OP aktiviert `/gamemode creative` das Kreativmenue und Fliegen;
   `/gamemode survival` schaltet zurueck. Gespeicherte Spieler behalten ihren Modus.
5. `/monitor on` zeigt Live-Messwerte im Spiel; `/perf` liefert Details.
   `monitor on` im Serverfenster aktiviert die Konsolenausgabe.
   `/spawn` bringt dich zurueck; `/help` zeigt die verfuegbaren Befehle.
   Mit `stop` oder Strg+C im Serverfenster speichern und beenden.

Der normale Starter verwendet die neue Oberwelt in **`world-density`** mit
Standardseed **20261113**. **`Start Klimawelt 0.5.cmd`** oeffnet die bisherige
Klimawelt in **`world-climate`**. **`Start Oberwelt 0.4.cmd`** oeffnet die bisherige
experimentelle Welt in **`world-overworld`**. **`Start Flachwelt.cmd`** oeffnet
die flache Welt in **`world`**. Alle vier Generatoren bleiben verfuegbar;
alte Bauten werden nicht in anderes Terrain umgewandelt.

Alle 1.166 Blocktypen mit 29.671 Zustaenden sind registriert; registrierte Blockitems
lassen sich mit einem Grundzustand platzieren. 1.505 Items werden als einfache Stapel
akzeptiert. Spezielle Blockfunktionen und Item-Komponenten sind weiterhin nur teilweise
implementiert; das ist keine vollstaendige Vanilla-Paritaet. Der Spielbereich ist 2048 × 2048 Bloecke gross,
die Hoehe reicht von -64 bis 319. Pro Spieler werden 49 Chunks geladen.

Aenderungen landen etwa jede Sekunde in **`world-density/blocks.json`**
(alte Oberwelt: `world-overworld/blocks.json`, Flachwelt: `world/blocks.json`), maximal 100.000
abweichende Blockpositionen. Diese Datei beim Sichern zusammen mit dem Projekt
aufbewahren; OP-Rechte liegen daneben in **`operators.json`** und gehoeren ins Backup.
Das ist ein eigenes Speicherformat, **keine Anvil-Welt**. Nicht zwei
Server gleichzeitig mit demselben Weltordner starten. Survival-Position, Inventar,
Werkzeugverschleiss, Gesundheit, Hunger, Modus, Mobs und ausstehende Wasserticks
werden gespeichert. Kreativ-Spieler starten weiterhin am Spawn.
Generatorversion und Seed werden in der Weltdatei gespeichert. Ein abweichender
expliziter Seed/Generator wird abgelehnt. Fuer andere Seeds einen neuen Ordner angeben.
Alte Weltdateien im Format 1 werden weiterhin als Flachwelt geladen.

Der Starter verwendet **`build/cuda/cuda-craft.exe`**, nicht die alte CPU-EXE unter
`build/windows/Release`. CUDA 12.8.1 liegt projektlokal in `.tools/cuda`; die Runtime
ist statisch eingebunden. Der Kerneltest besteht auf der RTX 4060.

```powershell
& '.\Start CUDA Craft.cmd'
# Fuer andere Rechner im eigenen Netzwerk:
& '.\Start CUDA Craft.cmd' --bind 0.0.0.0 --port 25565
# Neue Landschaft mit eigenem numerischen Seed:
& '.\Start CUDA Craft.cmd' --world 'meine-welt' --generator density --seed 12345
# Bisherige Klimawelt aus Version 0.5:
& '.\Start Klimawelt 0.5.cmd'
# Experimentelle Oberwelt aus Version 0.4:
& '.\Start Oberwelt 0.4.cmd'
# Bisherige Flachwelt:
& '.\Start Flachwelt.cmd'
& '.\build\cuda\cuda-craft.exe' --gpu-self-test
& '.\build\cuda\cuda-craft.exe' --help
```

Standardmaessig lauscht der Server nur lokal. Fuer LAN-Verbindungen die lokale
IPv4-Adresse dieses Rechners verwenden und den Port in der Windows-Firewall erlauben.
`--port 0` waehlt einen freien Port, `--run-for-ms` begrenzt die Laufzeit.

## Build reproduzieren (Windows)

Voraussetzungen: CMake >= 3.25, Ninja, Visual Studio 2022 Build Tools mit C++-Workload
und Windows SDK, Python >= 3.10 sowie cryptography fuer Login-Tests.

```powershell
# Nur fuer eine frische Einrichtung:
& '.\tools\install-cuda.ps1'
py -3 -m pip install -r tests/requirements.txt
# Aktiviert MSVC, baut und testet:
& '.\tools\build-cuda.cmd'
# Optional: fixierte Registry-/Blockdaten neu erzeugen:
& '.\tools\fetch-game-data.ps1'
py -3 tools/generate-game-data.py
py -3 tools/generate-simulation-data.py
```

Der lokale CUDA-Preset verwendet Architektur 89 fuer die RTX 4060. Fuer andere GPUs
CMAKE_CUDA_ARCHITECTURES anpassen. Die Installation aendert weder System-PATH noch
Treiber. Offizielle NVIDIA-Pakete und zlib 1.4 werden per SHA-256 geprueft.
Die generierten Spieldaten stammen aus dem unabhaengigen PrismarineJS-Projekt;
Commit und Pruefsummen sind fixiert. Hinweise stehen unter `third_party`.
Die native Cubiomes-Bibliothek liegt mit MIT-Lizenz unter `third_party/cubiomes`.
`tools/fetch-cubiomes.ps1` prueft/reproduziert die fest gepinnten Quelldateien.
Die native Density-/Surface-/Carver-Bibliothek liegt unter `third_party/hyperchunk`
(MIT). `tools/fetch-hyperchunk.ps1` reproduziert die fixierten Dateien samt den
1.21.11-Anpassungen. `tools/generate-density-data.py` bettet die verifizierten
1.21.11-Konfigurationsdaten ein; der generierte Header ist bereits enthalten.
Die offizielle Java-Testreferenz gehoert nur zum optionalen Vergleichswerkzeug;
CUDA Craft benoetigt sie im Betrieb nicht und fuehrt sie nicht aus.

`windows-cuda` setzt ein systemweit installiertes Toolkit samt VS-Integration voraus.
CUDACRAFT_CUDA=ON verlangt CUDA, OFF deaktiviert es, AUTO erlaubt CPU-Terrain.
Online-Login ist wegen Windows CNG/WinHTTP derzeit nur unter Windows verfuegbar.

## Tests und aktueller Umfang

Dreizehn CTest-Suiten pruefen Protokoll, Netzwerk, Kryptografie, Login, Gameplay,
Simulation, Konsole/OP/Performance, Terrain, Vanilla-Block- und Biomreferenzen und den
CUDA-Kernel. Spieltests decken Chunk-Dekodierung, Bewegung, Zwei-Spieler-Bauaenderungen,
Inventar, Spawn nach Umbauten, Speichern/Neustart, alte Weltdateien und laengere Verbindungen ab.
Die Terraingenerierung wird fuer positive/negative Koordinaten und Seeds zwischen
CPU und CUDA verglichen. Chunk-Reihenfolge aendert das Ergebnis nicht. Biomtests
verwenden erfasste Vanilla-Daten; ein JVM-Start ist fuer normale CTest-Laeufe unnoetig.

Der unveraenderte grafische 1.21.11-Client hat die Welt ueber die separate lokale
Test-EXE betreten und Chunks verarbeitet, auch mit der neuen Oberwelt. Der Nutzer
hat den bisherigen spielbaren Stand bestaetigt. Der normale Netzwerkserver verwendet
die offizielle Online-Pruefung; private Fabric-Sitzungen verwenden einen besitzgebundenen
HMAC-Nachweis am eigenen Loopback-Port. Ein oeffentlicher Offline-Modus fehlt.

Noch nicht implementiert: vollstaendige Survival-Progression, alle Crafting-Rezepte,
XP, Hopper, Ruestung, weitere Mobs, Vanilla-Pathfinding, Kolben, Beobachter, Komparatoren,
Lavafluss, vollstaendige Lichtaktualisierung, Anvil,
GPU-Spielsimulation und spekulative Ticks. Die neue Simulation ist eine getestete
Basis und kein Nachweis exakten Vanilla-Tickverhaltens. Die Dekoration bleibt angenaehert; gepruefte Uebereinstimmung
bis zur Carver-Stufe bedeutet keine vollstaendige Worldgen-Paritaet.
Licht wird innerhalb eines Chunks initial berechnet;
grenzuebergreifende/dynamische Aktualisierung fehlt. Wasser fliesst; Lava bleibt statisch,
kann bei Wasserkontakt aber zu Obsidian/Kopfsteinpflaster werden.
Automatische Doerfer, Tempel, Festungen sowie Nether- und End-Generierung fehlen.

- [Protokollumfang](docs/PROTOCOL.md)
- [Neue Spielsysteme, Bedienung und Grenzen](docs/GAMEPLAY.md)
- [Konsole, OP-Rechte, Befehle und Performance-Monitor](docs/ADMIN.md)
- [Architektur](docs/ARCHITECTURE.md) und [Meilensteine](docs/ROADMAP.md)
- [Testergebnisse](docs/VALIDATION.md)
- [Referenzen und Lizenzen](docs/REFERENCES.md)

Keine proprietaeren Minecraft-Quellen, Server-JARs oder Spielassets sind enthalten.
