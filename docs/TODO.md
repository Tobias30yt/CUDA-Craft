# CUDA Craft – nächste Implementierungen

Stand: 18.11.2026, Version 0.11.5, Minecraft Java 1.21.11.

Die [Aufgabenliste](implementation-backlog.json) enthält 59 priorisierte Aufgaben
mit Status, nächstem Schritt, Abnahmekriterium und Abhängigkeiten. Davon sind
25 offen, 33 teilweise umgesetzt und eine erledigt. Diese Zahlen beschreiben
den ausgewählten Entwicklungsplan, keinen prozentualen Vanilla-Fertigstellungsgrad.

## Als Nächstes

1. [Orbital Strike Cannon Mk.6/MS](OSC_VANILLA_TEST.md): vollständigen Fixture-Import und Vanilla-Referenzlauf für 1.21.11 vorbereiten.
2. Update-Reihenfolge, Observer/Comparatoren, Kolben und TNT anhand kleiner Referenzschaltungen umsetzen; danach die vollständige Kanone testen.
3. Die neue Netherportal-Verknüpfung mit Vanilla-Vergleichsfällen absichern und die Platzierungswahl angleichen.
4. Nether/End-Generierung, Features und automatische Bauwerke vervollständigen.
5. Workstations, Redstone, Flüssigkeiten, Mob-KI und Survival anhand der Abnahmekriterien erweitern.

P0 bezeichnet Grundlagen, P1 zentrale Spielmechaniken, P2 weiteren Ausbau und
P3 Auslieferung. Die Reihenfolge ist ein Vorschlag, keine Terminprognose.

## Prüflisten

- [1.166 Blocktypen](block-coverage.csv): Für mehr als 90 % müssen mindestens
  **1.050 Typen** ihr vollständiges Verhalten im Vanilla-Vergleich bestehen.
  Ein registrierter oder platzierbarer Block zählt dafür noch nicht als fertig.
- [89 Mobtypen](MOBS.md): Spawn, Speicherung und gemeinsame Basisprofile sind
  vorhanden. Artspezifische KI, Interaktionen, Loot und Bossphasen sind weiterhin offen.
- Die zu diesem Stand erstellte Excel-Datei enthält die Blätter **Aufgaben**,
  **Blöcke** und **Mobs**, Filter, Statusauswahl und automatisch berechnete Zähler.
  Sie liegt lokal unter `outputs/01a1d5db-d59d-75c1-8b99-8bb6f5555ad7/` und wird
  im Chat bereitgestellt. Änderungen in Excel synchronisieren sich nicht automatisch
  mit der Aufgabenliste im Repository.

## Seit 0.11.5

Native Welt-Sperren schützen auch außerhalb des Fabric-Clients gegen paralleles
Öffnen. Fehlgeschlagene oder erzwungene Server-Abschaltungen werden nicht mehr als
erfolgreich gespeichert gemeldet. Die portable EXE benötigt keine zusätzlich
installierte VC++-Laufzeit. Das ist noch keine transaktionale Mehrdimensionsspeicherung.
Original-Schematics für Cubicmetres Kanone wurden inventarisiert und mit Hashes
fixiert; B056–B059 beschreiben die verbleibenden Integrationstests und Grundlagen.

## Seit 0.11.4 teilweise umgesetzt

1.010 geformte/ungeformte Crafting-Rezepte, gespiegelte Muster, Item-Tags und
Crafting-Rückgabe-Items sind vorhanden. Shift-Klick verarbeitet mehrere Durchläufe
und behandelt volle Inventare ohne Zutatenverlust. 60 Spezial-/Komponentenrezepte
bleiben ausgeschlossen. Details: [Crafting](CRAFTING.md).

## Seit 0.11.3 teilweise umgesetzt

Linker/rechter Inventar-Drag, Doppelklick-Sammeln und Offhand-Tausch sind eingebaut.
Veraltete Transaktionen und Änderungen durch andere Container-Nutzer brechen einen
laufenden Drag ab. Vollständige Komponenten und Spezialmenüs bleiben
offen. Details: [Inventare](INVENTORY.md).

## Seit 0.11.2 teilweise umgesetzt

Netherportale verwenden 8:1-Koordinaten, suchen intakte Ziele und bauen bei Bedarf
einen Rahmen mit freiem Ankunftsbereich. Hin- und Rückreise wurden auch im echten
Fabric-Client geprüft. Die genaue Vanilla-Platzierungswahl und Kollisionskorrektur
bleiben offen. Details: [Portale](PORTALS.md).

## In 0.11.1 erledigt

Beschädigte Netherportale werden nach Änderungen an erforderlichen Rahmenblöcken
oder Portalflächen entfernt. Beide Achsen werden geprüft; fehlende Rahmenecken
bleiben zulässig. Pro Tick werden höchstens acht zusammenhängende Portalflächen
verarbeitet. Bei vielen gleichzeitigen Änderungen kann die Bereinigung daher
mehrere Ticks benötigen. Die Regression umfasst außerdem einen Spieler im
beschädigten Portal. Details: [Validierung](VALIDATION.md).
