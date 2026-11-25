# IEC 60870-5-104 Library

Library für IEC 60870-5-104 mit Client (steuernde Station) und Server (gesteuerte Station). Der Kern ist in C++17 geschrieben, nach außen gibt es eine stabile C-Schnittstelle (`include/iec104/iec104.h`). Ziel ist eine normgerechte, gut lesbare und vollständig getestete Implementierung; jede Protokollregel im Code nennt ihre Quelle.

Stand: 25.11.2026, Version 0.1.0, Linux x86-64.

## Stand

| Modul | Inhalt |
| --- | --- |
| `codec` | APCI (I/S/U-Format), ASDU-Kopf mit parametrierbaren Feldlängen, alle Informationselemente, Typtabelle mit 59 Typkennungen, vollständige ASDUs mit SQ = 0 und SQ = 1, Aufteilen langer Objektlisten nach maximaler ASDU-Länge, strikter und toleranter Modus, CP56Time2a |
| `apci` | Zustandsmaschine der Verbindung ohne eigenes I/O: STARTDT/STOPDT/TESTFR, Folgenummern, Quittierung nach k und w, Timer t1–t3 |
| `transport` | TCP für Linux (nicht blockierend, `poll`), Verbindungsaufbau mit t0 |
| `session` | Abläufe der gesteuerten Station: Prozessabbild, Generalabfrage (Station und Gruppen), Zählerabfrage, Lesebefehl, Testbefehl, Uhrzeit, Befehle mit Bestätigung und Beendigung, Ende der Initialisierung, negative Bestätigungen (Ursachen 44–47) |
| C-API | Server und Client mit Handles, konfigurierbaren Ports, Timern und ASDU-Profil; Werte statt Oktette (`iec104_object`), Callbacks für Objekte, Befehle und Zustandswechsel |
| Selbsttest | `iec104_selftest_server` und `iec104_selftest_client` für automatische und manuelle Tests |
| Befehle | Befehlsmodell je IOA (direkt oder Select/Execute mit Auswahl-Timeout), maximale Befehlsverzögerung für Befehle mit Zeitmarke, Befehlsverfolgung am Client mit Ergebnis je Befehl und getrennten Timeouts |
| in Arbeit | Pufferung, TLS (OpenSSL 3.x), Redundanzgruppen, Logging und Statistik |

## Unterlagen

| Datei | Inhalt |
| --- | --- |
| `docs/01_IEC104_verstehen.md` | Einführung in das Protokoll, Fragen an eine Gegenstelle |
| `docs/02_Technik_und_Quellen.md` | Architektur, Protokollaufbau, Parameter, Typkennungen, Elemente, Quellen |
| `docs/03_Quellen_APCI.md` | Öffentliche Belege für die Regeln der Verbindungssteuerung |
| `docs/04_Selbsttest.md` | Selbsttest-Programme und manuelle Tests: Aufruf, Testfälle, Optionen |
| `docs/05_Normfragen_und_Festlegungen.md` | Offene Normfragen (N1–N13) und technische Festlegungen (E1–E7), auf die der Code verweist |

## Verzeichnisse

| Ordner | Inhalt |
| --- | --- |
| `include/iec104/` | öffentlicher C-Header, die einzige Schnittstelle für Anwender |
| `src/codec/` | reine Funktionen auf Byte-Puffern, kein I/O, keine Allokation |
| `src/apci/` | Zustandsmaschine: Bytes und Zeit rein, Bytes und Ereignisse raus |
| `src/session/` | Anwendungsabläufe der gesteuerten Station, Prozessabbild |
| `src/transport/` | Sockets, Verbindungsaufbau, Zeitbasis |
| `src/api/` | Implementierung der C-Schnittstelle |
| `tests/` | Unit-Tests (GoogleTest), Referenzdaten unter `tests/data/` |
| `fuzz/` | libFuzzer-Ziele für Codec und Zustandsmaschine |
| `tools/selftest/` | Selbsttest-Programme und Testskript |
| `tools/` | Installation der Werkzeuge, Startdateien für den Fuzzer |

## Bauen und testen

Werkzeuge einmalig installieren (AlmaLinux 9): `./tools/setup_tools.sh`

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug    # Build und alle Tests
cmake --preset asan  && cmake --build --preset asan  && ctest --preset asan     # mit AddressSanitizer/UBSan
cmake --preset clang && cmake --build --preset clang                            # mit clang-tidy
```

Fuzzing (Clang):

```bash
cmake --preset fuzz && cmake --build --preset fuzz
tools/make_fuzz_seeds.sh
./build/fuzz/fuzz_codec -max_total_time=300 fuzz/corpus
./build/fuzz/fuzz_apci  -max_total_time=300 fuzz/corpus_apci
./build/fuzz/fuzz_session -max_total_time=300 fuzz/corpus_session
```

Die Library baut mit `-Werror`, ohne Exceptions und ohne RTTI. Formatierung nach `.clang-format`.

## Selbsttest

```bash
build/debug/iec104_selftest_server --port 2404 --count 100
build/debug/iec104_selftest_client --host 127.0.0.1 --port 2404 --idle 25
build/debug/iec104_selftest_client --host <Gerät> --foreign --ca <Adresse> --idle 60     # fremder Server
```

Der Client meldet jeden Schritt mit `ok` oder `FAILED` und endet mit `result: PASSED` (Exit-Code 0) oder `result: FAILED` (Exit-Code 1). `ctest` lässt beide Programme automatisch über Loopback gegeneinander laufen. Testfälle gegen fremde Geräte, Fehlerfälle und alle Optionen: `docs/04_Selbsttest.md`.

## Verwendung der C-API

Server mit Prozessabbild; Generalabfrage, Zählerabfrage und Lesebefehle beantwortet die Library selbst, Befehle entscheidet die Anwendung:

```c
#include <iec104/iec104.h>
#include <stdio.h>

static int on_command(void* user, uint32_t connection, const iec104_asdu_info* info,
                      const iec104_object* command) {
    printf("command type %u at IOA %u, value %g\n", info->type, command->ioa, command->value);
    return 1; /* 1 = annehmen (positive Bestätigung), 0 = ablehnen */
}

int main(void) {
    iec104_link_config config;
    iec104_link_config_init(&config); /* Normwerte: t0..t3 = 30/15/10/20 s, k = 12, w = 8 */

    iec104_callbacks callbacks = {0};
    callbacks.on_command = on_command;

    iec104_server* server = NULL;
    if (iec104_server_create(&config, &callbacks, &server) != IEC104_OK) {
        return 1;
    }
    iec104_object point = {0};
    point.type = 13;      /* M_ME_NC_1, Gleitkomma */
    point.ioa = 1001;
    point.value = 230.4;
    iec104_server_add_point(server, 1, &point, 0x0001); /* Station 1, Abfragegruppe 1 */

    if (iec104_server_listen(server, NULL, IEC104_DEFAULT_PORT) != IEC104_OK) {
        return 1;
    }
    for (;;) {
        iec104_server_poll(server, 100); /* Empfang, Timer, Antworten, Callbacks */
        /* Neue Werte: point.value = ...; iec104_server_update(server, 1, &point); (spontan) */
    }
}
```

Befehlspunkte legt der Server mit `iec104_server_add_command` fest (direkt oder Select/Execute); die maximale Befehlsverzögerung steht in `iec104_server_options`.

Der Client funktioniert entsprechend: `iec104_client_create`, `iec104_client_connect(client, host, port)`, nach dem Zustand `IEC104_LINK_STOPPED` `iec104_client_start`, danach zum Beispiel `iec104_client_interrogation(client, 1, 20)`; die Antworten kommen als Objekte über `on_object`. Befehle sendet `iec104_client_send_command` oder, zweistufig, `iec104_client_select_execute`; das Ergebnis jedes Befehls (ausgeführt, abgelehnt, keine Bestätigung, …) meldet `on_command_result`. Alle Funktionen liefern einen `iec104_status`; `iec104_status_string()` gibt einen lesbaren Text dazu.

## Lizenz

Die Lizenztexte liegen in `LICENSES/`. Der öffentliche Header `include/iec104/iec104.h` steht unter Apache-2.0, alle übrigen Dateien sind proprietär. Jede Quelldatei nennt ihre Lizenz im SPDX-Kopf.

lib60870 wird ausschließlich als externe Gegenstelle für Tests verwendet; es wird weder gelinkt noch Code übernommen.
