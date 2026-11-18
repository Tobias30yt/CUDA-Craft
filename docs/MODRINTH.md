# CUDA Craft in der Modrinth App

## Fertiges Paket importieren

1. In der Modrinth App die Funktion zum Erstellen/Importieren einer Instanz
   oeffnen und eine lokale `.mrpack`-Datei auswaehlen.
2. `dist/CUDA-Craft-0.11.5-windows-x64.mrpack` aus diesem Projekt importieren.
3. Die Installation von Minecraft **1.21.11**, **Fabric Loader 0.20.1** und
   **Fabric API 0.141.6+1.21.11** abschliessen. Java 21 verwenden.
4. Die Instanz starten und im Minecraft-Titelmenue **CUDA Singleplayer** anklicken.
   Dort eine Welt erstellen. Dieser Knopf startet das native Backend.

Der aktuelle Backend-Build ist fuer **Windows x64**. CUDA-Terrain benoetigt eine
kompatible NVIDIA-GPU und deren Treiber; der genaue Dichtegenerator laeuft aktuell
auf der CPU. Der Backend-Prozess und seine Welten bleiben in der Modrinth-Instanz.
Die normale Vanilla-Einzelspieler-Schaltflaeche startet nicht CUDA Craft.

Das Paket enthaelt den eigenen Fabric-Mod, das native Backend und Lizenzhinweise.
Fabric API wird mit SHA-1/SHA-512-Pruefung von Modrinth geladen. Minecraft selbst,
Kontodaten, Testwelten und vorhandene Spielstaende sind nicht im Paket enthalten.
Das Paketformat und die enthaltenen Dateien werden vom Packaging-Skript geprueft;
der Importdialog der Modrinth App wurde nicht automatisiert getestet.

## Manuell zu einer vorhandenen Fabric-Instanz hinzufuegen

Die Instanz muss ebenfalls Minecraft 1.21.11/Fabric verwenden. Ihren Spielordner
in der Modrinth App oeffnen und diese Dateien hineinkopieren:

| Quelldatei im CUDA-Craft-Projekt | Ziel relativ zum Spielordner |
| --- | --- |
| `fabric-client/build/libs/cuda-craft-client-0.11.5.jar` | `mods/cuda-craft-client.jar` |
| `build/cuda/cuda-craft.exe` | `cudacraft/native/cuda-craft.exe` |

Fabric API in der oben genannten Version ueber die App hinzufuegen. Es darf nur
eine Version des CUDA-Craft-Mods im `mods`-Ordner liegen. Allein die Mod-JAR reicht
nicht: der Client startet die EXE aus `cudacraft/native`.

Optional die lokal importierten Original-Strukturvorlagen aus `data/structures`
nach `cudacraft/native/data/structures` kopieren. Diese Minecraft-Vorlagen werden
nicht ueber GitHub oder das Paket verteilt. Ohne sie stehen die entsprechenden
`/place template`-Vorlagen nicht zur Verfuegung. Automatische Vanilla-Strukturen
sind ohnehin noch nicht implementiert.

## Vorhandene CUDA-Welten uebernehmen

Beide Clients vorher beenden. Den Ordner `client-instance/cuda-worlds` aus dem
Projekt in den Spielordner der neuen Instanz kopieren, ohne bestehende Welten zu
ueberschreiben. CUDA-Welten sind eigene Saves; sie gehoeren nicht in `saves`.
Der lokale Starter verwendet das Profil `CudaPlayer`. Ein anderer Minecraft-
Account in Modrinth hat eine andere UUID und damit einen eigenen Spielerstand.

## Paket neu bauen

Nach dem nativen Release-Build und dem Fabric-Build:

```powershell
python tools/package-modrinth.py
python tools/verify-release.py dist/CUDA-Craft-0.11.5-windows-x64.mrpack
```

Das Ergebnis und seine SHA-256-Pruefsumme liegen in `dist/`. Die `.mrpack` ist
fuer lokalen Import gedacht; damit wird kein Projekt auf modrinth.com publiziert.
Formatreferenz: https://support.modrinth.com/en/articles/8802351-modrinth-modpack-format-mrpack

Der zweite Befehl prüft Prüfsumme, native/Fabric-Version und Windows-DLL-Importe.
Er entpackt in einen frischen Pfad mit Leerzeichen/Umlauten und testet dort Start,
Statusabfrage, Speichern, Neustart, Welt-Sperren und Speicherfehler mit reduziertem
PATH. Das ersetzt keinen vollständigen Importtest im Modrinth-App-Dialog oder
einen Test auf einem sauberen zweiten PC. Dessen Nachweis bleibt offen.

Seit 0.11.5 enthält die EXE die C++-Laufzeit statisch. `native-world.lock` bleibt
nach normalem Beenden als leere Datei liegen; entscheidend ist die Betriebssystem-
Sperre, nicht die Existenz der Datei. Die Datei nicht während einer laufenden
Welt löschen. Der Client zeigt nach Speicherfehlern/erzwungenem Beenden eine
Fehlermeldung statt „Local world saved“; Details stehen im Weltordner in
`native-server.log`.
