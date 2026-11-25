# TASK-011: Test-GUI

Status: erledigt (25.11.2026) · Module: `examples/julia/gui`, `examples/julia/IEC104.jl`

## Ziel

Einfache Oberfläche für manuelle Tests, im Browser, von Julia aus betrieben (Wunsch des Auftraggebers: „unter Julia oder im Browser, einfach“).

## Entscheidung

Julia-Programm mit eingebautem HTTP-Server aus der Standardbibliothek (`Sockets`) und einer statischen Seite (HTML, JavaScript ohne Framework). Keine Pakete, kein Build-Schritt; läuft aus dem Binärpaket. Bindung an 127.0.0.1 (kein Login, kann Befehle senden).

## Definition of Done

- [x] Leitstelle: Verbinden (Timer, k, w, tolerant, TLS), STARTDT/STOPDT, Generalabfrage, Zählerabfrage, Uhrzeit, Befehle direkt und Select/Execute, Werttabelle.
- [x] Station: Start, Datenpunkte, Werte spontan senden, Befehle annehmen/ablehnen, Verbindungen.
- [x] Ereignisse (Zustände, Befehlsergebnisse, TLS, Library-Warnungen).
- [x] Getestet über die HTTP-Schnittstelle: GUI gegen sich selbst, gegen Selbsttest-Server mit TLS und Select/Execute.
- [x] Doku (docs/07, README, Kurzanleitung Binärpaket).

## Offen

| Nr. | Thema | Stand |
| --- | --- | --- |
| O1 | Darstellung im Browser | nicht automatisch geprüft (kein Browser/JS-Engine auf dem Build-Rechner); Nutzer prüft |
| O2 | Redundanzgruppen, Konfigurationsdatei in der GUI | nicht enthalten |
