# Auftrag an Claude Code: Repo für die IEC-104-Library einrichten

Arbeitsverzeichnis: `/home/soptim.net/scud/src/spps/iee104` (Klon von `Welthulk/ieee104`, enthält bereits `docs/` und `folien/`).

## Was zu tun ist

1. Lege jede Datei aus dem Abschnitt **Dateien** genau mit dem angegebenen Pfad und Inhalt an. Vorhandene Dateien mit gleichem Pfad (`README.md`) werden ersetzt. `docs/` und `folien/` bleiben unverändert.
   - Liegt `setup_tools.sh` noch im Repo-Wurzelordner, lösche es dort; es gehört nach `tools/`.
   - `tools/setup_tools.sh` ausführbar machen: `chmod +x tools/setup_tools.sh`.
2. Bauen und testen:
   ```bash
   cmake --preset debug && cmake --build --preset debug && ctest --preset debug
   cmake --preset asan  && cmake --build --preset asan  && ctest --preset asan
   cmake --preset clang && cmake --build --preset clang
   cmake --preset fuzz  && cmake --build --preset fuzz
   mkdir -p fuzz/corpus && ./build/fuzz/fuzz_codec -max_total_time=120 fuzz/corpus
   ```
   Erwartung: 21 Tests grün in `debug` und `asan`, keine Warnungen, keine clang-tidy-Befunde, Fuzzer ohne Absturz.
   Schlägt etwas fehl, die Ursache beheben und kurz berichten, was geändert wurde. Keine Tests abschwächen.
3. Committen (nicht pushen):
   ```bash
   git add -A
   git commit -m "build: Projektgerüst, Codec-Start (APCI, ASDU-Kopf, CP56Time2a), CLAUDE.md, TASK-001"
   ```
4. Danach `CLAUDE.md` lesen und mit `tasks/TASK-001_codec.md` beginnen. Vor dem Start kurz den Plan nennen.

## Bereits geprüft

Der Code wurde vorab mit GCC 13 und Clang 18 (`-Werror`, ASan/UBSan) gebaut, alle 21 Tests liefen grün, clang-tidy ohne Befund, die Fuzz-Invarianten mit 3 Mio. Zufallseingaben ohne Fehler. Nicht geprüft: Build auf AlmaLinux 9 (GCC 11.5) und `find_package(GTest)` mit dem EPEL-Paket.

## Dateien

### `CLAUDE.md`

````markdown
# CLAUDE.md – Regeln für Claude Code in diesem Repo

IEC-60870-5-104-Library für die SpPS (SOPTIM). Kern C++17, stabile C-API, TLS über externe Library.

## Verbindliche Unterlagen

- `docs/02_Konzept_IEC104_Library.md` ist maßgeblich (Sprache, Architektur, Parameter, Tests).
- `docs/03_Recherchebericht_IEC104_im_Detail.md` liefert Belege und Werte. Seine Rust-Aussagen (rustls, `Strictness::…`, cargo-fuzz) sind überholt; Sprache ist C++17, TLS-Backend OpenSSL 3.x.
- `docs/01_IEC104_verstehen.md` ist die Einführung.
- Aufgaben stehen in `tasks/`. Immer nur eine Aufgabe bearbeiten, Definition of Done abhaken.

## Build und Test

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset asan  && cmake --build --preset asan  && ctest --preset asan    # vor jedem Commit
cmake --preset clang && cmake --build --preset clang                           # clang-tidy
cmake --preset fuzz  && cmake --build --preset fuzz
./build/fuzz/fuzz_codec -max_total_time=300
```

Zielplattform: AlmaLinux 9, GCC 11.5, Clang 22, CMake 3.31, OpenSSL 3.5, GoogleTest aus EPEL.
Werkzeuge installiert `tools/setup_tools.sh`.

## Architektur (Schichten ohne Abhängigkeit nach oben)

| Schicht | Ordner | Regel |
| --- | --- | --- |
| codec | `src/codec` | reine Funktionen auf Byte-Puffern, kein I/O, keine Allokation |
| apci | `src/apci` | Zustandsmaschine, sans-I/O: Bytes + Zeit rein, Bytes + Ereignisse raus |
| session | `src/session` | GA, Uhrzeit, Befehle, Pufferung, Redundanzgruppen |
| transport | `src/transport` | TCP/TLS, Event-Loop, austauschbares Backend |
| API | `include/iec104/iec104.h`, `src/api` | C-Schnittstelle |

## Code-Regeln

- C++17, keine Exceptions, kein RTTI im Library-Code (`-fno-exceptions -fno-rtti`). Fehler als `iec104_status`.
- Öffentliche Header: nur C, keine STL-Typen, alles mit `IEC104_API` exportiert.
- Normverhalten ist Default. Jede bekannte Abweichung einer Gegenstelle wird ein Parameter, nie ein Code-Zweig für einen Hersteller.
- Keine GPL-Abhängigkeit im Produkt. lib60870 nur als externe Testgegenstelle, nie linken, nie Code übernehmen.
- Werte, die aus der Norm stammen und im Repo nicht belegt sind, nicht raten. Stattdessen `// TODO(norm): …` mit Abschnittsangabe und in der Aufgabe als offen melden.
- Bezeichner und Code-Kommentare auf Englisch, Dokumentation unter `docs/` auf Deutsch.
- Formatierung mit `.clang-format`, keine Warnungen (Library baut mit `-Werror`).
- Jede neue Funktion bekommt Tests, möglichst mit Referenz-Hexdumps und Quelle im Kommentar.

## Git

- Kleine Commits, Nachricht auf Deutsch, Präfix nach Modul (`codec: …`, `apci: …`, `build: …`).
- Nicht pushen, ohne zu fragen.
````

### `README.md`

````markdown
# IEC 60870-5-104: Einführung, Recherche, Konzept und Library

Eigene IEC-104-Library (C++17-Kern, C-API, TLS über OpenSSL) für die SpPS und die Anbindung an die Prozessabkopplung (Aucoteam). Stand: 25.11.2026.

## Unterlagen

| Datei | Inhalt |
| --- | --- |
| `docs/01_IEC104_verstehen.md` | Einführung ins Protokoll, offene Fragen an Aucoteam |
| `docs/02_Konzept_IEC104_Library.md` | Konzept: Sprache, Architektur, Parameter, TLS, Laufzeit, Tests, offene Entscheidungen |
| `docs/03_Recherchebericht_IEC104_im_Detail.md` | Recherche: Norm, Praxis bei Netzbetreibern, Interop-Fallstricke, Sicherheit, Tests (mit Quellen) |
| `folien/Konzept_IEC104_Library_Folien.html` | Folien zum Konzept, im Browser öffnen (10 Folien, 1920×1080) |
| `CLAUDE.md` | Regeln für Claude Code |
| `tasks/` | Aufgaben, eine nach der anderen |

## Bauen

Werkzeuge einmalig installieren: `./tools/setup_tools.sh`

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --preset asan  && cmake --build --preset asan  && ctest --preset asan
cmake --preset fuzz  && cmake --build --preset fuzz  && ./build/fuzz/fuzz_codec -max_total_time=300
```

## Stand

| Modul | Stand |
| --- | --- |
| `codec` | APCI (I/S/U), ASDU-Kopf, IOA, CP56Time2a mit Tests. Informationselemente und ASDUs: `tasks/TASK-001_codec.md` |
| `apci`, `session`, `transport` | offen |
| C-API | Version und Statuscodes |
````

### `tasks/TASK-001_codec.md`

````markdown
# TASK-001: Codec vervollständigen (Informationselemente und ASDUs)

Status: offen · Modul: `src/codec` · Grundlage: `docs/02_Konzept` (Architektur, ASDU-Profil), `docs/03_Recherchebericht` (Typkennungen, COT, Elemente)

## Ausgangslage

Vorhanden und getestet: APCI (I/S/U, Folgenummern, Framing), ASDU-Kopf mit parametrierbaren Feldlängen, IOA, CP56Time2a.
Es fehlen die Informationselemente und das Kodieren und Dekodieren vollständiger ASDUs.

## Ziel

Jede Typkennung des Umfangs Version 1 lässt sich als komplette ASDU kodieren und dekodieren, mit SQ = 0 und SQ = 1, im IEC-104-Profil und in abweichenden Profilen.

## Umfang

1. **Informationselemente** (`src/codec/elements.hpp/.cpp`): SIQ, DIQ, QDS, VTI, NVA, SVA, IEEE-754-Float, BCR, BSI, SCO, DCO, RCO, QOS, QOC, QOI, QCC, QPM, QPA, QRP, COI, SEP/SPE/QDP (Schutz, Typen 38–40) sowie CP16Time2a, soweit für die Typen nötig. Qualitätsbits IV 0x80, NT 0x40, SB 0x20, BL 0x10, OV 0x01.
2. **Typtabelle** (`src/codec/type_table.hpp/.cpp`): je Typkennung Elementlänge, Zeitmarke ja/nein, Richtung. Typen: 1, 3, 5, 7, 9, 11, 13, 15, 20, 21, 30–40, 45–51, 58–64, 70, 100–107, 110–113.
3. **ASDU-Objekte** (`src/codec/asdu.hpp/.cpp`): eine ASDU als Kopf plus Liste von Objekten (IOA + Element + optionale Zeit). Kodieren und Dekodieren für SQ = 0 (je Objekt eine IOA) und SQ = 1 (eine Start-IOA, fortlaufend). Keine dynamische Allokation: Ausgabe in vom Aufrufer gestellte Puffer oder per Callback je Objekt.
4. **Aufteilen**: Kodierer, der eine Objektliste in mehrere ASDUs zerlegt, sodass `max_asdu_length` (Default 249, konfigurierbar) eingehalten wird.
5. **Strikt/tolerant**: Parameter `strict` im Profil. Strikt lehnt CP24-Typen (2, 4, 6, 8, 10, 12, 14, 16, 17, 18, 19) mit `IEC104_ERR_UNSUPPORTED` ab, tolerant dekodiert sie. Private Typen 136–255 werden roh (Bytes) an den Aufrufer gegeben.
6. **Fuzzing**: `fuzz/fuzz_codec.cpp` um vollständige ASDUs erweitern, Invariante Dekodieren → Kodieren → identische Bytes.

## Nicht Teil dieser Aufgabe

- Zuordnung „welche COT ist für welchen Typ erlaubt“ (Norm 9.5, Text liegt nicht vor). Nur als leere Tabelle mit `TODO(norm): 9.5` anlegen.
- APCI-Zustandsmaschine, k/w, Timer (TASK-002).
- Dateiübertragung 120–127, Security-Typen aus IEC TS 60870-5-7.

## Definition of Done

- [ ] Alle Typen aus Punkt 2 mit Roundtrip-Test (SQ = 0 und SQ = 1, wo zulässig).
- [ ] Je Typgruppe mindestens ein Referenz-Hexdump mit Quellenangabe im Kommentar (Wireshark-Dissector, Herstellerdoku). Keine selbst erfundenen „Referenzen“.
- [ ] Negativtests: zu kurze Puffer, Objektanzahl passt nicht zur Länge, unbekannte Typkennung, CP24 im strikten Modus.
- [ ] `ctest --preset debug` und `ctest --preset asan` grün, `clang`-Preset ohne clang-tidy-Befunde in neuem Code.
- [ ] Fuzzer läuft 10 Minuten ohne Befund.
- [ ] Offene Normfragen in diesem Dokument unter „Offen“ eingetragen.

## Offen

- (wird beim Bearbeiten gefüllt)
````

### `CMakeLists.txt`

````cmake
cmake_minimum_required(VERSION 3.21)

project(iec104
    VERSION 0.1.0
    DESCRIPTION "IEC 60870-5-104 library (C++17 core, C API)"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(IEC104_BUILD_TESTS          "Build unit tests (GoogleTest)"            ON)
option(IEC104_BUILD_FUZZERS        "Build libFuzzer targets (Clang only)"     OFF)
option(IEC104_ENABLE_SANITIZERS    "Build with ASan and UBSan"                OFF)
option(IEC104_WARNINGS_AS_ERRORS   "Treat warnings as errors (library code)"  ON)
option(IEC104_ENABLE_CLANG_TIDY    "Run clang-tidy while compiling"           OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Global instrumentation (applies to library, tests and fuzzers)
# ---------------------------------------------------------------------------
if(IEC104_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

if(IEC104_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "IEC104_BUILD_FUZZERS requires Clang (libFuzzer). Use the 'fuzz' preset.")
    endif()
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(IEC104_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    set(IEC104_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

# Warnings for our own targets
function(iec104_set_warnings target strict)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wcast-align -Wnull-dereference -Wdouble-promotion -Wformat=2)
    if(strict AND IEC104_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
add_library(iec104
    src/api/iec104.cpp
    src/codec/apci.cpp
    src/codec/asdu_header.cpp
    src/codec/cp56time2a.cpp)
add_library(iec104::iec104 ALIAS iec104)

target_include_directories(iec104
    PUBLIC  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_compile_definitions(iec104 PRIVATE IEC104_BUILDING_LIBRARY
    IEC104_VERSION_STRING="${PROJECT_VERSION}")

# Core rules: no exceptions, no RTTI (see docs/02_Konzept, Sprachwahl)
target_compile_options(iec104 PRIVATE -fno-exceptions -fno-rtti)
set_target_properties(iec104 PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
if(IEC104_ENABLE_CLANG_TIDY)
    set_target_properties(iec104 PROPERTIES CXX_CLANG_TIDY "${IEC104_CLANG_TIDY}")
endif()
iec104_set_warnings(iec104 TRUE)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(IEC104_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(iec104_tests
        tests/test_api.cpp
        tests/test_apci.cpp
        tests/test_asdu_header.cpp
        tests/test_cp56time2a.cpp)
    # Tests use the internal C++ headers directly (sans-I/O, no network)
    target_include_directories(iec104_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(iec104_tests PRIVATE iec104::iec104 GTest::gtest GTest::gtest_main)
    iec104_set_warnings(iec104_tests FALSE)
    gtest_discover_tests(iec104_tests)
endif()

# ---------------------------------------------------------------------------
# Fuzzers
# ---------------------------------------------------------------------------
if(IEC104_BUILD_FUZZERS)
    add_executable(fuzz_codec fuzz/fuzz_codec.cpp)
    target_include_directories(fuzz_codec PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(fuzz_codec PRIVATE iec104::iec104)
    target_link_options(fuzz_codec PRIVATE -fsanitize=fuzzer)
    iec104_set_warnings(fuzz_codec FALSE)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS iec104 EXPORT iec104Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
````

### `CMakePresets.json`

````json
{
  "version": 3,
  "cmakeMinimumRequired": { "major": 3, "minor": 21, "patch": 0 },
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}"
    },
    {
      "name": "debug",
      "displayName": "Debug (GCC)",
      "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" }
    },
    {
      "name": "release",
      "displayName": "Release (GCC)",
      "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
    },
    {
      "name": "asan",
      "displayName": "Debug + ASan/UBSan",
      "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "IEC104_ENABLE_SANITIZERS": "ON"
      }
    },
    {
      "name": "clang",
      "displayName": "Debug (Clang) + clang-tidy",
      "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_C_COMPILER": "clang",
        "CMAKE_CXX_COMPILER": "clang++",
        "IEC104_ENABLE_CLANG_TIDY": "ON"
      }
    },
    {
      "name": "fuzz",
      "displayName": "libFuzzer (Clang)",
      "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "CMAKE_C_COMPILER": "clang",
        "CMAKE_CXX_COMPILER": "clang++",
        "IEC104_BUILD_TESTS": "OFF",
        "IEC104_BUILD_FUZZERS": "ON"
      }
    }
  ],
  "buildPresets": [
    { "name": "debug",   "configurePreset": "debug" },
    { "name": "release", "configurePreset": "release" },
    { "name": "asan",    "configurePreset": "asan" },
    { "name": "clang",   "configurePreset": "clang" },
    { "name": "fuzz",    "configurePreset": "fuzz" }
  ],
  "testPresets": [
    { "name": "debug",   "configurePreset": "debug",   "output": { "outputOnFailure": true } },
    { "name": "release", "configurePreset": "release", "output": { "outputOnFailure": true } },
    { "name": "asan",    "configurePreset": "asan",    "output": { "outputOnFailure": true } },
    { "name": "clang",   "configurePreset": "clang",   "output": { "outputOnFailure": true } }
  ]
}
````

### `.clang-format`

````text
---
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 100
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
PointerAlignment: Left
NamespaceIndentation: None
SortIncludes: CaseSensitive
IncludeBlocks: Regroup
...
````

### `.clang-tidy`

````text
---
Checks: >
  bugprone-*,
  cert-*,
  clang-analyzer-*,
  cppcoreguidelines-*,
  misc-*,
  modernize-*,
  performance-*,
  portability-*,
  readability-*,
  -bugprone-easily-swappable-parameters,
  -cppcoreguidelines-avoid-magic-numbers,
  -cppcoreguidelines-macro-usage,
  -modernize-use-using,
  -performance-enum-size,
  -cppcoreguidelines-pro-bounds-pointer-arithmetic,
  -cppcoreguidelines-pro-bounds-constant-array-index,
  -misc-include-cleaner,
  -misc-non-private-member-variables-in-classes,
  -modernize-use-trailing-return-type,
  -readability-identifier-length,
  -readability-magic-numbers
WarningsAsErrors: ''
HeaderFilterRegex: '(include|src)/.*'
FormatStyle: file
...
````

### `.gitignore`

````text
build/
.cache/
compile_commands.json
CMakeUserPresets.json
.vscode/*
!.vscode/extensions.json
crash-*
leak-*
timeout-*
oom-*
fuzz/corpus/
*.swp
````

### `.vscode/extensions.json`

````json
{
  "recommendations": [
    "ms-vscode.cmake-tools",
    "ms-vscode.cpptools",
    "anthropic.claude-code"
  ]
}
````

### `include/iec104/iec104.h`

````cpp
/*
 * iec104.h - public C API of the IEC 60870-5-104 library.
 *
 * Rules for this header (see docs/02_Konzept_IEC104_Library.md):
 *   - plain C11, usable from C, C++ and Rust (bindgen)
 *   - no STL types, no exceptions crossing this boundary
 *   - all errors are reported as iec104_status return codes
 *
 * Status: skeleton. Only version and status helpers exist so far.
 * Connection handles, configuration and callbacks follow with the
 * apci/session/transport layers.
 */
#ifndef IEC104_IEC104_H
#define IEC104_IEC104_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(IEC104_BUILDING_LIBRARY)
#    define IEC104_API __declspec(dllexport)
#  else
#    define IEC104_API
#  endif
#else
#  define IEC104_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define IEC104_VERSION_MAJOR 0
#define IEC104_VERSION_MINOR 1
#define IEC104_VERSION_PATCH 0

/* Default TCP ports (IEC 60870-5-104, IANA "iec-104-sec" for TLS). */
#define IEC104_DEFAULT_PORT     2404
#define IEC104_DEFAULT_TLS_PORT 19998

/** Return codes. 0 is success, negative values are errors. */
typedef enum iec104_status {
    IEC104_OK                   = 0,
    IEC104_ERR_INVALID_ARGUMENT = -1, /* null pointer, invalid profile, ... */
    IEC104_ERR_BUFFER_TOO_SMALL = -2, /* output buffer cannot hold the result */
    IEC104_ERR_NEED_MORE_DATA   = -3, /* input incomplete, read more bytes */
    IEC104_ERR_MALFORMED        = -4, /* input violates the frame structure */
    IEC104_ERR_OUT_OF_RANGE     = -5, /* value outside the range allowed by the standard */
    IEC104_ERR_UNSUPPORTED      = -6, /* valid, but not supported (yet) */
    IEC104_ERR_INTERNAL         = -99
} iec104_status;

/** Library version as "major.minor.patch". Never NULL. */
IEC104_API const char* iec104_version_string(void);

/** Short English description of a status code. Never NULL. */
IEC104_API const char* iec104_status_string(iec104_status status);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* IEC104_IEC104_H */
````

### `src/api/iec104.cpp`

````cpp
#include "iec104/iec104.h"

#ifndef IEC104_VERSION_STRING
#define IEC104_VERSION_STRING "0.0.0"
#endif

extern "C" {

const char* iec104_version_string(void) {
    return IEC104_VERSION_STRING;
}

const char* iec104_status_string(iec104_status status) {
    switch (status) {
    case IEC104_OK:
        return "ok";
    case IEC104_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case IEC104_ERR_BUFFER_TOO_SMALL:
        return "buffer too small";
    case IEC104_ERR_NEED_MORE_DATA:
        return "need more data";
    case IEC104_ERR_MALFORMED:
        return "malformed frame";
    case IEC104_ERR_OUT_OF_RANGE:
        return "value out of range";
    case IEC104_ERR_UNSUPPORTED:
        return "unsupported";
    case IEC104_ERR_INTERNAL:
        return "internal error";
    }
    return "unknown status";
}

} // extern "C"
````

### `src/codec/byte_io.hpp`

````cpp
// Little-endian helpers. IEC 60870-5-104 transmits all multi-octet values
// least significant octet first ("mode 1").
#pragma once

#include <cstddef>
#include <cstdint>

namespace iec104::codec::detail {

inline void put_u16_le(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFFU);
    p[1] = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
}

inline std::uint16_t get_u16_le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(static_cast<unsigned>(p[0]) |
                                      (static_cast<unsigned>(p[1]) << 8U));
}

// Writes the lowest `n` octets of `v` (n = 1..4).
inline void put_uint_le(std::uint8_t* p, std::uint32_t v, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        p[i] = static_cast<std::uint8_t>((v >> (8U * i)) & 0xFFU);
    }
}

// Reads `n` octets (n = 1..4).
inline std::uint32_t get_uint_le(const std::uint8_t* p, std::size_t n) noexcept {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        v |= static_cast<std::uint32_t>(p[i]) << (8U * i);
    }
    return v;
}

} // namespace iec104::codec::detail
````

### `src/codec/apci.hpp`

````cpp
// APCI: start octet 0x68, length octet and four control octets.
// Pure functions on byte buffers, no I/O, no allocation.
//
// Sources: docs/03_Recherchebericht, section "Sechs Oktette APCI".
#pragma once

#include <cstddef>
#include <cstdint>

#include "iec104/iec104.h"

namespace iec104::codec {

inline constexpr std::uint8_t kStartByte = 0x68U;
inline constexpr std::size_t kApciLength = 6U;          // start + length + 4 control octets
inline constexpr std::size_t kControlFieldLength = 4U;
inline constexpr std::size_t kMaxLengthField = 253U;    // control octets + ASDU
inline constexpr std::size_t kMaxApduLength = kMaxLengthField + 2U;            // 255
inline constexpr std::size_t kMaxAsduLength = kMaxLengthField - kControlFieldLength; // 249
inline constexpr std::uint16_t kSequenceModulo = 32768U; // N(S), N(R): 15 bit

enum class FrameFormat : std::uint8_t { I, S, U };

// U-format function octet (control octet 1). Exactly one function per frame.
enum class UFunction : std::uint8_t {
    None = 0x00U,
    StartDtAct = 0x07U,
    StartDtCon = 0x0BU,
    StopDtAct = 0x13U,
    StopDtCon = 0x23U,
    TestFrAct = 0x43U,
    TestFrCon = 0x83U,
};

struct Apci {
    FrameFormat format{FrameFormat::U};
    std::uint16_t send_seq{0};          // N(S), I-format only
    std::uint16_t recv_seq{0};          // N(R), I- and S-format
    UFunction u_function{UFunction::None}; // U-format only
    std::size_t asdu_length{0};         // I-format only, 1..249
};

// Stream framing: from the first two octets, determine the total APDU length
// (2 + length field). Returns NEED_MORE_DATA if fewer than two octets are
// available, MALFORMED on a wrong start octet or a length outside 4..253.
iec104_status peek_apdu_length(const std::uint8_t* data, std::size_t size,
                               std::size_t& apdu_length) noexcept;

// Writes the 6-octet APCI of an I-frame. The ASDU (asdu_length octets) must
// follow directly in the output stream. out_size >= 6.
iec104_status encode_i_header(std::uint16_t send_seq, std::uint16_t recv_seq,
                              std::size_t asdu_length, std::uint8_t* out,
                              std::size_t out_size) noexcept;

// Writes a complete S-frame (6 octets).
iec104_status encode_s_frame(std::uint16_t recv_seq, std::uint8_t* out,
                             std::size_t out_size) noexcept;

// Writes a complete U-frame (6 octets).
iec104_status encode_u_frame(UFunction function, std::uint8_t* out, std::size_t out_size) noexcept;

// Decodes the APCI of a complete APDU. `size` must cover the whole APDU
// (2 + length field), otherwise NEED_MORE_DATA. Strict: reserved bits and
// unused octets must be zero, U-frames must carry exactly one function.
iec104_status decode_apci(const std::uint8_t* data, std::size_t size, Apci& out) noexcept;

} // namespace iec104::codec
````

### `src/codec/apci.cpp`

````cpp
#include "codec/apci.hpp"

#include "codec/byte_io.hpp"

namespace iec104::codec {
namespace {

void put_sequence(std::uint8_t* p, std::uint16_t seq) noexcept {
    // 15-bit counter shifted left by one; bit 0 of the first octet is the
    // format bit and stays 0.
    detail::put_u16_le(p, static_cast<std::uint16_t>(seq << 1U));
}

std::uint16_t get_sequence(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(detail::get_u16_le(p) >> 1U);
}

bool is_valid_u_function(std::uint8_t octet) noexcept {
    switch (static_cast<UFunction>(octet)) {
    case UFunction::StartDtAct:
    case UFunction::StartDtCon:
    case UFunction::StopDtAct:
    case UFunction::StopDtCon:
    case UFunction::TestFrAct:
    case UFunction::TestFrCon:
        return true;
    case UFunction::None:
        return false;
    }
    return false;
}

} // namespace

iec104_status peek_apdu_length(const std::uint8_t* data, std::size_t size,
                               std::size_t& apdu_length) noexcept {
    if (data == nullptr && size != 0U) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (size < 2U) {
        return IEC104_ERR_NEED_MORE_DATA;
    }
    if (data[0] != kStartByte) {
        return IEC104_ERR_MALFORMED;
    }
    const std::size_t length = data[1];
    if (length < kControlFieldLength || length > kMaxLengthField) {
        return IEC104_ERR_MALFORMED;
    }
    apdu_length = length + 2U;
    return IEC104_OK;
}

iec104_status encode_i_header(std::uint16_t send_seq, std::uint16_t recv_seq,
                              std::size_t asdu_length, std::uint8_t* out,
                              std::size_t out_size) noexcept {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < kApciLength) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    if (send_seq >= kSequenceModulo || recv_seq >= kSequenceModulo || asdu_length == 0U ||
        asdu_length > kMaxAsduLength) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    out[0] = kStartByte;
    out[1] = static_cast<std::uint8_t>(asdu_length + kControlFieldLength);
    put_sequence(&out[2], send_seq);
    put_sequence(&out[4], recv_seq);
    return IEC104_OK;
}

iec104_status encode_s_frame(std::uint16_t recv_seq, std::uint8_t* out,
                             std::size_t out_size) noexcept {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < kApciLength) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    if (recv_seq >= kSequenceModulo) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    out[0] = kStartByte;
    out[1] = static_cast<std::uint8_t>(kControlFieldLength);
    out[2] = 0x01U;
    out[3] = 0x00U;
    put_sequence(&out[4], recv_seq);
    return IEC104_OK;
}

iec104_status encode_u_frame(UFunction function, std::uint8_t* out, std::size_t out_size) noexcept {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < kApciLength) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    if (!is_valid_u_function(static_cast<std::uint8_t>(function))) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    out[0] = kStartByte;
    out[1] = static_cast<std::uint8_t>(kControlFieldLength);
    out[2] = static_cast<std::uint8_t>(function);
    out[3] = 0x00U;
    out[4] = 0x00U;
    out[5] = 0x00U;
    return IEC104_OK;
}

iec104_status decode_apci(const std::uint8_t* data, std::size_t size, Apci& out) noexcept {
    std::size_t apdu_length = 0;
    const iec104_status st = peek_apdu_length(data, size, apdu_length);
    if (st != IEC104_OK) {
        return st;
    }
    if (size < apdu_length) {
        return IEC104_ERR_NEED_MORE_DATA;
    }
    const std::size_t length = data[1];
    const std::uint8_t c1 = data[2];

    Apci result{};
    if ((c1 & 0x01U) == 0U) {
        // I-format: bit 0 of octet 3 must be 0, ASDU must be present.
        if ((data[4] & 0x01U) != 0U || length == kControlFieldLength) {
            return IEC104_ERR_MALFORMED;
        }
        result.format = FrameFormat::I;
        result.send_seq = get_sequence(&data[2]);
        result.recv_seq = get_sequence(&data[4]);
        result.asdu_length = length - kControlFieldLength;
    } else if ((c1 & 0x03U) == 0x01U) {
        // S-format: octet 1 = 0x01, octet 2 = 0, no ASDU.
        if (c1 != 0x01U || data[3] != 0U || (data[4] & 0x01U) != 0U ||
            length != kControlFieldLength) {
            return IEC104_ERR_MALFORMED;
        }
        result.format = FrameFormat::S;
        result.recv_seq = get_sequence(&data[4]);
    } else {
        // U-format: exactly one function, octets 2..4 zero, no ASDU.
        if (!is_valid_u_function(c1) || data[3] != 0U || data[4] != 0U || data[5] != 0U ||
            length != kControlFieldLength) {
            return IEC104_ERR_MALFORMED;
        }
        result.format = FrameFormat::U;
        result.u_function = static_cast<UFunction>(c1);
    }
    out = result;
    return IEC104_OK;
}

} // namespace iec104::codec
````

### `src/codec/asdu_header.hpp`

````cpp
// ASDU data unit identifier (type id, VSQ, COT, common address) and the
// information object address (IOA).
//
// Field lengths are parameters (AsduProfile). The IEC 104 profile is fixed at
// COT 2, CA 2, IOA 3 octets; other values exist only for non-conforming
// peers (docs/02_Konzept, "ASDU-Profil").
#pragma once

#include <cstddef>
#include <cstdint>

#include "iec104/iec104.h"

namespace iec104::codec {

struct AsduProfile {
    std::uint8_t cot_length{2}; // 1..2 (2 = with originator address)
    std::uint8_t ca_length{2};  // 1..2
    std::uint8_t ioa_length{3}; // 1..3
};

inline constexpr AsduProfile kIec104Profile{};

inline constexpr std::uint8_t kVsqSequenceBit = 0x80U;
inline constexpr std::uint8_t kMaxObjects = 0x7FU;   // 127
inline constexpr std::uint8_t kCotCauseMask = 0x3FU; // causes 0..63
inline constexpr std::uint8_t kCotNegativeBit = 0x40U;
inline constexpr std::uint8_t kCotTestBit = 0x80U;
inline constexpr std::uint16_t kBroadcastCa = 0xFFFFU; // with ca_length 2

struct AsduHeader {
    std::uint8_t type_id{0};
    bool sequence{false};          // SQ bit of VSQ
    std::uint8_t num_objects{0};   // 0..127
    std::uint8_t cause{0};         // COT 0..63
    bool negative{false};          // P/N bit
    bool test{false};              // T bit
    std::uint8_t originator{0};    // only with cot_length 2
    std::uint16_t common_address{0};
};

bool is_valid(const AsduProfile& profile) noexcept;

// 2 + cot_length + ca_length (6 in the IEC 104 profile).
std::size_t header_length(const AsduProfile& profile) noexcept;

iec104_status encode_asdu_header(const AsduHeader& header, const AsduProfile& profile,
                                 std::uint8_t* out, std::size_t out_size,
                                 std::size_t& written) noexcept;

// Decodes the header only, objects are not interpreted. Type id and cause are
// not checked against tables here (that is TASK-001).
iec104_status decode_asdu_header(const std::uint8_t* data, std::size_t size,
                                 const AsduProfile& profile, AsduHeader& out,
                                 std::size_t& consumed) noexcept;

iec104_status encode_ioa(std::uint32_t ioa, const AsduProfile& profile, std::uint8_t* out,
                         std::size_t out_size, std::size_t& written) noexcept;

iec104_status decode_ioa(const std::uint8_t* data, std::size_t size, const AsduProfile& profile,
                         std::uint32_t& ioa, std::size_t& consumed) noexcept;

} // namespace iec104::codec
````

### `src/codec/asdu_header.cpp`

````cpp
#include "codec/asdu_header.hpp"

#include "codec/byte_io.hpp"

namespace iec104::codec {
namespace {

std::uint32_t max_value(std::size_t octets) noexcept {
    return octets >= 4U ? 0xFFFFFFFFU : ((1U << (8U * octets)) - 1U);
}

} // namespace

bool is_valid(const AsduProfile& profile) noexcept {
    return profile.cot_length >= 1U && profile.cot_length <= 2U && profile.ca_length >= 1U &&
           profile.ca_length <= 2U && profile.ioa_length >= 1U && profile.ioa_length <= 3U;
}

std::size_t header_length(const AsduProfile& profile) noexcept {
    return 2U + static_cast<std::size_t>(profile.cot_length) +
           static_cast<std::size_t>(profile.ca_length);
}

iec104_status encode_asdu_header(const AsduHeader& header, const AsduProfile& profile,
                                 std::uint8_t* out, std::size_t out_size,
                                 std::size_t& written) noexcept {
    if (out == nullptr || !is_valid(profile)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    const std::size_t len = header_length(profile);
    if (out_size < len) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    if (header.num_objects > kMaxObjects || header.cause > kCotCauseMask ||
        (profile.cot_length == 1U && header.originator != 0U) ||
        header.common_address > max_value(profile.ca_length)) {
        return IEC104_ERR_OUT_OF_RANGE;
    }

    std::size_t pos = 0;
    out[pos++] = header.type_id;
    out[pos++] = static_cast<std::uint8_t>((header.sequence ? kVsqSequenceBit : 0U) |
                                           header.num_objects);
    out[pos++] = static_cast<std::uint8_t>(header.cause | (header.negative ? kCotNegativeBit : 0U) |
                                           (header.test ? kCotTestBit : 0U));
    if (profile.cot_length == 2U) {
        out[pos++] = header.originator;
    }
    detail::put_uint_le(&out[pos], header.common_address, profile.ca_length);
    pos += profile.ca_length;

    written = pos;
    return IEC104_OK;
}

iec104_status decode_asdu_header(const std::uint8_t* data, std::size_t size,
                                 const AsduProfile& profile, AsduHeader& out,
                                 std::size_t& consumed) noexcept {
    if (data == nullptr || !is_valid(profile)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    const std::size_t len = header_length(profile);
    if (size < len) {
        return IEC104_ERR_NEED_MORE_DATA;
    }

    AsduHeader h{};
    std::size_t pos = 0;
    h.type_id = data[pos++];
    h.sequence = (data[pos] & kVsqSequenceBit) != 0U;
    h.num_objects = static_cast<std::uint8_t>(data[pos++] & kMaxObjects);
    h.cause = static_cast<std::uint8_t>(data[pos] & kCotCauseMask);
    h.negative = (data[pos] & kCotNegativeBit) != 0U;
    h.test = (data[pos] & kCotTestBit) != 0U;
    ++pos;
    if (profile.cot_length == 2U) {
        h.originator = data[pos++];
    }
    h.common_address =
        static_cast<std::uint16_t>(detail::get_uint_le(&data[pos], profile.ca_length));
    pos += profile.ca_length;

    out = h;
    consumed = pos;
    return IEC104_OK;
}

iec104_status encode_ioa(std::uint32_t ioa, const AsduProfile& profile, std::uint8_t* out,
                         std::size_t out_size, std::size_t& written) noexcept {
    if (out == nullptr || !is_valid(profile)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < profile.ioa_length) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    if (ioa > max_value(profile.ioa_length)) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    detail::put_uint_le(out, ioa, profile.ioa_length);
    written = profile.ioa_length;
    return IEC104_OK;
}

iec104_status decode_ioa(const std::uint8_t* data, std::size_t size, const AsduProfile& profile,
                         std::uint32_t& ioa, std::size_t& consumed) noexcept {
    if (data == nullptr || !is_valid(profile)) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (size < profile.ioa_length) {
        return IEC104_ERR_NEED_MORE_DATA;
    }
    ioa = detail::get_uint_le(data, profile.ioa_length);
    consumed = profile.ioa_length;
    return IEC104_OK;
}

} // namespace iec104::codec
````

### `src/codec/cp56time2a.hpp`

````cpp
// CP56Time2a: 7-octet time tag.
//
// Layout (octets 1..7):
//   1-2  milliseconds incl. seconds, 0..59999, little endian
//   3    bits 0-5 minute, bit 6 RES1, bit 7 IV (invalid)
//   4    bits 0-4 hour,   bits 5-6 RES2, bit 7 SU (summer time)
//   5    bits 0-4 day of month 1..31, bits 5-7 day of week 1..7 (0 = not used)
//   6    bits 0-3 month 1..12, bits 4-7 RES3
//   7    bits 0-6 year 0..99,  bit 7 RES4
//
// The standard does NOT define whether the time is UTC or local time. This
// type therefore carries no time zone; interpretation belongs to the
// connection profile (TimeBase, see docs/02_Konzept, section "Zeit").
#pragma once

#include <cstddef>
#include <cstdint>

#include "iec104/iec104.h"

namespace iec104::codec {

inline constexpr std::size_t kCp56Time2aLength = 7U;

struct Cp56Time2a {
    std::uint16_t milliseconds{0}; // 0..59999 (seconds * 1000 + ms)
    std::uint8_t minute{0};        // 0..59
    std::uint8_t hour{0};          // 0..23
    std::uint8_t day_of_month{1};  // 1..31
    std::uint8_t day_of_week{0};   // 1..7, 0 = not used
    std::uint8_t month{1};         // 1..12
    std::uint8_t year{0};          // 0..99
    bool invalid{false};           // IV
    bool summer_time{false};       // SU
    bool res1{false};              // RES1 bit of the minute octet, kept as is

    friend bool operator==(const Cp56Time2a& a, const Cp56Time2a& b) noexcept {
        return a.milliseconds == b.milliseconds && a.minute == b.minute && a.hour == b.hour &&
               a.day_of_month == b.day_of_month && a.day_of_week == b.day_of_week &&
               a.month == b.month && a.year == b.year && a.invalid == b.invalid &&
               a.summer_time == b.summer_time && a.res1 == b.res1;
    }
    friend bool operator!=(const Cp56Time2a& a, const Cp56Time2a& b) noexcept {
        return !(a == b);
    }
};

// Range check of all fields. Does not check calendar validity (e.g. 31.02.).
iec104_status validate(const Cp56Time2a& t) noexcept;

// Encodes 7 octets. Returns OUT_OF_RANGE if validate() fails.
iec104_status encode_cp56time2a(const Cp56Time2a& t, std::uint8_t* out,
                                std::size_t out_size) noexcept;

// Decodes 7 octets without range check (tolerant). Reserved bits RES2..RES4
// are ignored. Call validate() for strict handling.
iec104_status decode_cp56time2a(const std::uint8_t* data, std::size_t size,
                                Cp56Time2a& out) noexcept;

} // namespace iec104::codec
````

### `src/codec/cp56time2a.cpp`

````cpp
#include "codec/cp56time2a.hpp"

#include "codec/byte_io.hpp"

namespace iec104::codec {

iec104_status validate(const Cp56Time2a& t) noexcept {
    if (t.milliseconds > 59999U || t.minute > 59U || t.hour > 23U || t.day_of_month < 1U ||
        t.day_of_month > 31U || t.day_of_week > 7U || t.month < 1U || t.month > 12U ||
        t.year > 99U) {
        return IEC104_ERR_OUT_OF_RANGE;
    }
    return IEC104_OK;
}

iec104_status encode_cp56time2a(const Cp56Time2a& t, std::uint8_t* out,
                                std::size_t out_size) noexcept {
    if (out == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (out_size < kCp56Time2aLength) {
        return IEC104_ERR_BUFFER_TOO_SMALL;
    }
    const iec104_status st = validate(t);
    if (st != IEC104_OK) {
        return st;
    }
    detail::put_u16_le(out, t.milliseconds);
    out[2] = static_cast<std::uint8_t>((t.minute & 0x3FU) | (t.res1 ? 0x40U : 0U) |
                                       (t.invalid ? 0x80U : 0U));
    out[3] = static_cast<std::uint8_t>((t.hour & 0x1FU) | (t.summer_time ? 0x80U : 0U));
    out[4] = static_cast<std::uint8_t>((t.day_of_month & 0x1FU) |
                                       ((t.day_of_week & 0x07U) << 5U));
    out[5] = static_cast<std::uint8_t>(t.month & 0x0FU);
    out[6] = static_cast<std::uint8_t>(t.year & 0x7FU);
    return IEC104_OK;
}

iec104_status decode_cp56time2a(const std::uint8_t* data, std::size_t size,
                                Cp56Time2a& out) noexcept {
    if (data == nullptr) {
        return IEC104_ERR_INVALID_ARGUMENT;
    }
    if (size < kCp56Time2aLength) {
        return IEC104_ERR_NEED_MORE_DATA;
    }
    Cp56Time2a t{};
    t.milliseconds = detail::get_u16_le(data);
    t.minute = static_cast<std::uint8_t>(data[2] & 0x3FU);
    t.res1 = (data[2] & 0x40U) != 0U;
    t.invalid = (data[2] & 0x80U) != 0U;
    t.hour = static_cast<std::uint8_t>(data[3] & 0x1FU);
    t.summer_time = (data[3] & 0x80U) != 0U;
    t.day_of_month = static_cast<std::uint8_t>(data[4] & 0x1FU);
    t.day_of_week = static_cast<std::uint8_t>((data[4] >> 5U) & 0x07U);
    t.month = static_cast<std::uint8_t>(data[5] & 0x0FU);
    t.year = static_cast<std::uint8_t>(data[6] & 0x7FU);
    out = t;
    return IEC104_OK;
}

} // namespace iec104::codec
````

### `tests/test_api.cpp`

````cpp
#include <cstring>

#include <gtest/gtest.h>

#include "iec104/iec104.h"

TEST(Api, VersionStringMatchesMacros) {
    const char* v = iec104_version_string();
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v, "0.1.0");
}

TEST(Api, StatusStringNeverNull) {
    EXPECT_STREQ(iec104_status_string(IEC104_OK), "ok");
    EXPECT_STREQ(iec104_status_string(IEC104_ERR_MALFORMED), "malformed frame");
    EXPECT_STREQ(iec104_status_string(static_cast<iec104_status>(12345)), "unknown status");
}
````

### `tests/test_apci.cpp`

````cpp
#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "codec/apci.hpp"

using namespace iec104::codec;
using Bytes6 = std::array<std::uint8_t, 6>;

// Reference frames, see docs/03_Recherchebericht (U functions) and the
// Wireshark dissector.
TEST(Apci, EncodeUFrames) {
    struct Case {
        UFunction f;
        Bytes6 expected;
    };
    const Case cases[] = {
        {UFunction::StartDtAct, {0x68, 0x04, 0x07, 0x00, 0x00, 0x00}},
        {UFunction::StartDtCon, {0x68, 0x04, 0x0B, 0x00, 0x00, 0x00}},
        {UFunction::StopDtAct, {0x68, 0x04, 0x13, 0x00, 0x00, 0x00}},
        {UFunction::StopDtCon, {0x68, 0x04, 0x23, 0x00, 0x00, 0x00}},
        {UFunction::TestFrAct, {0x68, 0x04, 0x43, 0x00, 0x00, 0x00}},
        {UFunction::TestFrCon, {0x68, 0x04, 0x83, 0x00, 0x00, 0x00}},
    };
    for (const auto& c : cases) {
        Bytes6 out{};
        ASSERT_EQ(encode_u_frame(c.f, out.data(), out.size()), IEC104_OK);
        EXPECT_EQ(out, c.expected);

        Apci apci{};
        ASSERT_EQ(decode_apci(out.data(), out.size(), apci), IEC104_OK);
        EXPECT_EQ(apci.format, FrameFormat::U);
        EXPECT_EQ(apci.u_function, c.f);
    }
}

TEST(Apci, EncodeUFrameRejectsNone) {
    Bytes6 out{};
    EXPECT_EQ(encode_u_frame(UFunction::None, out.data(), out.size()),
              IEC104_ERR_INVALID_ARGUMENT);
}

TEST(Apci, EncodeSFrame) {
    Bytes6 out{};
    ASSERT_EQ(encode_s_frame(1, out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out, (Bytes6{0x68, 0x04, 0x01, 0x00, 0x02, 0x00}));

    Apci apci{};
    ASSERT_EQ(decode_apci(out.data(), out.size(), apci), IEC104_OK);
    EXPECT_EQ(apci.format, FrameFormat::S);
    EXPECT_EQ(apci.recv_seq, 1U);
}

TEST(Apci, IFrameSequenceNumbersRoundTrip) {
    const std::uint16_t values[] = {0, 1, 127, 128, 255, 256, 16383, 32766, 32767};
    for (std::uint16_t ns : values) {
        for (std::uint16_t nr : values) {
            std::array<std::uint8_t, 7> buf{};
            ASSERT_EQ(encode_i_header(ns, nr, 1, buf.data(), buf.size()), IEC104_OK);
            Apci apci{};
            ASSERT_EQ(decode_apci(buf.data(), buf.size(), apci), IEC104_OK);
            EXPECT_EQ(apci.format, FrameFormat::I);
            EXPECT_EQ(apci.send_seq, ns);
            EXPECT_EQ(apci.recv_seq, nr);
            EXPECT_EQ(apci.asdu_length, 1U);
        }
    }
}

TEST(Apci, IFrameKnownBytes) {
    // N(S) = 2, N(R) = 3, ASDU length 10 (general interrogation)
    Bytes6 out{};
    ASSERT_EQ(encode_i_header(2, 3, 10, out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out, (Bytes6{0x68, 0x0E, 0x04, 0x00, 0x06, 0x00}));
}

TEST(Apci, EncodeRejectsOutOfRange) {
    Bytes6 out{};
    EXPECT_EQ(encode_i_header(32768, 0, 1, out.data(), out.size()), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(encode_i_header(0, 32768, 1, out.data(), out.size()), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(encode_i_header(0, 0, 0, out.data(), out.size()), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(encode_i_header(0, 0, kMaxAsduLength + 1U, out.data(), out.size()),
              IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(encode_i_header(0, 0, kMaxAsduLength, out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out[1], 253U);
    EXPECT_EQ(encode_s_frame(32768, out.data(), out.size()), IEC104_ERR_OUT_OF_RANGE);
    EXPECT_EQ(encode_s_frame(0, out.data(), 5), IEC104_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(encode_s_frame(0, nullptr, 6), IEC104_ERR_INVALID_ARGUMENT);
}

TEST(Apci, PeekLength) {
    std::size_t len = 0;
    const std::uint8_t ok[] = {0x68, 0x0E};
    EXPECT_EQ(peek_apdu_length(ok, 1, len), IEC104_ERR_NEED_MORE_DATA);
    ASSERT_EQ(peek_apdu_length(ok, 2, len), IEC104_OK);
    EXPECT_EQ(len, 16U);

    const std::uint8_t bad_start[] = {0x69, 0x04};
    EXPECT_EQ(peek_apdu_length(bad_start, 2, len), IEC104_ERR_MALFORMED);
    const std::uint8_t too_short[] = {0x68, 0x03};
    EXPECT_EQ(peek_apdu_length(too_short, 2, len), IEC104_ERR_MALFORMED);
    const std::uint8_t too_long[] = {0x68, 0xFE};
    EXPECT_EQ(peek_apdu_length(too_long, 2, len), IEC104_ERR_MALFORMED);
}

TEST(Apci, DecodeRejectsMalformed) {
    Apci apci{};
    // incomplete APDU
    const std::uint8_t partial[] = {0x68, 0x0E, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(decode_apci(partial, sizeof partial, apci), IEC104_ERR_NEED_MORE_DATA);
    // I-frame without ASDU
    const std::uint8_t i_empty[] = {0x68, 0x04, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(decode_apci(i_empty, sizeof i_empty, apci), IEC104_ERR_MALFORMED);
    // S-frame with non-zero octet 2
    const std::uint8_t s_bad[] = {0x68, 0x04, 0x01, 0x01, 0x00, 0x00};
    EXPECT_EQ(decode_apci(s_bad, sizeof s_bad, apci), IEC104_ERR_MALFORMED);
    // U-frame with two functions (STARTDT act + TESTFR act)
    const std::uint8_t u_two[] = {0x68, 0x04, 0x47, 0x00, 0x00, 0x00};
    EXPECT_EQ(decode_apci(u_two, sizeof u_two, apci), IEC104_ERR_MALFORMED);
    // U-frame with trailing ASDU
    const std::uint8_t u_long[] = {0x68, 0x05, 0x07, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(decode_apci(u_long, sizeof u_long, apci), IEC104_ERR_MALFORMED);
    // I-frame with LSB of N(R) set
    const std::uint8_t i_bad_nr[] = {0x68, 0x05, 0x00, 0x00, 0x01, 0x00, 0x00};
    EXPECT_EQ(decode_apci(i_bad_nr, sizeof i_bad_nr, apci), IEC104_ERR_MALFORMED);
}
````

### `tests/test_asdu_header.cpp`

````cpp
#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "codec/apci.hpp"
#include "codec/asdu_header.hpp"

using namespace iec104::codec;

// General interrogation C_IC_NA_1 (100), COT 6 activation, CA 1, IOA 0, QOI 20
static const std::uint8_t kGeneralInterrogation[] = {
    0x68, 0x0E, 0x00, 0x00, 0x00, 0x00,       // APCI, N(S) = N(R) = 0
    0x64, 0x01, 0x06, 0x00, 0x01, 0x00,       // type 100, VSQ 1, COT 6, ORG 0, CA 1
    0x00, 0x00, 0x00,                         // IOA 0
    0x14};                                    // QOI 20 (station interrogation)

TEST(AsduHeader, DecodeGeneralInterrogation) {
    Apci apci{};
    ASSERT_EQ(decode_apci(kGeneralInterrogation, sizeof kGeneralInterrogation, apci), IEC104_OK);
    ASSERT_EQ(apci.format, FrameFormat::I);
    ASSERT_EQ(apci.asdu_length, 10U);

    const std::uint8_t* asdu = kGeneralInterrogation + kApciLength;
    AsduHeader h{};
    std::size_t consumed = 0;
    ASSERT_EQ(decode_asdu_header(asdu, apci.asdu_length, kIec104Profile, h, consumed), IEC104_OK);
    EXPECT_EQ(consumed, 6U);
    EXPECT_EQ(h.type_id, 100U);
    EXPECT_FALSE(h.sequence);
    EXPECT_EQ(h.num_objects, 1U);
    EXPECT_EQ(h.cause, 6U);
    EXPECT_FALSE(h.negative);
    EXPECT_FALSE(h.test);
    EXPECT_EQ(h.originator, 0U);
    EXPECT_EQ(h.common_address, 1U);

    std::uint32_t ioa = 99;
    std::size_t ioa_len = 0;
    ASSERT_EQ(decode_ioa(asdu + consumed, apci.asdu_length - consumed, kIec104Profile, ioa,
                         ioa_len),
              IEC104_OK);
    EXPECT_EQ(ioa, 0U);
    EXPECT_EQ(ioa_len, 3U);
}

TEST(AsduHeader, EncodeRoundTripAllFlags) {
    AsduHeader h{};
    h.type_id = 13;
    h.sequence = true;
    h.num_objects = 127;
    h.cause = 47;
    h.negative = true;
    h.test = true;
    h.originator = 0xAB;
    h.common_address = 0xFFFF;

    std::array<std::uint8_t, 6> buf{};
    std::size_t written = 0;
    ASSERT_EQ(encode_asdu_header(h, kIec104Profile, buf.data(), buf.size(), written), IEC104_OK);
    EXPECT_EQ(written, 6U);
    EXPECT_EQ(buf, (std::array<std::uint8_t, 6>{0x0D, 0xFF, 0xEF, 0xAB, 0xFF, 0xFF}));

    AsduHeader d{};
    std::size_t consumed = 0;
    ASSERT_EQ(decode_asdu_header(buf.data(), buf.size(), kIec104Profile, d, consumed), IEC104_OK);
    EXPECT_EQ(d.type_id, h.type_id);
    EXPECT_EQ(d.sequence, h.sequence);
    EXPECT_EQ(d.num_objects, h.num_objects);
    EXPECT_EQ(d.cause, h.cause);
    EXPECT_EQ(d.negative, h.negative);
    EXPECT_EQ(d.test, h.test);
    EXPECT_EQ(d.originator, h.originator);
    EXPECT_EQ(d.common_address, h.common_address);
}

TEST(AsduHeader, ShortProfile) {
    const AsduProfile p{1, 1, 2};
    EXPECT_TRUE(is_valid(p));
    EXPECT_EQ(header_length(p), 4U);

    AsduHeader h{};
    h.type_id = 1;
    h.num_objects = 1;
    h.cause = 3;
    h.common_address = 200;
    std::array<std::uint8_t, 4> buf{};
    std::size_t written = 0;
    ASSERT_EQ(encode_asdu_header(h, p, buf.data(), buf.size(), written), IEC104_OK);
    EXPECT_EQ(buf, (std::array<std::uint8_t, 4>{0x01, 0x01, 0x03, 0xC8}));

    h.common_address = 256; // does not fit into one octet
    EXPECT_EQ(encode_asdu_header(h, p, buf.data(), buf.size(), written), IEC104_ERR_OUT_OF_RANGE);
    h.common_address = 1;
    h.originator = 1; // no originator octet with cot_length 1
    EXPECT_EQ(encode_asdu_header(h, p, buf.data(), buf.size(), written), IEC104_ERR_OUT_OF_RANGE);
}

TEST(AsduHeader, RejectsInvalidInput) {
    AsduHeader h{};
    std::array<std::uint8_t, 6> buf{};
    std::size_t n = 0;
    EXPECT_FALSE(is_valid(AsduProfile{0, 2, 3}));
    EXPECT_FALSE(is_valid(AsduProfile{2, 3, 3}));
    EXPECT_FALSE(is_valid(AsduProfile{2, 2, 4}));
    EXPECT_EQ(encode_asdu_header(h, AsduProfile{2, 2, 4}, buf.data(), buf.size(), n),
              IEC104_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(encode_asdu_header(h, kIec104Profile, buf.data(), 5, n),
              IEC104_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(decode_asdu_header(buf.data(), 5, kIec104Profile, h, n), IEC104_ERR_NEED_MORE_DATA);

    h.num_objects = 128;
    EXPECT_EQ(encode_asdu_header(h, kIec104Profile, buf.data(), buf.size(), n),
              IEC104_ERR_OUT_OF_RANGE);
    h.num_objects = 1;
    h.cause = 64;
    EXPECT_EQ(encode_asdu_header(h, kIec104Profile, buf.data(), buf.size(), n),
              IEC104_ERR_OUT_OF_RANGE);
}

TEST(AsduHeader, IoaRanges) {
    std::array<std::uint8_t, 3> buf{};
    std::size_t n = 0;
    ASSERT_EQ(encode_ioa(0xFFFFFF, kIec104Profile, buf.data(), buf.size(), n), IEC104_OK);
    EXPECT_EQ(buf, (std::array<std::uint8_t, 3>{0xFF, 0xFF, 0xFF}));
    EXPECT_EQ(encode_ioa(0x1000000, kIec104Profile, buf.data(), buf.size(), n),
              IEC104_ERR_OUT_OF_RANGE);

    ASSERT_EQ(encode_ioa(4711, kIec104Profile, buf.data(), buf.size(), n), IEC104_OK);
    EXPECT_EQ(buf, (std::array<std::uint8_t, 3>{0x67, 0x12, 0x00}));
    std::uint32_t ioa = 0;
    ASSERT_EQ(decode_ioa(buf.data(), buf.size(), kIec104Profile, ioa, n), IEC104_OK);
    EXPECT_EQ(ioa, 4711U);
    EXPECT_EQ(decode_ioa(buf.data(), 2, kIec104Profile, ioa, n), IEC104_ERR_NEED_MORE_DATA);
}
````

### `tests/test_cp56time2a.cpp`

````cpp
#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "codec/cp56time2a.hpp"

using namespace iec104::codec;
using Bytes7 = std::array<std::uint8_t, 7>;

namespace {
Cp56Time2a sample() {
    // Wednesday 2026-11-25 09:48:30.123
    Cp56Time2a t{};
    t.milliseconds = 30123;
    t.minute = 48;
    t.hour = 9;
    t.day_of_month = 25;
    t.day_of_week = 3;
    t.month = 11;
    t.year = 26;
    return t;
}
} // namespace

TEST(Cp56Time2a, EncodeKnownBytes) {
    Bytes7 out{};
    ASSERT_EQ(encode_cp56time2a(sample(), out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out, (Bytes7{0xAB, 0x75, 0x30, 0x09, 0x79, 0x0B, 0x1A}));
}

TEST(Cp56Time2a, FlagsRoundTrip) {
    Cp56Time2a t = sample();
    t.invalid = true;
    t.summer_time = true;
    t.res1 = true;
    Bytes7 out{};
    ASSERT_EQ(encode_cp56time2a(t, out.data(), out.size()), IEC104_OK);
    EXPECT_EQ(out[2], 0x30U | 0x40U | 0x80U);
    EXPECT_EQ(out[3], 0x09U | 0x80U);

    Cp56Time2a d{};
    ASSERT_EQ(decode_cp56time2a(out.data(), out.size(), d), IEC104_OK);
    EXPECT_EQ(d, t);
}

TEST(Cp56Time2a, DecodeIgnoresReservedBits) {
    // RES2 (hour bits 5-6), RES3 (month bits 4-7), RES4 (year bit 7) set
    const Bytes7 in{0xAB, 0x75, 0x30, 0x09 | 0x60, 0x79, 0x0B | 0xF0, 0x1A | 0x80};
    Cp56Time2a d{};
    ASSERT_EQ(decode_cp56time2a(in.data(), in.size(), d), IEC104_OK);
    EXPECT_EQ(d, sample());
}

TEST(Cp56Time2a, ValidateRanges) {
    EXPECT_EQ(validate(sample()), IEC104_OK);

    Cp56Time2a t = sample();
    t.milliseconds = 60000;
    EXPECT_EQ(validate(t), IEC104_ERR_OUT_OF_RANGE);
    t = sample();
    t.minute = 60;
    EXPECT_EQ(validate(t), IEC104_ERR_OUT_OF_RANGE);
    t = sample();
    t.hour = 24;
    EXPECT_EQ(validate(t), IEC104_ERR_OUT_OF_RANGE);
    t = sample();
    t.day_of_month = 0;
    EXPECT_EQ(validate(t), IEC104_ERR_OUT_OF_RANGE);
    t = sample();
    t.month = 13;
    EXPECT_EQ(validate(t), IEC104_ERR_OUT_OF_RANGE);
    t = sample();
    t.year = 100;
    EXPECT_EQ(validate(t), IEC104_ERR_OUT_OF_RANGE);

    Bytes7 out{};
    EXPECT_EQ(encode_cp56time2a(t, out.data(), out.size()), IEC104_ERR_OUT_OF_RANGE);
}

TEST(Cp56Time2a, DecodeIsTolerant) {
    // month 0 and minute 63 decode, but fail validation
    const Bytes7 in{0x00, 0x00, 0x3F, 0x00, 0x01, 0x00, 0x00};
    Cp56Time2a d{};
    ASSERT_EQ(decode_cp56time2a(in.data(), in.size(), d), IEC104_OK);
    EXPECT_EQ(d.minute, 63U);
    EXPECT_EQ(d.month, 0U);
    EXPECT_EQ(validate(d), IEC104_ERR_OUT_OF_RANGE);
}

TEST(Cp56Time2a, BufferChecks) {
    Bytes7 out{};
    EXPECT_EQ(encode_cp56time2a(sample(), out.data(), 6), IEC104_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(encode_cp56time2a(sample(), nullptr, 7), IEC104_ERR_INVALID_ARGUMENT);
    Cp56Time2a d{};
    EXPECT_EQ(decode_cp56time2a(out.data(), 6, d), IEC104_ERR_NEED_MORE_DATA);
}
````

### `fuzz/fuzz_codec.cpp`

````cpp
// libFuzzer target for the codec layer.
// Build:  cmake --preset fuzz && cmake --build --preset fuzz
// Run:    ./build/fuzz/fuzz_codec -max_total_time=600 fuzz/corpus
//
// Invariant checked: whatever decodes successfully must re-encode to the
// identical bytes (APCI, ASDU header with the IEC 104 profile, CP56Time2a
// apart from the ignored reserved bits).
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "codec/apci.hpp"
#include "codec/asdu_header.hpp"
#include "codec/cp56time2a.hpp"

using namespace iec104::codec;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // APCI
    Apci apci{};
    if (decode_apci(data, size, apci) == IEC104_OK) {
        std::array<std::uint8_t, kApciLength> re{};
        iec104_status st = IEC104_ERR_INTERNAL;
        switch (apci.format) {
        case FrameFormat::I:
            st = encode_i_header(apci.send_seq, apci.recv_seq, apci.asdu_length, re.data(),
                                 re.size());
            break;
        case FrameFormat::S:
            st = encode_s_frame(apci.recv_seq, re.data(), re.size());
            break;
        case FrameFormat::U:
            st = encode_u_frame(apci.u_function, re.data(), re.size());
            break;
        }
        if (st != IEC104_OK || std::memcmp(re.data(), data, kApciLength) != 0) {
            std::abort();
        }

        // ASDU header behind an I-frame
        if (apci.format == FrameFormat::I) {
            const std::uint8_t* asdu = data + kApciLength;
            AsduHeader h{};
            std::size_t consumed = 0;
            if (decode_asdu_header(asdu, apci.asdu_length, kIec104Profile, h, consumed) ==
                IEC104_OK) {
                std::array<std::uint8_t, 6> hb{};
                std::size_t written = 0;
                if (encode_asdu_header(h, kIec104Profile, hb.data(), hb.size(), written) !=
                        IEC104_OK ||
                    written != consumed || std::memcmp(hb.data(), asdu, written) != 0) {
                    std::abort();
                }
            }
        }
    }

    // CP56Time2a on the raw input
    Cp56Time2a t{};
    if (decode_cp56time2a(data, size, t) == IEC104_OK && validate(t) == IEC104_OK) {
        std::array<std::uint8_t, kCp56Time2aLength> tb{};
        Cp56Time2a back{};
        if (encode_cp56time2a(t, tb.data(), tb.size()) != IEC104_OK ||
            decode_cp56time2a(tb.data(), tb.size(), back) != IEC104_OK || !(back == t)) {
            std::abort();
        }
    }
    return 0;
}
````

### `tools/setup_tools.sh`

````bash
#!/usr/bin/env bash
# setup_tools.sh – Werkzeuge für die IEC-104-Library (C++17, C-API, OpenSSL, Tests)
#
# Aufruf:
#   ./setup_tools.sh                  Grundausstattung installieren
#   ./setup_tools.sh --with-lib60870  zusätzlich lib60870-C als Testgegenstelle bauen
#                                     (GPLv3, nur für Tests, liegt außerhalb des Repos)
#
# Unterstützt: apt (Debian/Ubuntu/Tuxedo), dnf (Fedora/RHEL), zypper (openSUSE).
# Pakete werden einzeln installiert. Fehlt ein Paket, läuft das Skript weiter
# und listet es am Ende auf.

set -u

WITH_LIB60870=0
for arg in "$@"; do
    case "$arg" in
        --with-lib60870) WITH_LIB60870=1 ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "Unbekannte Option: $arg"; exit 1 ;;
    esac
done

THIRD_PARTY_DIR="${HOME}/src/third_party"
FAILED=()

if [ "$(id -u)" -eq 0 ]; then SUDO=""; else SUDO="sudo"; fi

# ---------------------------------------------------------------------------
# Paketmanager erkennen und Paketlisten festlegen
# ---------------------------------------------------------------------------
if command -v apt-get >/dev/null 2>&1; then
    PM="apt"
    UPDATE="$SUDO apt-get update"
    INSTALL="$SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends"
    PACKAGES=(
        # Compiler und Build
        build-essential g++ clang lld llvm cmake ninja-build pkg-config ccache git
        # Analyse und Formatierung
        clang-tidy clang-format cppcheck
        # Debugging, Sanitizer-Laufzeit
        gdb valgrind
        # TLS
        openssl libssl-dev libmbedtls-dev
        # Unit-Tests
        libgtest-dev libgmock-dev catch2
        # Fuzzing
        afl++
        # Trace-Analyse, Netzwerk-Namespaces, Testverbindungen
        tshark iproute2 netcat-openbsd
        # Doku
        doxygen
    )
elif command -v dnf >/dev/null 2>&1; then
    PM="dnf"
    # Nicht erreichbare Fremd-Repos (z. B. almalinux-nvidia) überspringen statt abbrechen
    UPDATE="$SUDO dnf -y --setopt=skip_if_unavailable=True makecache"
    INSTALL="$SUDO dnf install -y --setopt=skip_if_unavailable=True"
    PACKAGES=(
        gcc gcc-c++ make clang lld llvm cmake ninja-build pkgconf-pkg-config ccache git
        clang-tools-extra cppcheck
        gdb valgrind libasan libubsan compiler-rt
        openssl openssl-devel mbedtls-devel
        gtest-devel gmock-devel catch2-devel
        american-fuzzy-lop
        wireshark-cli iproute nmap-ncat
        doxygen
    )
elif command -v zypper >/dev/null 2>&1; then
    PM="zypper"
    UPDATE="$SUDO zypper --non-interactive refresh"
    INSTALL="$SUDO zypper --non-interactive install"
    PACKAGES=(
        gcc gcc-c++ make clang lld llvm cmake ninja pkg-config ccache git
        clang-tools cppcheck
        gdb valgrind
        openssl libopenssl-devel mbedtls-devel
        googletest-devel Catch2-devel
        afl
        wireshark iproute2 netcat-openbsd
        doxygen
    )
else
    echo "Kein unterstützter Paketmanager gefunden (apt, dnf, zypper)."
    exit 1
fi

echo "==> Paketmanager: $PM"
echo "==> Paketquellen aktualisieren"
$UPDATE || { echo "Aktualisieren fehlgeschlagen (Proxy/Netz prüfen)."; exit 1; }

# ---------------------------------------------------------------------------
# Pakete einzeln installieren
# ---------------------------------------------------------------------------
for pkg in "${PACKAGES[@]}"; do
    echo "==> $pkg"
    if ! $INSTALL "$pkg" >/dev/null 2>&1; then
        echo "    !! nicht installiert"
        FAILED+=("$pkg")
    fi
done

# Ubuntu/Debian: libFuzzer steckt in libclang-rt-<Version>-dev
if [ "$PM" = "apt" ] && command -v clang >/dev/null 2>&1; then
    CLANG_MAJOR="$(clang -dumpversion | cut -d. -f1)"
    pkg="libclang-rt-${CLANG_MAJOR}-dev"
    if apt-cache show "$pkg" >/dev/null 2>&1; then
        echo "==> $pkg (libFuzzer)"
        $INSTALL "$pkg" >/dev/null 2>&1 || FAILED+=("$pkg")
    fi
fi

# ---------------------------------------------------------------------------
# Optional: lib60870-C als Testgegenstelle (GPLv3, nur Test, nie linken)
# ---------------------------------------------------------------------------
if [ "$WITH_LIB60870" -eq 1 ]; then
    echo "==> lib60870-C nach ${THIRD_PARTY_DIR}/lib60870"
    mkdir -p "$THIRD_PARTY_DIR"
    if [ ! -d "${THIRD_PARTY_DIR}/lib60870/.git" ]; then
        git clone --depth 1 https://github.com/mz-automation/lib60870.git \
            "${THIRD_PARTY_DIR}/lib60870" || FAILED+=("lib60870 (clone)")
    fi
    if [ -d "${THIRD_PARTY_DIR}/lib60870/lib60870-C" ]; then
        cmake -S "${THIRD_PARTY_DIR}/lib60870/lib60870-C" \
              -B "${THIRD_PARTY_DIR}/lib60870/lib60870-C/build" \
              -G Ninja -DCMAKE_BUILD_TYPE=Release \
        && cmake --build "${THIRD_PARTY_DIR}/lib60870/lib60870-C/build" \
        || FAILED+=("lib60870 (build)")
        echo "    Beispiele (z. B. cs104_server, cs104_client) liegen unter"
        echo "    ${THIRD_PARTY_DIR}/lib60870/lib60870-C/build/examples"
        echo "    TLS-Beispiele brauchen mbedTLS 2.28 im Ordner dependencies/ (siehe README von lib60870)."
    fi
fi

# ---------------------------------------------------------------------------
# Prüfung
# ---------------------------------------------------------------------------
echo
echo "==> Versionen"
check() {
    if command -v "$1" >/dev/null 2>&1; then
        printf "  %-14s %s\n" "$1" "$("$@" 2>&1 | head -n1)"
    else
        printf "  %-14s FEHLT\n" "$1"
    fi
}
check g++ --version
check clang++ --version
check cmake --version
check ninja --version
check clang-tidy --version
check cppcheck --version
check openssl version
check gdb --version
check tshark --version
check afl-fuzz -h

# libFuzzer und Sanitizer kurz testen
TMPD="$(mktemp -d)"
cat > "${TMPD}/fuzz.cpp" <<'EOF'
#include <cstdint>
#include <cstddef>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t) { return 0; }
EOF
if command -v clang++ >/dev/null 2>&1 && \
   clang++ -fsanitize=fuzzer,address,undefined "${TMPD}/fuzz.cpp" -o "${TMPD}/fuzz" >/dev/null 2>&1; then
    echo "  libFuzzer+ASan+UBSan  ok"
else
    echo "  libFuzzer+ASan+UBSan  FEHLT (clang-Runtime prüfen)"
    FAILED+=("libFuzzer/Sanitizer")
fi
rm -rf "$TMPD"

# OpenSSL 3.x?
if command -v openssl >/dev/null 2>&1; then
    case "$(openssl version)" in
        "OpenSSL 3"*) ;;
        *) echo "  Hinweis: OpenSSL ist nicht Version 3.x" ;;
    esac
fi

echo
if [ "${#FAILED[@]}" -eq 0 ]; then
    echo "Alles installiert."
else
    echo "Nicht installiert (Paketname auf dieser Distribution prüfen):"
    for f in "${FAILED[@]}"; do echo "  - $f"; done
    exit 2
fi
````
