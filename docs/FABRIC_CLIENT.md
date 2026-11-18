# CUDA Craft Singleplayer 0.11

Minecraft Java **1.21.11**, Fabric Loader **0.20.1**, Fabric API
**0.141.6+1.21.11**. Der Vanilla-Client rendert Welt, Inventar und Oberflaeche.
Ein Fabric-Mod verwaltet einen eigenen nativen CUDA-Craft-Prozess pro Sitzung.
Die Simulation laeuft weiterhin im C++-Backend; es wird kein Java-Spielserver
als Backend gestartet. Der praezise Density-Generator und die Spielsimulation
laufen derzeit auf der CPU; die vorhandenen CUDA-Pfade bleiben erhalten.

## Start auf diesem Rechner

`Start CUDA Client.cmd` doppelklicken. Im Titelmenue **CUDA Singleplayer** waehlen,
Weltname und optional Seed eingeben, Survival/Creative und Overworld/Flat waehlen,
dann **Create and play**. Bereits erstellte Welten erscheinen darueber.
ESC pausiert die lokale Simulation, auch in den nachfolgenden Optionsmenues.
Beim Verlassen der Welt oder Beenden des Clients wird gespeichert und der
zugehoerige Server beendet. Der lokale Spieler hat OP-Rechte; `/help`, `/monitor on`
und `/perf` stehen zur Verfuegung.

Der Starter nutzt die bereits installierten offiziellen 1.21.11-Dateien in
`%APPDATA%/.minecraft`, den vorhandenen Java-21-Laufzeitpfad und Python 3.10.
Er installiert Fabric-Bibliotheken und den Mod getrennt in `client-instance`.
Das Profil **CudaPlayer** ist ein lokales Profil, keine Microsoft-Anmeldung.
Fuer einen anderen lokalen Namen: `Start CUDA Client.cmd --name DeinName`.
Der Name bestimmt die lokale UUID; ein Namenswechsel erzeugt einen anderen
Spielerstand. Online-Mehrspieler erfordert einen normal angemeldeten Launcher.
Der Starter liest keine Kontotokens; entsprechende Session-Key-401-Meldungen
im Vanilla-Log sind beim lokalen Profil zu erwarten.

Alternativ den gebauten Mod `fabric-client/build/libs/cuda-craft-client-0.11.5.jar`
und die passende Fabric API in einen eigenen Fabric-1.21.11-Launcher eintragen.
Die native EXE gehoert unter `<Spielordner>/cudacraft/native/cuda-craft.exe`,
die importierten Vorlagen unter `cudacraft/native/data/structures`.
Der Standardstarter bereitet diese Dateien automatisch vor.

## Speicherung und Verbindung

Jede Welt liegt unter `client-instance/cuda-worlds/<UUID>` mit
`cuda-world.properties`, `blocks.json`, `operators.json` und `native-server.log`.
Der gesamte Weltordner gehoert ins Backup. Die letzten Serverlogs werden rotiert.
Vanilla-Anvil-Spielstaende werden nicht importiert oder veraendert. Der Fabric-Mod
sperrt einen geoeffneten Weltordner gegen eine zweite Fabric-Sitzung.

Der native Prozess verwendet `--local-session`, ausschliesslich **127.0.0.1**
und einen vom Betriebssystem vergebenen Port. Ein zufaelliges 256-Bit-Geheimnis
wird nur im Kindprozess-Environment uebergeben. Eine Login-Plugin-Challenge
`cudacraft:local_session` prueft HMAC-SHA256 ueber einen frischen 32-Byte-Nonce;
Name und UUID muessen zum startenden Profil passen. Der Client beantwortet
Challenges ausschliesslich am eigenen Loopback-Port. Danach folgen RSA/AES und
Kompression. Der normale Netzwerkserver behaelt seine Mojang-Kontopruefung.
Wenn der Frontend-Prozess verschwindet, loest das Schliessen seiner stdin-Pipe
Speichern und Serverende aus. Geheimnisse stehen weder in Argumenten noch Logs.

## Bloecke und Strukturen: genaue Grenze

Alle **1.166 Blocktypen / 29.671 Blockzustaende** sind in der Registry vorhanden;
alle registrierten Blockitems erhalten einen Platzierungszustand. **1.505 Items**
sind als einfache Inventarstapel zugelassen. Achsen, horizontale Ausrichtung,
Rotation, obere/untere Treppen und Slabs sowie der Waterlogged-Platzierungszustand
werden aus Eigenschaften erzeugt. Tueren, Betten und hohe Pflanzen werden als
zusammengehoerige Bloecke gesetzt und entfernt; Holz-/Kupfertueren, Falltueren und
Zauntore lassen sich manuell umschalten. Zaeune, Glasscheiben und Eisengitter
aktualisieren einfache Nachbarverbindungen. Gleiche Slabs lassen sich kombinieren.
Wasserfuellbare Bloecke nehmen Eimerwasser auf, geben es wieder ab und speisen den
Wasserfluss; beim Abbauen bleibt die Quelle stehen. Vollstaendige Blockformen,
Treppenecken, Schlafen/Respawn an Betten, saemtliche Spezialinventare und die
Funktionen aller Items sind noch nicht implementiert. Besondere Item-Komponenten
ausser Werkzeugverschleiss werden weiterhin abgelehnt. Registry-Abdeckung bedeutet
deshalb noch keine vollstaendige Vanilla-Spielmechanik.

Seit 0.10 speichern Truhen, Doppeltruhen, Faesser und die drei Ofentypen ihre
Inventare. Oefen schmelzen mit Brennstoff und gespeicherten Fortschritten;
Details stehen in [Container und Oefen](CONTAINERS.md).
Der Starter installiert den aktuellen Mod als `mods/cuda-craft-client.jar` und
verschiebt erkannte aeltere CUDA-Craft-Modversionen in `cudacraft/mod-backups`,
damit Fabric nicht zwei Versionen desselben Mods gleichzeitig laedt.

`tools/import-vanilla-structures.py` konvertiert **1.202 Originalvorlagen** aus dem
lokal vorhandenen und checksum-geprueften offiziellen 1.21.11-Serverarchiv.
Diese Minecraft-Assets bleiben lokale, ignorierte Dateien. Die Liste steht in
`data/structures/manifest.json`. Als OP beispielsweise:

```
/place template village/plains/houses/plains_small_house_1 ~ ~ ~
/place template igloo/top ~ ~ ~
```

Optional folgt eine numerische Palettenvariante, beginnend bei 0. Platzierung
uebernimmt die originalen Blockzustaende und prueft die komplette Vorlage vor
einer Aenderung. Grosse Vorlagen werden ueber begrenzte Chunk-Neusendungen
uebertragen. Jigsaw-Marker werden durch ihren `final_state` ersetzt; Struktur-
Marker werden ausgelassen. Block-Entity-Daten und Entities werden noch nicht
ausgefuehrt; der Befehl nennt deren Anzahl. Es gibt noch keine Rotation, keinen
Loot und keine Jigsaw-Zusammensetzung ganzer Doerfer, Bastionen usw. Native
automatische Strukturplatzierung nach Vanilla-Seedregeln ist weiterhin offen.
Die Weltgrenze von 2048 x 2048 und das Limit von 100.000 geaenderten Positionen
gelten auch fuer Vorlagen.

## Build und Tests

`tools/build-fabric-client.cmd` verwendet das lokale Gradle **9.2.1** und JDK 23
mit Java-21-Zielbytecode. Es baut den Mod und fuehrt JUnit-Tests fuer Weltkatalog,
Login-Beweis und Fehler beim Start aus. Native Tests: `ctest --preset cuda-local
--output-on-failure`. `tests/local_session_test.py` testet das Produktionsbackend
mit falschen Identitaeten, HMAC/Replay-Angriffen, privater Anmeldung und Parent-EOF.

`tools/launch-fabric-client.py --smoke` startet einen echten isolierten Fabric-Client,
erstellt eine neue Testwelt, prueft den Weltbeitritt und beendet ihn nach 20 Sekunden.
`--smoke-generator density` prueft den Oberweltgenerator. Testdaten liegen unter
`.tools/fabric-smoke`, getrennt von Spielwelten. Das Skript prueft auch Serverende
und gespeicherte Spieler. `tests/vanilla_structure_assets_test.py` vergleicht alle
Vorlagenpaletten mit dem unabhaengig erzeugten offiziellen Blockbericht.

`--smoke --smoke-containers` prueft in einer neuen Survival-Testwelt die echten
Truhen-, Ofen- und Werkbankmenues, einschliesslich Schmelzen, Craften und der
gespeicherten Itemmengen. Der Client beendet sich nach erfolgreichem Ablauf;
nach spaetestens 30 Sekunden wird ein unvollstaendiger Ablauf als Fehler gemeldet.
