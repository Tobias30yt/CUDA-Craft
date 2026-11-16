# Container und Oefen in CUDA Craft 0.10

Ziel bleibt Minecraft Java 1.21.11. Alle folgenden Funktionen laufen im nativen
C++-Backend; der Fabric-Client verwendet die originalen Minecraft-Menues.

## Truhen und Faesser

Truhe, Redstone-Truhe und Fass haben 27 gespeicherte Plaetze. Zwei benachbarte,
gleich ausgerichtete Truhen desselben Typs verbinden sich beim Setzen zu einer
Doppeltruhe mit 54 Plaetzen. Beide Haelften oeffnen dasselbe Inventar in derselben
Reihenfolge. Beim Abbau einer Haelfte bleibt die andere mit ihrem Inhalt bestehen.
Truhen mit einem als solide erkannten Block darueber lassen sich nicht oeffnen.

Links-/Rechtsklick, Shift-Klick, Hotbar-Zahlentasten und Wegwerfen werden
serverseitig berechnet. Mehrere Spieler koennen denselben Container oeffnen;
jede Aenderung aktualisiert die anderen Zuschauer. Veraltete Inventarrevisionen,
falsche Fenster-IDs und verspätete Schliesspakete veraendern keine Items.
Entfernung, Tod, Blockabbau und Verlassen des Spiels schliessen das Menue.
Gegenstaende am Cursor werden ins Inventar zurueckgegeben oder abgelegt.

Beim Abbau werden Inhalte einmal als Item-Entities abgelegt. Reicht das bestehende
Entity-Limit dafuer nicht aus, wird die Blockaenderung abgelehnt und der Container
bleibt erhalten. Dies gilt auch beim Ueberschreiben durch Strukturvorlagen.
Bis zu 4.096 Container werden pro Welt gespeichert. Alte Welten ohne Containerfeld
laden weiter. Positionen, Typen, Stapelgroessen und Werkzeugverschleiss werden beim
Laden validiert; doppelte Positionen oder ungueltige versteckte Slots sind Fehler.

## Ofen, Schmelzofen und Raeucherofen

Alle **107 originalen Schmelz-/Blasting-/Smoking-Rezepte** werden unterstuetzt:
Nach Aufloesung der Item-Tags ergeben sich **227 Eingabezuordnungen**. Die Tabelle
enthaelt **280 originale Brennstoffe**. Die Daten werden aus dem lokal vorhandenen,
geprueften 1.21.11-Serverarchiv und Mojangs gemappter Brennstoffreferenz gewonnen;
`tools/smelting-data-manifest.json` dokumentiert Herkunft und Hashes.

Rechtsklick oeffnet das passende Vanilla-Menue. Oben kommt das Material hinein,
unten der Brennstoff. Shift-Klick ordnet schmelzbare Items und Brennstoffe zu.
Das Ausgabefeld nimmt keine vom Spieler eingefuegten Items an. Volle oder mit
einem anderen Ergebnis belegte Ausgaben verhindern weiteres Schmelzen und das
Anbrechen eines neuen Brennstoffs. Bereits brennender Brennstoff laeuft weiter.

Ofenrezepte brauchen ihre hinterlegte Garzeit; Schmelzofen und Raeucherofen
nutzen ihre eigenen Rezepte und halbierte Brenndauern. Wechselndes Eingabematerial
setzt den Garfortschritt zurueck. Ohne Brennstoff kuehlt der Fortschritt ab.
Brennzeit, Garfortschritt, Eingabe und Ausgabe werden gemeinsam gespeichert.
Lavaeimer hinterlassen einen Eimer; ein nasser Schwamm kann diesen beim Trocknen
in einen Wassereimer verwandeln. Der sichtbare `lit`-Blockzustand sowie die
Fortschrittsbalken werden synchronisiert.

Neue Crafting-Rezepte: Truhe, Fass, Ofen, Schmelzofen, Raeucherofen und Holzslabs.
Damit lassen sich beispielsweise nach Holz-/Steinwerkzeugen ein Ofen und
anschliessend Eisenbarren herstellen. Es ist weiterhin kein vollstaendiger
Vanilla-Rezeptmanager vorhanden; Rezeptbuch-Autofill bleibt offen.

## Grenzen

XP-Orbs, Ofen-Rezept-XP, automatische Hopper-Befoerderung, Komparator-Ausgaben,
das Signal geoeffneter Redstone-Truhen und Deckelanimationen fehlen noch.
Ofen-Simulation folgt derzeit dem globalen nativen Scheduler; vollstaendige
Vanilla-Chunk-Tickets und Entladung sind nicht implementiert. Dynamische
Lichtausbreitung fehlt trotz korrektem Ofen-Blockzustand. Strukturvorlagen
uebernehmen noch keine NBT-Inhalte, Loot-Tabellen oder benannten Container.
Shulkerkisten, Endertruhen und weitere Spezialinventare sind nicht enthalten.

## Pruefung

`container-tests` prueft Container-Lebensdauer, persistente Werkzeugdaten,
Entity-Kapazitaet, ungueltige Saves sowie jede der 227 Ofen-Eingabezuordnungen
mit Garzeit und Ergebnis. Fortschritt wird mitten im Schmelzen gespeichert,
neu geladen und bis zum Ergebnis fortgesetzt.

`container-network` verwendet unabhaengige verschluesselte Protokollclients:
zwei Zuschauer, veraltete Klicks, Fensterwechsel, Doppeltruhe, Abbau-Drops,
Neustart, Entfernung, Ofen-Fortschritt und Schutz des Ausgabefelds.
Die Crafting-Tests gehen ebenfalls durch die echten Menue-Pakete.

`tools/launch-fabric-client.py --smoke --smoke-containers` startet einen isolierten
echten Fabric-Client, oeffnet eine vorbereitete Truhe und einen Ofen und entnimmt
17 Diamanten sowie einen frisch geschmolzenen Eisenbarren. Anschliessend stellt
er aus acht Brettern an der Werkbank eine Truhe her. Nach dem Beenden werden
Spielerinventar und Container im nativen Save auf genau diese Mengen und den
Verbrauch der Bretter geprueft.
Dieser Testmodus ist im normalen Starter ausgeschaltet.
