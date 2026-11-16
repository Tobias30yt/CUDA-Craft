# Spielsysteme in 0.7.0

Minecraft Java 1.21.11 / Protokoll 774. Die folgenden Systeme laufen nativ
im C++-Server mit 20 Spielticks pro Sekunde. Sie sind eine erste spielbare
Implementierung; ein differentieller Vanilla-Simulationstest steht noch aus.

## Bedienung

- Neue Spieler starten im normalen Starter in Survival. `--gamemode creative`
  waehlt den Anfangsmodus fuer neue Spieler; gespeicherte Modi bleiben erhalten.
- `/gamemode survival` und `/gamemode creative` wechseln den eigenen Modus.
- `/summon pig`, `/summon zombie`, `/time set day`, `/time set night`, `/spawn`.
  Seit 0.8 benoetigen Spielmodus, Summon und Zeit OP-Rechte; `/spawn` bleibt frei.
  Ersten OP mit `op Name` in der Konsole setzen. Siehe [Verwaltung](ADMIN.md).
- Wasser mit Wassereimer setzen und mit leerem Eimer eine Quelle aufnehmen.
- Hebel rechtsklicken; Repeater rechtsklicken, um die Verzoegerung zu wechseln.
- Werkbank rechtsklicken fuer das 3x3-Menue. Das Inventar bietet das 2x2-Feld.

## Wasser

Quellen, sieben horizontale Flussstufen, fallendes Wasser, Rueckzug nach dem
Entfernen einer Quelle, zwei Quellen zur Regeneration und Suche nach einem
Abfluss bis vier Schritte Entfernung. Nachbarticks werden nach fuenf Spielticks
ausgefuehrt. Fluss kann Chunkgrenzen ueberschreiten. Wasser kuehlt benachbarte
Lava zu Obsidian oder Kopfsteinpflaster. Lava selbst fliesst noch nicht.
Waterlogging mit Eimerfuellung/-entnahme, Quellfluss und Erhalt der Quelle beim
Abbau ist seit 0.9 vorhanden. Exakte Blockform-Kollision, Stroemungskraefte und alle
Vanilla-Fluessigkeitswechselwirkungen fehlen. Worldgen-Promotion bleibt ein
separates, noch nicht exakt abgeglichenes Thema.

## Survival und Inventar

Seit 0.9 sind alle Blockitems registriert. Tueren, Betten und hohe Pflanzen setzen
und entfernen ihre zusammengehoerigen Teile gemeinsam; bei blockiertem zweiten
Platz wird nichts gesetzt. Holz-/Kupfertueren, Falltueren und Zauntore lassen sich
rechtsklicken. Slabs kombinieren sich zu Doppel-Slabs, die beim normalen Abbau zwei
Items liefern. Zaeune, Glasscheiben und Eisengitter aktualisieren ihre einfachen
Nachbarverbindungen. Schlafen, Tuerscharniere nach Nachbargeometrie, vollstaendige
Treppenformen und exakte Kollisionen sind weiterhin offen.

Endliche Stapel, Abbauzeit, einfache Werkzeug-/Erzstufen, Werkzeugverschleiss,
Drops und Aufheben, Fallschaden, Ertrinken, Lavaschaden, Hunger, Essen,
Regeneration, Tod mit Inventar-Drops und Respawn. Brot, Apfel, Steak und rohes
Schweinefleisch koennen gegessen werden. Kampf hat eine einfache Abklingzeit.

Unterstuetzte Rezepte: Staemme zu Planken, Planken zu Stoecken/Werkbank,
Kohle plus Stock zu Fackeln, Holz-/Stein-/Eisen-/Diamantwerkzeuge aus einfachen
Rezeptmustern. Seit 0.10 kommen Truhen, Faesser, Oefen, Schmelzoefen, Raeucheroefen
und Holzslabs hinzu. Die drei Ofentypen verwenden die originalen 1.21.11-Schmelz-
und Brennstofftabellen. Siehe [Container und Oefen](CONTAINERS.md).
Die komplette Survival-Progression ist noch nicht erreicht. Loot, Werkzeuggeschwindigkeit und Schaden sind
vereinfacht; Verzauberungen, Ruestung, XP, Effekte und Landwirtschaft fehlen.

Inventarklicks funktionieren mit Links-/Rechtsklick, Hotbar-Zahlentasten,
Shift-Transfer zwischen Hauptinventar und Hotbar sowie Wegwerfen. Crafting
entnimmt pro Klick ein Rezept. Drag-Verteilung, Doppelklick und Shift-Crafting
werden noch zurueckgesetzt. Clientseitige Slot-Hashes ersetzen niemals den
serverseitigen Inventarstand. Kreativ-Inventarpakete werden in Survival ignoriert.
Bewegung/Kollision beruhen weiterhin weitgehend auf dem Vanilla-Client;
dies ist keine vollstaendige Physik- oder Anti-Cheat-Implementierung.

## Mobs

Schweine wandern, Zombies verfolgen und greifen Survival-Spieler an.
Ueberlebte Nahkampftreffer stossen beide Mobarten vom Angreifer weg und heben
sie kurz an. Der Impuls bleibt ueber KI-Ticks erhalten, klingt durch Reibung ab
und kollidiert mit vollen Bloecken, Waenden und Decken. Bewegungsdaten gehen an
sichtbare Clients und bleiben beim Speichern erhalten. Sprint-/Verzauberungsboni,
Resistenzen und exakte Vanilla-Kollisionsformen sind noch nicht enthalten.
Zombies nehmen im ungeschuetzten Tageslicht Schaden. Einfache Gravitation,
Ein-Block-Stufen, Treffer, Tod, Schweinefleisch/Rotten-Flesh-Drops und Aufheben
sind implementiert. In sichtbaren Chunks spawnen begrenzt Schweine am Tag und
Zombies in der Nacht. Spawning und KI sind angenaehert, ohne Vanilla-Pathfinding,
Zucht, Navigation um komplexe Hindernisse, Ausruestung oder weitere Mobarten.

## Redstone

Hebel, Redstonebloecke, Staubleitungen mit Staerke 0..15 und Abschwaechung,
Stufenverbindungen, invertierende Redstonefackeln, gerichtete Repeater mit
1..4 Redstoneticks Verzoegerung und seitlicher Verriegelung sowie Lampen.
Lampen schalten verzoegert aus. Leitungen werden ohne Rueckkopplung ihrer
alten Signalstaerke neu ausgewertet.

Kolben, Komparatoren, Beobachter, Druckplatten, Knoepfe, Quasi-Connectivity,
Fackel-Burnout und exakte Vanilla-Nachbarreihenfolge fehlen. Das Lampenmodell
schaltet sichtbar um; dynamische Lichtpakete fehlen. Fackel-/Lampenlicht wird
erst beim erneuten Laden des Chunks berechnet.

## Speichern und Grenzen

Die bestehende `blocks.json` enthaelt zusaetzlich ein versioniertes
`simulation`-Objekt: Uhrzeit, ausstehende Wasser-/Redstone-Ereignisse,
Spieler nach UUID, Inventar einschliesslich Cursor/Craftingfeld, Gesundheit,
Hunger, Modus und Mobs. Nach erneutem Beitreten werden offene Crafting-Inhalte
ins Inventar zurueckgegeben. Autosave jede Sekunde, atomarer Dateiaustausch,
Speicherung bei normalem Beenden. Alte Weltdateien ohne Simulation laden weiter.

Grenzen: 100.000 Blockaenderungen, 65.536 ausstehende Wasserticks,
maximal 2.048 Wasserpositionen pro Tick, 128 lebende Mobs, insgesamt
4.096 Mob-/Item-Entities und 1.024 gespeicherte Spieler. Simulation und
Inventar sind CPU-basiert; CUDA-Terrain und GPU-Diagnostik bleiben verfuegbar.
