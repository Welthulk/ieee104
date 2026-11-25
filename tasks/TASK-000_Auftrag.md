# TASK-000: Auftrag – exemplarische IEC-104-Library

Status: erledigt (25.11.2026) · gilt für alle weiteren Aufgaben in `tasks/` · Vorrang vor TASK-001

## 1. Die Aufgabe

Wir bauen eine **exemplarische Library für IEC 60870-5-104**: Client (Controlling Station) und Server (Controlled Station), Kern in C++17, stabile C-API, TLS über OpenSSL.

„Exemplarisch“ heißt hier:

- **Normgerecht und nachvollziehbar.** Jede Protokollregel im Code ist auf die Norm oder eine belegte Quelle zurückführbar. Was nicht belegt ist, wird markiert und nicht geraten.
- **Lesbar vor schnell.** Klare Struktur, kleine Funktionen, keine Tricks. Wer den Code liest, soll IEC 104 verstehen können.
- **Vollständig getestet.** Unit-Tests mit Referenz-Hexdumps, Zustandsmaschine mit simulierter Zeit, Fuzzing, Interop gegen eine fremde Gegenstelle.
- **Neutral.** Die Library ist an kein Projekt, keinen Kunden und keine Firma gebunden (siehe Abschnitt 4).
- **Vorführbar.** Am Ende gibt es zwei kleine Beispielprogramme (Client und Server), die gegeneinander laufen, und eine mit Doxygen erzeugte API-Doku.

## 2. Was zu lesen ist (in dieser Reihenfolge)

| Nr. | Datei | Wozu |
| --- | --- | --- |
| 1 | `CLAUDE.md` | Build, Architektur, Code-Regeln |
| 2 | `docs/01_IEC104_verstehen.md` | Protokollgrundlagen |
| 3 | `docs/02_Konzept_IEC104_Library.md` | **maßgeblich**: Sprache, Schichten, Parameter, TLS, Laufzeit, Tests |
| 4 | `docs/03_Recherchebericht_IEC104_im_Detail.md` | Belege, Werte, Interop-Fallstricke. Rust-Aussagen sind überholt |
| 5 | `folien/Konzept_IEC104_Library_Folien.html` | Kurzfassung des Konzepts |
| 6 | `include/`, `src/`, `tests/`, `fuzz/` | vorhandener Code |
| 7 | `tasks/TASK-001_codec.md` | nächste Umsetzungsaufgabe |

## 3. Was zu analysieren ist

Vor dem ersten neuen Code eine Analyse schreiben: `docs/04_Analyse.md` (Deutsch, knapp, Tabellen statt Fließtext). Inhalt:

1. **Widersprüche zwischen den Unterlagen.** Mindestens: Rust (Recherchebericht) gegenüber C++17 (Konzept), rustls gegenüber OpenSSL, Parameter mit abweichenden Defaults oder Bereichen. Je Punkt: Fundstelle, Widerspruch, Vorschlag.
2. **Offene Normfragen.** Alles, was laut Unterlagen nur aus Sekundärquellen belegt oder ausdrücklich offen ist (z. B. Zustandsverhalten Abschnitt 5, COT/Typ-Matrix 9.5, Redundanz Abschnitt 10, Cipher-Tabelle IEC 62351-3). Je Punkt: betroffenes Modul, Auswirkung, vorläufige Annahme im Code.
3. **Abgleich vorhandener Code gegen Konzept.** Was passt, was fehlt, was muss angepasst werden (inkl. Kommentarstandard aus Abschnitt 5).
4. **Vorschlag Aufgabenreihe** TASK-002 ff. bis zur vorführbaren Library, je Aufgabe ein Satz Ziel und grobe Größe (S/M/L). Erwartete Reihenfolge: codec (TASK-001) → apci-Zustandsmaschine → session (GA, Uhrzeit, Befehle, Pufferung) → transport TCP → Beispielprogramme → TLS → Redundanzgruppen → Interop-Tests → Doxygen.
5. **Fragen an den Auftraggeber**, die eine Entscheidung brauchen.

Danach anhalten und auf Freigabe warten. Erst dann TASK-001 beginnen.

## 4. Neutralitätsregel (verbindlich)

Im erzeugten Code darf **kein Hinweis auf Kunden, Firmen oder Projekte** stehen. Das gilt für:

- Quellcode, Kommentare, Bezeichner, Namespaces, Dateinamen
- Tests, Testnamen, Testdaten, Fuzz-Korpus
- CMake, Skripte, Doxygen-Texte, Beispielprogramme
- Commit-Nachrichten

Konkret:

- Keine Namen von Netzbetreibern, Herstellern, Auftraggebern, Personen, internen Produkten oder Projekten, auch nicht als Beispiel oder in Kommentaren wie „Wert von XY“.
- Abweichungen von Gegenstellen werden **allgemein** beschrieben: „some peers use t1 = 250 s“, „field devices that send local time with the SU bit“. Testprofile heißen neutral nach ihrer Eigenschaft (`long_timeout_profile`, `local_time_su_profile`, `select_before_operate_profile`), nicht nach Firmen.
- Erlaubt sind Normen und Gremien (IEC, IANA, RFC) sowie die technisch genutzten Werkzeuge und Bibliotheken (OpenSSL, GoogleTest, libFuzzer, Wireshark als Quelle für Referenz-Hexdumps, lib60870 nur als externe Testgegenstelle).
- `docs/` und `folien/` sind ausgenommen. Aus ihnen werden aber keine Namen in Code oder Kommentare übernommen.
- `CLAUDE.md` und `README.md` enthalten noch Bezüge auf Projekt und Firma. Beide neutral umformulieren.

**Prüfung:** Lege `tools/check_neutral.sh` an. Das Skript liest Suchbegriffe zeilenweise aus `${IEC104_FORBIDDEN_TERMS:-$HOME/.config/iec104/forbidden_terms.txt}` (liegt **außerhalb** des Repos, wird nicht committet), durchsucht alle Dateien außer `docs/`, `folien/`, `build/` und `.git/` ohne Beachtung der Groß-/Kleinschreibung und endet mit Fehlercode, wenn etwas gefunden wird. Fehlt die Begriffsdatei, Hinweis ausgeben und mit Fehlercode enden. Vor jedem Commit ausführen. Die Begriffsdatei pflegt der Auftraggeber.

## 5. Kommentarstandard (Englisch, ausführlich)

Der Code ist **ausführlich auf Englisch** zu kommentieren. Ziel: Wer IEC 104 nicht kennt, versteht den Code aus den Kommentaren.

**Jede Datei** beginnt mit einem Kopfblock:

```cpp
/**
 * @file apci.hpp
 * @brief APCI encoding and decoding (start octet, length, control field).
 *
 * What this file does, which part of IEC 60870-5-104 it implements
 * (with clause numbers where known), and what it deliberately does not do.
 * Layer: codec (pure functions, no I/O, no allocation).
 */
```

**Jede öffentliche oder modulweit genutzte Funktion, Struktur und Konstante** bekommt einen Doxygen-Kommentar:

- `@brief` in einem Satz.
- Ausführliche Beschreibung: was, warum, welcher Normabschnitt.
- `@param` und `@return` mit allen möglichen Statuscodes und wann sie auftreten.
- `@note` für Sonderfälle, Toleranzen und Abweichungen von Gegenstellen (neutral formuliert).
- Bei Byte-Formaten ein Bit-Layout als ASCII-Diagramm und ein kurzes Hex-Beispiel.

Beispiel für ein Bit-Layout:

```cpp
/*
 * I-format control field (4 octets, little endian counters shifted by one):
 *
 *   octet 1:  N(S) bits 0..6  | 0      <- bit 0 = 0 marks I-format
 *   octet 2:  N(S) bits 7..14
 *   octet 3:  N(R) bits 0..6  | 0
 *   octet 4:  N(R) bits 7..14
 *
 * Example: N(S) = 2, N(R) = 3  ->  04 00 06 00
 */
```

**Im Funktionsrumpf** werden nicht offensichtliche Schritte begründet („why“, nicht „what“). Kein Kommentar wie `// increment i`.

**Normbezug:** Abschnittsnummern nur angeben, wenn sie in den Unterlagen belegt sind. Sonst `TODO(norm): <Thema>, clause unknown`. Nichts erfinden.

**Tests** erklären im Kommentar, welche Regel sie prüfen und woher die Referenzbytes stammen.

Der vorhandene Code (`src/codec/*`, `include/iec104/iec104.h`, Tests, Fuzzer) ist auf diesen Standard nachzuziehen. Das ist der erste Umsetzungsschritt nach der Freigabe der Analyse, noch vor TASK-001.

## 6. Ergänzung für CLAUDE.md

Folgende Regeln in `CLAUDE.md` aufnehmen (Abschnitt „Code-Regeln“):

- Exemplarische Library: Lesbarkeit und Nachvollziehbarkeit vor Performance.
- Kommentarstandard nach `tasks/TASK-000_Auftrag.md`, Abschnitt 5.
- Neutralitätsregel nach `tasks/TASK-000_Auftrag.md`, Abschnitt 4; `tools/check_neutral.sh` vor jedem Commit.

## Definition of Done

- [x] `docs/04_Analyse.md` liegt vor, mit allen fünf Punkten aus Abschnitt 3.
- [x] Freigabe der Analyse durch den Auftraggeber.
- [x] `tools/check_neutral.sh` vorhanden und läuft ohne Treffer.
- [x] `CLAUDE.md` und `README.md` neutral und um Abschnitt 6 ergänzt.
- [x] Vorhandener Code nach Kommentarstandard überarbeitet, Build und Tests (`debug`, `asan`) weiter grün.
- [x] Commit ohne Bezug auf Kunden, Firma oder Projekt in Code und Nachricht.

Hinweise zum Abschluss:

- Begriffsliste `~/.config/iec104/forbidden_terms.txt` liegt als Entwurf vor; Prüfung durch den Auftraggeber offen.
- Der gepushte Commit `cbee03d` enthält noch eine Co-Author-Zeile (Entscheidung F6: bleibt).
- Alle Dateien einmal mit `.clang-format` formatiert (vorher 51 Abweichungen).
- Neue Tests gegen den Mitschnitt `tests/data/lib60870_capture.txt`, 27 Tests grün in `debug` und `asan`.
