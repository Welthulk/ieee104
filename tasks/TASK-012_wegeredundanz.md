# TASK-012: Wegeredundanz der Leitstelle

Status: in Arbeit; Entscheidungen D1–D7 am 27.11.2026 wie empfohlen bestätigt · Module: `src/session`, `src/api`, `src/transport`, `include`, `tools/selftest`, `examples/julia`, Doku · Zielversion 1.0.0

## 1. Ziel

Die Leitstelle (Client) ist über zwei Wege mit zwei PAs verbunden. Beide Verbindungen laufen (STARTDT), die Anwendung bekommt die Daten genau eines Wegs. Fällt dieser Weg aus, übernimmt die Library ohne Zutun der Anwendung den anderen. Die Anwendung sieht weiter eine Verbindung; der Wechsel steht im Log. In der Test-GUI lässt sich ein Weg von Hand kappen oder automatisch zufällig stören. Das Verfahren ist dokumentiert.

## 2. Begriffe und Abgrenzung

| Begriff | Bedeutung |
| --- | --- |
| Kanalredundanz (vorhanden, TASK-008) | Verfahren der Norm, Abschnitt 10: mehrere Verbindungen zu **einer** Station bilden eine Redundanzgruppe, genau eine ist gestartet. Bei uns auf der Stationsseite (`iec104_server_add_group`, E10, N7) |
| Wegeredundanz (neu) | Zwei Wege zu **zwei** Stationen (PAs) mit demselben Prozess. **Beide** gestartet, beide liefern. Kein Verfahren der Norm: Jeder Weg ist für seine PA eine gewöhnliche Einzelverbindung |
| führender Weg | der Weg, dessen Daten an die Anwendung gehen und über den Befehle laufen |
| mitlaufender Weg | gestartet, empfängt, wird überwacht; seine Daten werden nicht zugestellt, aber für den Nachlauf gemerkt |
| Umschalten | der mitlaufende Weg wird führend |
| Nachlauf | beim Umschalten nachgelieferte Meldungen des neuen führenden Wegs aus der Zeit, in der der Ausfall noch nicht erkannt war |

Voraussetzung an der Gegenseite: Hängen beide Wege an derselben Station, muss sie beide Verbindungen als getrennte Redundanzgruppen führen, sonst übernimmt das zweite STARTDT (E10).

## 3. Ist-Stand

| Punkt | Stand | Fundstelle |
| --- | --- | --- |
| Client | genau eine Verbindung, Kennung 1; zweites `connect` gibt `WRONG_STATE` | `src/api/client.cpp` (`links` mit höchstens einem Eintrag) |
| Poll-Schleife | bedient schon mehrere Links mit Timern | `api::poll_links`, vom Server genutzt |
| Wiederverbinden | gibt es nicht; die Anwendung ruft `connect` neu | – |
| Befehlsverfolgung | ein `CommandTracker` je Client; bei Abbruch Ergebnis `CONNECTION_LOST` | `src/session/command_tracker.*` |
| Ausfallerkennung | TCP-Ende sofort; stiller Ausfall über t1 (15 s) nach unquittiertem Rahmen, im Leerlauf TESTFR nach t3 (20 s) | `src/apci/connection.cpp` |
| Verbindungsaufbau | `connect` nicht blockierend, Namensauflösung blockiert | `src/transport/tcp.cpp` |
| Störung zum Testen | nur `close()`; „stumm“ (Kabel gezogen) nicht nachstellbar | `src/transport/link.hpp` |
| Umschalten im Test | Selbsttest schaltet von außen mit zwei Client-Handles um | `tools/selftest/selftest_client.cpp` (`--switchover`) |
| Rückruf-Struktur | `iec104_callbacks` wird kopiert; ein neues Feld bräche das ABI | `include/iec104/iec104.h` |

## 4. Analyse

### 4.1 Erkennungszeit

| Ausfall | Erkannt nach | Standardwerte |
| --- | --- | --- |
| TCP wird beendet (FIN, RST), PA-Prozess endet | sofort | – |
| Weg stumm, eigene Rahmen unquittiert | t1 | 15 s |
| Weg stumm, Leerlauf | t3 + t1 | 35 s |
| PA sendet Testrahmen, aber keine Daten mehr | gar nicht (APCI lebt) | offen, O2 |

### 4.2 Kernproblem: die Lücke beim Umschalten

```
Zeit ──────────────►   T (Weg 1 fällt stumm aus)        T+35 s (erkannt, Umschalten)
Weg 1 (führend)   e1 e2 │
Weg 2 (mitlaufend) e1 e2 │ e3 e4 e5 ............................│ e6 e7
Anwendung          e1 e2 │        ? ? ?                         │ e6 e7
```

Die Meldungen e3–e5 kamen nur über Weg 2 und wurden als „mitlaufend“ verworfen. Ohne Gegenmaßnahme fehlen sie der Anwendung, und ihr Abbild ist bis zur nächsten Änderung falsch. Das widerspricht „von außen merkt man nichts“.

### 4.3 Varianten

| Variante | Verhalten beim Umschalten | Verlust | Doppelte | Aufwand |
| --- | --- | --- | --- | --- |
| V1 nur umschalten | ab jetzt Weg 2 | Meldungen der Lücke; Abbild veraltet | nein | klein |
| V2 Abgleich | V1 + Generalabfrage auf dem neuen Weg | Zwischenzustände und Zeitmarken der Lücke | nein (GA-Werte mit Ursache 20) | klein |
| V3 Nachlauf + Abgleich | Meldungen des mitlaufenden Wegs werden für ein Zeitfenster gemerkt und beim Umschalten nachgeliefert, danach GA | keiner, solange das Fenster die Erkennungszeit deckt | möglich, auf Sekundenbruchteile um den Ausfall begrenzt | mittel |

Empfehlung: **V3**. Gleiche Linie wie auf der Stationsseite (N7): doppelt möglich, kein Verlust.

### 4.4 Nachlauf im Einzelnen

- Je mitlaufendem Weg ein Ringpuffer der empfangenen Objekte in Melderichtung mit Empfangszeit; begrenzt durch Zeitfenster (Standard t1 + t3 + 5 s) und Anzahl (Standard 10000).
- Je Datenpunkt merkt sich die Library das zuletzt zugestellte Objekt mit Empfangszeit τ.
- Beim Umschalten je Datenpunkt: Im Puffer des neuen Wegs das Gegenstück zum zuletzt zugestellten Objekt suchen (gleicher Wert und gleiche Qualität, Empfangszeit am nächsten an τ innerhalb der Toleranz J, Standard 500 ms). Alles danach wird in Empfangsreihenfolge zugestellt. Ohne Gegenstück: alles ab τ − J. Wurde für den Punkt noch nie etwas zugestellt: alles aus dem Puffer.
- Reihenfolge über alle Punkte: nach Empfangszeit.
- Danach Generalabfrage auf dem neuen führenden Weg (abschaltbar). Sie gleicht ab, was der Nachlauf nicht fassen kann: Unterschiede der beiden PAs, Meldungen älter als das Fenster.
- Grenzen (zu dokumentieren): Ein Wert, der innerhalb von J wechselt und zurückkehrt, kann doppelt kommen oder fehlen; Zeitmarken stammen von der jeweiligen PA und können sich um Millisekunden unterscheiden; Zählwerte tragen die Folgenummer der jeweiligen PA.

### 4.5 Steuerrichtung

| Thema | Analyse |
| --- | --- |
| Befehle | nur über den führenden Weg. Automatisches Wiederholen nach dem Umschalten ist gefährlich: Ein Befehl ohne Bestätigung kann schon ausgeführt sein (Stufenbefehl „höher“ käme zweimal) |
| Select/Execute | die Auswahl gilt an der Station je Verbindung; nach dem Umschalten ist sie verfallen |
| Abfragen der Anwendung (GA, Zähler, Lesen) | über den führenden Weg; Antworten von dort |
| Uhrzeit | beide PAs haben eigene Uhren: an alle gestarteten Wege |
| STARTDT/STOPDT | gilt für alle Wege; die Library merkt sich den Sollzustand und stellt ihn nach jedem Wiederverbinden her |

## 5. Verfahren (Soll)

| Nr. | Regel |
| --- | --- |
| R1 | Die Library baut alle Wege auf und hält sie; ein verlorener Weg wird im Abstand `reconnect_interval` neu aufgebaut, unbegrenzt |
| R2 | Führend wird der erste Weg, der „gestartet“ erreicht |
| R3 | Verlässt der führende Weg „gestartet“ (Abbruch, t1, STOPDT-Bestätigung, TLS-Fehler), wird sofort ein anderer gestarteter Weg führend: Nachlauf (4.4), dann Generalabfrage |
| R4 | Kein gestarteter Weg: logischer Zustand „verbinde“, Warnung im Log, offene Befehle enden mit `CONNECTION_LOST`; sobald ein Weg startet, wird er führend und fragt ab |
| R5 | Ein zurückgekehrter Weg läuft mit; zurückgeschaltet wird nicht (Option `prefer_first_path`) |
| R6 | Zustellung an die Anwendung nur vom führenden Weg; `on_state` meldet den logischen Zustand mit Kennung 1 wie bisher |
| R7 | Befehle und Abfragen über den führenden Weg; kein Wiederholen; Uhrzeit an alle Wege |
| R8 | Log: jeder Wechsel mit Grund („path 2 leading, path 1 closed: t1 timeout“), Rückkehr eines Wegs, Anzahl nachgelieferter und unterdrückter Objekte, „all paths lost“. Rahmen-Trace mit der Wegnummer als Verbindungskennung |
| R9 | Abfragbar je Weg: Zustand, führend ja/nein, Zähler (Wechsel, Wiederverbindungen, verworfen, nachgeliefert, unterdrückt), Link-Statistik |
| R10 | Client mit einem Weg (`iec104_client_connect`) verhält sich unverändert |
| R11 | Testhilfe: einen Weg trennen oder stumm schalten und wieder freigeben |

## 6. Entscheidungen (bestätigt am 27.11.2026, jeweils wie empfohlen)

| Nr. | Frage | Empfehlung |
| --- | --- | --- |
| D1 | Liefern beide PAs dieselben Datenpunkte unter derselben gemeinsamen Adresse und denselben IOAs? | ja, als Voraussetzung. Eine Umsetztabelle je Weg wäre eine eigene Aufgabe |
| D2 | Wie streng ist „transparent“? V1, V2 oder V3 (4.3) | V3 |
| D3 | Offener Befehl beim Umschalten | nicht wiederholen, Ergebnis `CONNECTION_LOST`; die Anwendung entscheidet |
| D4 | Zurückschalten, wenn der erste Weg wieder da ist | nein, als Option vorsehen |
| D5 | Sichtbarkeit des Wechsels | Log und Abfrage; zusätzlich ein eigener Rückruf über `iec104_client_set_path_callback` (ohne Änderung von `iec104_callbacks`) |
| D6 | Beide Wege weg | weiter versuchen, Zustand „verbinde“; Ende nur durch `iec104_client_close` |
| D7 | Anzahl Wege | Schnittstelle bis 4, getestet mit 2 und 3 |

Fragen an den Betreiber der PAs. Laut Auftraggeber keine Vorbedingung: Die Library bietet das Verfahren an, Anpassungen sind später möglich. Die Fragen ändern nur Parameter, nicht das Verfahren:

| Nr. | Frage | Folge |
| --- | --- | --- |
| K1 | Senden beide PAs gleichzeitig (heiß/heiß), oder liefert die zweite erst nach Ausfall der ersten? | Liefert sie nicht, ist der Nachlauf leer; dann trägt die Generalabfrage nach dem Umschalten |
| K2 | Dürfen beide PAs gleichzeitig eine gestartete Verbindung derselben Leitstelle haben? | sonst ist es Kanalredundanz, nicht dieses Verfahren |
| K3 | Stammen Zeitmarken vom Prozess (gleich auf beiden PAs) oder von der PA? | Genauigkeit der Doppel-Erkennung |
| K4 | Führt jede PA Befehle aus, oder nur eine? | Bedeutung von R7 |

## 7. Schnittstelle (Entwurf)

```c
#define IEC104_MAX_PATHS 4

typedef struct iec104_path_config {
    char host[256];
    uint16_t port;
    char tls_peer_name[256];        /* "" = wie in iec104_tls_config */
    char tls_peer_fingerprint[128]; /* "" = wie in iec104_tls_config */
} iec104_path_config;

typedef struct iec104_path_options {
    uint32_t reconnect_interval_ms;  /* 5000 */
    uint32_t replay_window_ms;       /* 0 = t1 + t3 + 5 s */
    uint32_t replay_max_objects;     /* 10000 */
    uint32_t replay_tolerance_ms;    /* 500 */
    int interrogation_after_switch;  /* 1 */
    int prefer_first_path;           /* 0 */
} iec104_path_options;

iec104_status iec104_client_connect_paths(iec104_client*, const iec104_path_config* paths,
                                          size_t count, const iec104_path_options* options);
uint32_t      iec104_client_leading_path(const iec104_client*);          /* 0 = keiner */
iec104_status iec104_client_get_path_info(const iec104_client*, uint32_t path,
                                          iec104_path_info* out);
void          iec104_client_set_path_callback(iec104_client*, iec104_path_event_fn, void* user);
iec104_status iec104_client_disturb_path(iec104_client*, uint32_t path, iec104_path_fault fault);
                                         /* Testhilfe: CLOSE, SILENT, RESTORE */
```

Konfigurationsdatei: `path = host:port` (mehrfach), `path_reconnect`, `path_replay_window`, `path_replay_tolerance`, `path_interrogation`, `path_prefer_first`. Julia: dieselben Funktionen in `IEC104.jl`.

## 8. Umsetzung

| Schicht | Datei | Inhalt |
| --- | --- | --- |
| session | `src/session/path_selector.{hpp,cpp}` (neu) | ohne I/O: Zustände der Wege, Wahl des führenden Wegs, Puffer je Weg, zuletzt Zugestelltes je Punkt, Nachlauf; Eingang: Ereignisse mit Zeit, Ausgang: Zustellungen und Meldungen |
| transport | `src/transport/link.*` | Testhilfe „stumm“: liest und schreibt nicht mehr |
| api | `src/api/client.cpp` | mehrere Links (Kennung = Wegnummer), Wiederverbinden mit Timer in der Poll-Schleife, Weiterleitung über den Selector, Befehle über den führenden Weg, logischer Zustand, Log |
| api | `src/api/config.cpp` | Schlüssel `path…` |
| Header | `include/iec104/iec104.h` | Abschnitt 7, Doxygen |
| Werkzeuge | `tools/selftest/*` | Client `--path` (mehrfach), `--fault-every`, Reihenprüfung über Wegausfälle |

## 9. Tests

| Art | Inhalt |
| --- | --- |
| Unit, Selector | Wahl des führenden Wegs; Umschalten bei jedem Grund; Nachlauf-Fälle als Folgen (kein Gegenstück, Gegenstück vor/nach τ, Wiederholungen a-b-a, Fenster abgelaufen, Puffer voll); Rückkehr; Flattern; kein Weg; drei Wege |
| TCP, zwei Server auf 127.0.0.1 | Reihe über Trennen und über „stumm“ (kurze Timer): kein Wert fehlt, Reihenfolge stimmt, Doppelte gezählt; Befehl offen beim Umschalten; beide Wege weg und wieder da; TLS mit zwei Zertifikaten; Client mit einem Weg unverändert |
| Selbsttest | `SelfTest.PathRedundancy`: 300 Werte, Störungen in der Mitte, Trennen und stumm |
| Fuzz | `fuzz_paths`: zufällige Ereignisfolgen gegen den Selector; Invarianten: höchstens ein führender Weg, Speicher begrenzt, kein Objekt älter als das zuletzt zugestellte desselben Punkts |
| GUI | Abschnitt 10; `test_gui.sh`, `test_page.js` und Selbsttest der GUI erweitert |

## 10. Test-GUI

| Teil | Inhalt |
| --- | --- |
| Zwei Teststationen | „Station A“ und „Station B“ mit denselben Datenpunkten; eine Wertänderung geht an beide (Abweichung absichtlich einstellbar: nur A, nur B) |
| Prüfstand | zweite Adresse/Port („Weg 2“); Tabelle der Wege: Zustand, führend/mitlaufend, Zähler |
| Von Hand | je Weg „trennen“, „stumm schalten“, „freigeben“ |
| Automatisch | „zufällig stören“: Abstand von–bis, Art zufällig, nie beide zugleich (abschaltbar); Startwert des Zufalls im Protokoll, damit ein Lauf wiederholbar ist |
| Nachweis | Reihenprüfung: Die Stationen senden eine laufende Zahl; die GUI zeigt gesendet, zugestellt, fehlend, doppelt und die Zahl der Umschaltungen |
| Protokoll | Umschalten, Nachlauf und Abgleich als erklärte Ereignisse; Rahmen mit „Weg 1“/„Weg 2“ |
| Selbsttest der GUI | neue Schritte: Umschalten bei Trennen, Umschalten bei stummem Weg, beide Wege weg, Befehl beim Umschalten |

## 11. Dokumentation

| Datei | Inhalt |
| --- | --- |
| `docs/02` | Abschnitt Redundanz: Wegeredundanz neben Kanalredundanz, Ablaufbild |
| `docs/05` | E11 Verfahren (führender Weg, Nachlauf, Abgleich), E12 Befehle beim Umschalten; Hinweis: kein Verfahren der Norm |
| `docs/06` | Kapitel „Zwei Wege“: Beispiel, Parameter, Grenzen, Wahl der Timer |
| `docs/07`, `docs/12` | Bedienung in der GUI, Testblock |
| `docs/10` | Weg, führend, mitlaufend, Umschalten, Nachlauf |
| README, CHANGELOG (kurz), `intern/CHANGELOG_fein.md` | Feature |

## 12. Definition of Done

- [ ] Entscheidungen D1–D7 bestätigt und in `docs/05` festgehalten.
- [ ] Selector mit Unit-Tests; Fuzzer `fuzz_paths` 10 min ohne Befund.
- [ ] TCP-Tests: Reihe von 1000 Werten mit mindestens 10 Störungen (Trennen und stumm): kein Wert fehlt, Reihenfolge stimmt, Doppelte nur innerhalb der Toleranz.
- [ ] Befehl beim Umschalten endet mit `CONNECTION_LOST`, nichts wird wiederholt.
- [ ] Client mit einem Weg: alle bisherigen Tests unverändert grün.
- [ ] Selbsttest-Programme und `SelfTest.PathRedundancy`.
- [ ] Test-GUI nach Abschnitt 10; Seitentest in gjs und WebKit; Selbsttest der GUI grün.
- [ ] Doku nach Abschnitt 11; Doxygen ohne Warnung; Interoperabilitätsliste aktuell.
- [ ] `debug`, `asan` grün, `clang` ohne Befunde; unabhängige Prüfung.
- [ ] Version 1.0.0: SONAME wird `libiec104.so.1` (CMake SOVERSION = Hauptversion); Namen nachziehen in `examples/julia/IEC104.jl`, `tools/make_package.sh`, `packaging/*`, `tests/gui/*.sh`, `tools/gui_screenshot.sh`, README, `docs/06`, `docs/07`, CLAUDE.md.
- [ ] Lieferung 1.0.0.

## 13. Reihenfolge

| Schritt | Inhalt |
| --- | --- |
| 1 | Selector ohne I/O mit Tests (das Verfahren steht damit fest) |
| 2 | Client: mehrere Wege, Wiederverbinden, Weiterleitung, Log, Abfragen; TCP-Tests |
| 3 | Testhilfe „stumm“, Selbsttest-Programme, Fuzzer |
| 4 | Julia-Anbindung, Test-GUI mit zwei Stationen und Störungen |
| 5 | Doku, Prüfung, Lieferung |

## 14. Offen und Risiken

| Nr. | Thema | Stand |
| --- | --- | --- |
| O1 | Namensauflösung blockiert beim Wiederverbinden die Poll-Schleife | IP-Adressen empfehlen; nicht blockierende Auflösung später |
| O2 | PA lebt (Testrahmen), liefert aber keine Daten | nicht erkennbar; Datenwächter („mitlaufender Weg liefert, führender nicht“) als spätere Option |
| O3 | Beide PAs unterscheiden sich dauerhaft in einem Wert | nach dem Umschalten gilt der Wert des neuen Wegs; ein Vergleich beider Wege im Betrieb ist nicht Teil dieser Aufgabe |
| O4 | Speicher des Nachlaufs bei Meldeschwall | durch Anzahl begrenzt; Überlauf wird gezählt und geloggt |
| O5 | Kanal- und Wegeredundanz zugleich (je PA mehrere Verbindungen) | nicht Teil dieser Aufgabe |
