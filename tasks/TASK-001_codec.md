# TASK-001: Codec vervollständigen (Informationselemente und ASDUs)

Status: erledigt (25.11.2026) · Modul: `src/codec` · Grundlage: `docs/02_Konzept` (Architektur, ASDU-Profil), `docs/03_Recherchebericht` (Typkennungen, COT, Elemente)

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

- [x] Alle Typen aus Punkt 2 mit Roundtrip-Test (SQ = 0 und SQ = 1, wo zulässig).
- [x] Je Typgruppe mindestens ein Referenz-Hexdump mit Quellenangabe im Kommentar (Wireshark-Dissector, Herstellerdoku). Keine selbst erfundenen „Referenzen“.
- [x] Negativtests: zu kurze Puffer, Objektanzahl passt nicht zur Länge, unbekannte Typkennung, CP24 im strikten Modus.
- [x] `ctest --preset debug` und `ctest --preset asan` grün, `clang`-Preset ohne clang-tidy-Befunde in neuem Code.
- [x] Fuzzer läuft 10 Minuten ohne Befund (600 s, 210 Mio. Eingaben, Korpus aus `tools/make_fuzz_seeds.sh`).
- [x] Offene Normfragen in diesem Dokument unter „Offen“ eingetragen.

## Umsetzung (Stand 25.11.2026)

| Datei | Inhalt |
| --- | --- |
| `src/codec/elements.hpp/.cpp` | Elemente als Structs bzw. Bitmasken, Kodieren/Dekodieren/Bereichsprüfung |
| `src/codec/type_table.hpp/.cpp` | 59 Typen (Umfang Punkt 2 plus CP24-Typen), Elementliste, Zeitmarke, Richtung; leere Matrix 9.5 |
| `src/codec/asdu.hpp/.cpp` | `decode_asdu`, `encode_asdu`, `encode_asdu_fitting` (Aufteilen), strikt/tolerant, private Typen roh |
| `src/codec/asdu_header.hpp` | `AsduProfile` um `max_asdu_length` (Default 249) und `strict` (Default an) erweitert |
| `tests/data/lib60870_asdu_reference.txt` | 72 Referenz-ASDUs aller Typen (SQ 0/1, abweichendes Profil), kodiert von lib60870-C 2.4.1, gegengeprüft mit Wireshark 3.4.10 |
| `tests/test_elements.cpp`, `test_type_table.cpp`, `test_asdu.cpp`, `test_asdu_reference.cpp` | 41 neue Tests; `test_capture.cpp` prüft jetzt auch vollständige ASDUs |
| `fuzz/fuzz_codec.cpp`, `tools/make_fuzz_seeds.sh` | vollständige ASDUs in drei Profilen; Seeds aus `tests/data/` |

Entwurfsentscheidungen:

- **Verlustfreies Dekodieren:** Jedes empfangene Bit bleibt erhalten, auch reservierte Bits. Nur so gilt die Fuzz-Invariante „Dekodieren → Kodieren → identische Bytes“. Folge: CP56Time2a behält jetzt RES1..RES4 (vorher wurden RES2..RES4 verworfen, `docs/04` A6).
- **Dekodieren tolerant, Kodieren streng:** Werte außerhalb des Bereichs (z. B. Minute 63) werden dekodiert, beim Kodieren aber mit `IEC104_ERR_OUT_OF_RANGE` abgelehnt, nie abgeschnitten.
- **Strikt** lehnt CP24-Typen ab (`UNSUPPORTED`) und ASDUs mit 0 Objekten (`MALFORMED`); tolerant nimmt beides an.
- **Unbekannte Typen** ergeben `IEC104_ERR_UNSUPPORTED` (Grundlage für COT 44 in der Session). Private Typen 136–255 werden nur dekodiert (roh); Kodieren privater Typen ist noch nicht vorgesehen.
- Der Generator der Referenz-ASDUs linkt lib60870 (GPL) und liegt deshalb nicht im Repo; Vorgehen und Eingabewerte stehen im Kopf und je Zeile der Datendatei.

## Offen

| Nr. | Thema | Stand im Code |
| --- | --- | --- |
| O1 | Wireshark 3.4.10 kennt die Typen 17, 18, 19, 104, 106 nicht und zeigt 20, 38–40, 101, 107 nur als Rohdaten. Für SCD, SEP, SPE, OCI, QDP, QCC, FBP, TSC ist lib60870 die einzige Quelle der Bitpositionen. | Prüfen mit neuerer Wireshark-Version oder Normtext |
| O2 | Matrix Typ/COT (9.5) | leere Tabelle, `TODO(norm): 9.5`, alles erlaubt (N3) |
| O3 | SQ = 1: für welche Typen zulässig | Codec erlaubt SQ = 1 für alle Typen, `TODO(norm)` in `asdu.hpp` |
| O4 | CP24-Typen in 104 (N4), Typen 104 und 106 in 104 (N5), VSQ 0 (N9) | wie in `docs/04` angenommen, jeweils `TODO(norm)` |
| O5 | Wertebedeutungen QU 4..31, COI 3..127, KPA 3..63, QCC, Nummerierung ST/CD in SCD | nur Rohwerte, `TODO(norm)` in `elements.hpp` |
| O6 | Wertebereich CP16Time2a | jeder 16-Bit-Wert wird geschrieben, `TODO(norm)` in `asdu.cpp` |
| O7 | Bedeutung der reservierten Bits (z. B. RES1 der Zeitmarke) | erhalten, ohne Bedeutung |
| O8 | Abschnittsnummern der Elemente und der Typtabelle | `TODO(norm): clause unknown` |
