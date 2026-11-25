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
