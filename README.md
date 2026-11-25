# IEC 60870-5-104 Library

Library für IEC 60870-5-104 mit Client (steuernde Station) und Server (gesteuerte Station). Der Kern ist in C++17 geschrieben, nach außen gibt es eine stabile C-Schnittstelle (`include/iec104/iec104.h`). Ziel ist eine normgerechte, gut lesbare und vollständig getestete Implementierung; jede Protokollregel im Code nennt ihre Quelle.

Stand: 25.11.2026, Version 0.1.0, Linux x86-64.

## Stand

| Modul | Inhalt |
| --- | --- |
| `codec` | APCI (I/S/U-Format), ASDU-Kopf mit parametrierbaren Feldlängen, alle Informationselemente, Typtabelle mit 59 Typkennungen, vollständige ASDUs mit SQ = 0 und SQ = 1, Aufteilen langer Objektlisten nach maximaler ASDU-Länge, strikter und toleranter Modus, CP56Time2a |
| `apci` | Zustandsmaschine der Verbindung ohne eigenes I/O: STARTDT/STOPDT/TESTFR, Folgenummern, Quittierung nach k und w, Timer t1–t3 |
| `transport` | TCP für Linux (nicht blockierend, `poll`), Verbindungsaufbau mit t0 |
| C-API | Server und Client mit Handles, konfigurierbaren Ports und Timern, Callbacks für empfangene ASDUs und Zustandswechsel |
| Selbsttest | `iec104_selftest_server` und `iec104_selftest_client` für automatische und manuelle Tests |
| in Arbeit | Session-Schicht: Generalabfrage, Uhrzeitsynchronisation, Zählerabfrage, Befehle; danach TLS (OpenSSL 3.x) und Redundanzgruppen |

## Unterlagen

| Datei | Inhalt |
| --- | --- |
| `docs/01_IEC104_verstehen.md` | Einführung in das Protokoll |
| `docs/02_Konzept_IEC104_Library.md` | Konzept: Architektur, Parameter, TLS, Laufzeitmodell, Testkonzept |
| `docs/03_Recherchebericht_IEC104_im_Detail.md` | Recherche mit Quellen: Norm, Praxis, Interop-Fallstricke, Sicherheit |
| `docs/05_Quellen_APCI.md` | Öffentliche Belege für die Regeln der Verbindungssteuerung |
| `docs/06_Selbsttest.md` | Selbsttest-Programme und manuelle Tests: Aufruf, Testfälle, Optionen |
| `docs/07_Normfragen_und_Festlegungen.md` | Offene Normfragen (N1–N13) und technische Festlegungen (E1–E6), auf die der Code verweist |
| `folien/Konzept_IEC104_Library_Folien.html` | Folien zum Konzept, im Browser öffnen |

## Verzeichnisse

| Ordner | Inhalt |
| --- | --- |
| `include/iec104/` | öffentlicher C-Header, die einzige Schnittstelle für Anwender |
| `src/codec/` | reine Funktionen auf Byte-Puffern, kein I/O, keine Allokation |
| `src/apci/` | Zustandsmaschine: Bytes und Zeit rein, Bytes und Ereignisse raus |
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
```

Die Library baut mit `-Werror`, ohne Exceptions und ohne RTTI. Formatierung nach `.clang-format`.

## Selbsttest

```bash
build/debug/iec104_selftest_server --port 2404 --count 100
build/debug/iec104_selftest_client --host 127.0.0.1 --port 2404 --expect 100 --count 10 --idle 25
```

Der Client meldet jeden Schritt mit `ok` oder `FAILED` und endet mit `result: PASSED` (Exit-Code 0) oder `result: FAILED` (Exit-Code 1). `ctest` lässt beide Programme automatisch über Loopback gegeneinander laufen. Testfälle gegen fremde Geräte, Fehlerfälle und alle Optionen: `docs/06_Selbsttest.md`.

## Verwendung der C-API

```c
#include <iec104/iec104.h>
#include <stdio.h>

static void on_asdu(void* user, uint32_t connection, const uint8_t* asdu, size_t length) {
    printf("connection %u: ASDU type %u, %zu octets\n", connection, asdu[0], length);
}

static void on_state(void* user, uint32_t connection, iec104_link_state state,
                     iec104_close_reason reason) {
    printf("connection %u: %s\n", connection, iec104_link_state_string(state));
}

int main(void) {
    iec104_link_config config;
    iec104_link_config_init(&config);          /* Normwerte: t0..t3 = 30/15/10/20 s, k = 12, w = 8 */

    iec104_callbacks callbacks = {NULL, on_asdu, on_state};
    iec104_server* server = NULL;
    if (iec104_server_create(&config, &callbacks, &server) != IEC104_OK ||
        iec104_server_listen(server, NULL, IEC104_DEFAULT_PORT) != IEC104_OK) {
        return 1;
    }
    for (;;) {
        iec104_server_poll(server, 100);       /* Empfang, Timer, Senden und Callbacks */
        /* iec104_server_send_asdu(server, 0, asdu, length) sendet an alle gestarteten Verbindungen */
    }
}
```

Der Client funktioniert entsprechend: `iec104_client_create`, `iec104_client_connect(client, host, port)`, nach dem Zustand `IEC104_LINK_STOPPED` `iec104_client_start`, danach `iec104_client_send_asdu`. Alle Funktionen liefern einen `iec104_status`; `iec104_status_string()` gibt einen lesbaren Text dazu.

## Lizenz

Die Lizenztexte liegen in `LICENSES/`. Der öffentliche Header `include/iec104/iec104.h` steht unter Apache-2.0, alle übrigen Dateien sind proprietär. Jede Quelldatei nennt ihre Lizenz im SPDX-Kopf.

lib60870 wird ausschließlich als externe Gegenstelle für Tests verwendet; es wird weder gelinkt noch Code übernommen.
