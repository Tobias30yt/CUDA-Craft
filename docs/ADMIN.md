# Konsole, OP und Performance in 0.8.0

## Schnellstart

1. `Start CUDA Craft.cmd` starten und mit Minecraft Java 1.21.11 beitreten.
2. Im Serverfenster `op DeinSpielername` eingeben und Enter druecken.
3. Im Spiel `/monitor on` fuer die Live-Anzeige oder `/perf` fuer Details eingeben.
4. Im Serverfenster `monitor on` fuer regelmaessige Messwerte einschalten.
5. Mit `stop` in der Konsole sauber speichern und beenden.

Die Konsole akzeptiert Befehle mit oder ohne `/`. Der erste Spieler wird nicht
automatisch OP. `op` funktioniert fuer aktuell verbundene oder zuvor auf dieser
Welt authentifizierte Spieler. Unbekannte Namen muessen zuerst beitreten.
Die Test-EXE verwendet wie bisher ausschliesslich eine lokale Testidentitaet;
die Produktions-EXE bleibt im Online-Modus.

## Berechtigungen

Es gibt zwei Rollen: Spieler und Operator. Die lokale Konsole besitzt volle Rechte.
Operatoren duerfen auch andere Operatoren hinzufuegen und entfernen. Rechte werden
an der verifizierten UUID gespeichert, nicht allein am Spielernamen. Ein neuer
Account mit demselben Namen erbt daher keine Rechte. Namensaenderungen behalten
die Rechte der UUID. Mehrdeutige alte Namen erfordern einen erneuten Beitritt.

`operators.json` liegt im jeweiligen Weltordner neben `blocks.json` und enthaelt
Operatoren sowie bekannte UUID-/Namenspaare. Rechte sind damit pro Welt getrennt.
Die Datei wird atomar ersetzt. Fehlerhafte Dateien verhindern den Serverstart,
statt unbemerkt alle Rechte zu erteilen oder zu loeschen. Zur Sicherung beide
Dateien mitnehmen. Eine laufende Datei nicht parallel von Hand bearbeiten.

OP und Deop aktualisieren sofort den Command-Baum und den Client-Permissionsstatus.
Jeder Befehl wird ausserdem beim Ausfuehren erneut geprueft; ein manipuliertes
Chatpaket umgeht die Rechte nicht. Erfolgreiche Verwaltungsbefehle erscheinen mit
`[ADMIN]` in der Konsole. Auch der Minecraft-Spielmoduswechsel per F3+F4/F3+N
benoetigt OP. Adventure und Spectator sind noch nicht implementiert; der Client
erhaelt einen Hinweis und behaelt seinen bisherigen Modus. Es gibt noch keine
fein abgestuften OP-Level, Bans,
Whitelist, RCON, Plugins oder allgemeine Selektoren wie `@a`/`@e`.

## Befehle

Bei Spielerzielen bedeutet `@s` der ausfuehrende Spieler. Die Konsole muss einen
Namen angeben. Spielerziele muessen online sein, ausser bei `op`/`deop`.
`help <Befehl>` zeigt die genaue Syntax; Tab-Vorschlaege beachten die Rechte.

| Befehl | Rechte | Funktion |
| --- | --- | --- |
| `help [Befehl]` | alle | Verfuegbare Befehle und Syntax |
| `version` | alle | Server-, Minecraft- und Protokollversion |
| `list` | alle | Verbundene Spieler |
| `whoami` | alle | Eigene UUID und Rolle |
| `spawn` | Spieler | Zum Weltspawn teleportieren |
| `tps` | alle | Kompakte Messwerte |
| `perf` | alle | TPS, Tick-/Loopzeiten, CPU/RAM, Netzwerk, Chunks, Entities und Speicherzeit |
| `perf export` | OP/Konsole | Momentaufnahme in `performance.json` im Weltordner |
| `monitor on` / `monitor off` | alle | Im Spiel Actionbar jede Sekunde, in der Konsole standardmaessig alle 5 Sekunden |
| `monitor on 1` | Konsole | Ausgabeintervall 1..60 Sekunden |
| `op Name` / `deop Name` | OP/Konsole | Persistente OP-Rechte setzen/entziehen |
| `ops` | OP/Konsole | Operatoren auflisten |
| `seed` | OP/Konsole | Weltseed und Generator |
| `gamemode survival\|creative [Name]` | OP/Konsole | Spielmodus setzen; Standardziel ist man selbst |
| `time set day\|night\|Zahl` | OP/Konsole | Tageszeit, Zahl 0..23999 |
| `summon pig\|zombie [x y z]` | OP/Konsole | Unterstuetzte Mobs erzeugen; Konsole benoetigt Koordinaten |
| `tp Name` | OP im Spiel | Zum anderen Spieler |
| `tp x y z` | OP im Spiel | Sich selbst teleportieren |
| `tp Name Zielspieler` | OP/Konsole | Einen Spieler zu einem anderen teleportieren |
| `tp Name x y z` | OP/Konsole | Einen Spieler an Koordinaten teleportieren |
| `give Name Gegenstand [Anzahl]` | OP/Konsole | 1..64 Gegenstaende; z.B. `give @s minecraft:diamond 3` |
| `heal [Name]` | OP/Konsole | Gesundheit, Luft und Hunger auffuellen |
| `feed [Name]` | OP/Konsole | Hunger auffuellen |
| `clear [Name]` | OP/Konsole | Inventar, Cursor und Craftingfeld leeren |
| `kill [Name]` | OP/Konsole | Tod mit normalem Drop-/Respawn-Ablauf, auch im Kreativmodus |
| `kick Name [Grund]` | OP/Konsole | Verbindung mit lesbarem Grund beenden |
| `say Nachricht` | OP/Konsole | Servernachricht an alle verbundenen Spieler |
| `save-all` | OP/Konsole | Welt, Spieler und Rechte speichern |
| `stop` | OP/Konsole | Speichern, Spieler trennen und Prozess sauber beenden |

Teleportkoordinaten akzeptieren `~` und `~Zahl`, z.B. `tp ~10 ~ ~-5`.
Grenzen: x/z -1023..1023, y -63..319. Nichtendliche Zahlen werden abgelehnt.
`give` legt nur tatsaechlich passende Gegenstaende ins Inventar und meldet die
uebergebene Anzahl bei Platzmangel. Ein Gegenstand aus der Registry erhaelt durch
`give` keine bisher fehlende Spielfunktion: Oefen, Ruestung usw. bleiben unvollstaendig.

## Was der Monitor misst

- **TPS 1s/10s/60s:** Anzahl ausgefuehrter Ticks geteilt durch reale Zeit im
  jeweiligen Fenster, maximal 20. Direkt nach dem Start ist das Fenster kuerzer.
- **Sim ms:** Dauer eines Simulationsschritts. Mittelwert, p95 und Maximum aus
  den letzten 60 Sekunden. Zielbudget 50 ms. Tickzahlen und Schritte ueber 50 ms
  stehen zusaetzlich im JSON-Export.
- **Loop work:** Aktive Arbeit der Hauptschleife einschliesslich Netzwerk,
  Kompression, Chunk-Erstellung, Konsole und Autosave. Die Wartezeit in `select`
  ist abgezogen. Fenster maximal 60 Sekunden bzw. die letzten 4096 Messungen.
  So wird langsame Chunk-Erstellung sichtbar, auch wenn Sim-ms niedrig bleiben.
- **Dropped slots:** Wegen Ueberlast verworfene 50-ms-Nachholintervalle.
- **RAM:** Working Set des Serverprozesses in MiB. **Private commit** ist
  zugesicherter privater Speicher; das ist nicht derselbe Wert wie residenter RAM.
- **CPU:** Prozess-CPU-Zeit, auf alle logischen Prozessoren normiert. 100 Prozent
  bedeutet volle Auslastung aller logischen Prozessoren, nicht eines einzelnen
  Threads. Erste Werte nach ungefaehr zwei Sekunden; keine erfundenen Startwerte.
- **Netzwerk:** Tatsaechlich empfangene/gesendete TCP-Nutzdaten inklusive Framing,
  Kompression und Verschluesselung, etwa sekundenweise gemessene KiB/s,
  ausstehende Ausgabebytes und Verbindungen. Kein Ethernet/IP-Overhead.
- **Welt:** Spieler, Mob-/Item-Entities, Terrain-Cache-Chunks, Blockaenderungen,
  ausstehende Wasserticks, erzeugte/gesendete Chunks und Cache-Treffer.
- **Kosten:** Letzte/maximale Chunk-Generierung, letztes Erstellen/Senden eines
  Chunk-Pakets und letzter erfolgreicher Welt-Speichervorgang in Millisekunden.

Das ist ein Monitor fuer diesen Server, kein allgemeiner PC-Monitor. GPU-Auslastung,
VRAM und Temperaturen werden nicht abgefragt. Der CUDA-Buildstatus ist getrennt
von den Messwerten: die praezise Density-Generierung und laufende Spielsimulation
arbeiten weiterhin auf der CPU. Die Messung behauptet keine FPS oder Pingwerte.

## Konsole und Beenden

Die Eingabe wird ohne blockierendes `getline` im Serverthread verarbeitet.
Native Windows-Konsole und umgeleitete UTF-8-Eingabe werden unterstuetzt. Backspace
funktioniert; Shell-History und eine vollstaendige Terminal-UI sind nicht enthalten.
Ueberlange Zeilen werden als Ganzes verworfen. EOF beendet beim normalen Netzwerkserver
nur die Eingabe. Private Fabric-Sitzungen speichern und stoppen bei Parent-EOF.
Konsole und Befehle starten keine Shell und fuehren keine fremden
Programme aus. Quick-Edit-Pausen durch Mausselektion sind waehrend des Betriebs
abgeschaltet; beim Verlassen wird der urspruengliche Konsolenmodus wiederhergestellt.

`stop` und Strg+C trennen Spieler, geben offene Crafting-Inhalte zurueck und speichern
den letzten Stand. Fuer das Senden der Abschiedspakete gilt eine kurze begrenzte
Frist; ein nicht mehr erreichbarer Client blockiert das Beenden nicht.

Seit 0.9 setzt `/place template <id> x y z [palette]` lokal importierte
Vanilla-Strukturvorlagen. `/pause on|off` ist nur in privaten Fabric-Sitzungen
verfuegbar. Beide Befehle erfordern OP. Siehe [Fabric-Client](FABRIC_CLIENT.md).
