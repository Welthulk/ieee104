# Normfragen und Festlegungen

Stand: 25.11.2026 · Normstand IEC 60870-5-104 Ed. 2.1 mit AMD1:2016. Code-Stellen zu N1–N13 sind mit `TODO(norm)` markiert; E1–E10 sind technische Festlegungen. Belege: `docs/02_Technik_und_Quellen.md`, `docs/03_Quellen_APCI.md`.

## Normfragen

| Nr. | Thema (Normabschnitt) | Modul | Auswirkung | Umsetzung |
| --- | --- | --- | --- | --- |
| N1 | Zustandsverhalten der Verbindung (104, Abschnitt 5) | apci | Wann I-Rahmen erlaubt sind, wann geschlossen wird | Regeln R1–R12 nach öffentlichen Belegen, siehe `docs/03_Quellen_APCI.md`; unbelegte Punkte (N(R) außerhalb des Fensters, Verhalten bei Erreichen von k, Startbedingung von t3) mit `TODO(norm)` |
| N2 | Bedeutung von t1 | apci | Einige Geräte überwachen mit t1 nur U-Rahmen | t1 überwacht I-Rahmen und U-Rahmen act (Bild 17 des Normentwurfs, Interoperabilitätsliste) |
| N3 | Zulässige Kombinationen Typkennung/Übertragungsursache (9.5) | codec, session | Prüfung eingehender ASDUs, negative Quittung | Tabelle angelegt, aber leer; jede Kombination ist erlaubt (`is_cause_allowed()`) |
| N4 | Typen mit CP24Time2a (2–16 gerade, 17–19) in IEC 104 | codec | Ablehnen oder dekodieren | Strikt: `IEC104_ERR_UNSUPPORTED`. Tolerant: dekodieren und kodieren |
| N5 | Typen 104 (Testbefehl ohne Zeitmarke) und 106 in IEC 104 | codec | Unterstützung | Beide in der Typtabelle. Öffentliche Wiedergaben der Interoperabilitätsliste führen beide in IEC 104 als nicht vorgesehen (schwarzes Feld); die Station spiegelt 104 trotzdem und lehnt 106 ab (docs/09) |
| N6 | Maximale Befehlsverzögerung (AMD1, Abschnitt 6) | session | Zu spät eintreffende Befehle werden verworfen | Nur für Befehle mit CP56Time2a (58–64, 107), Default aus; Vergleich mit der UTC-Uhr des Servers; eine ungültige Zeitmarke (IV) wird bei eingeschalteter Prüfung ebenfalls verworfen; Zeitmarken in der Zukunft werden nicht geprüft |
| N7 | Redundanzgruppen (Abschnitt 10) | session | Umschalten mit unquittierten I-Rahmen | Ereignispuffer gehört der Gruppe; ein Ereignis bleibt darin, bis die Gegenstelle es quittiert (N(R)). Nach Abbruch oder Umschaltung geht Unquittiertes auf der nächsten aktiven Verbindung erneut hinaus: doppelt möglich, kein Verlust. Gepuffert werden alle Ursachen außer zyklisch (1) und Hintergrund (2) |
| N8 | Abläufe Generalabfrage, Befehle, Initialisierung (Abschnitt 7) | session | Reihenfolge ACTCON / Daten / ACTTERM, Typ 70 nach Neustart, Typen der Antwort | Übliche Lesart: Aktivierung, Bestätigung, Daten, Beendigung; Antworten auf eine Abfrage ohne Zeitmarke; Typ 70 mit COT 4 nach dem ersten STARTDT einer Verbindung (abschaltbar); Freeze/Reset der Zählerabfrage nur bestätigt |
| N9 | ASDU mit 0 Objekten (VSQ = 0) | codec | Dekodieren | Strikt: `IEC104_ERR_MALFORMED`. Tolerant: angenommen, wenn keine Objektoktette folgen. Kodieren mit 0 Objekten ist nie möglich |
| N10 | Gemeinsame Adresse 0 und 65535 (Broadcast) | session | Annahme von Generalabfrage und Uhrzeit als Broadcast | 65535 (bei 1-Oktett-CA 255) nur bei 100, 101, 103, 105, beantwortet je Station mit deren Adresse; andere unbekannte Adressen negativ mit Ursache 46 |
| N11 | Inhalt von COR1:2023 | alle | Unbekannte Korrekturen | Keine Annahme |
| N12 | TLS nach IEC 62351-3 Ed. 2: Cipher-Suites (Tabelle 1), Resumption und Renegotiation (7.4.4, 7.4.5), Key Update (8.4) | transport | Defaults der TLS-Konfiguration | TLS 1.2 und 1.3; für 1.2 nur ECDHE mit AEAD (GCM, ChaCha20-Poly1305), für 1.3 die Suites ohne CCM; gegenseitige Authentifizierung mit X.509; Renegotiation nur nach RFC 5746, in beide Richtungen erlaubt, abschaltbar; Schlüsselerneuerung nach Zeit, Default aus; Session Resumption an, Lebensdauer 1 h. Alles je Handle einstellbar (`iec104_tls_config`) |
| N13 | Zeitbasis UTC oder Ortszeit | session | Bewertung von Zeitmarken | Nicht genormt; Zeitbasis wird Parameter (UTC / fester Offset / Ortszeit mit SU). CP56Time2a im Codec ohne Zeitzone |

## Technische Festlegungen

| Nr. | Festlegung | Begründung |
| --- | --- | --- |
| E1 | Strikter Modus ist Default (`AsduProfile::strict`, `ApciConfig::strict`, C-API `tolerant = 0`) | Normverhalten als Ausgangspunkt; Abweichungen einer Gegenstelle werden bewusst eingeschaltet, nie im Code fest verdrahtet |
| E2 | Die Empfehlungen t2 < t1 und w ≤ 2/3 k werden standardmäßig nur als Warnung behandelt; wählbar Warnung / Fehler / aus | Im Feld kommen Kombinationen wie t1 = 250 s, t2 = 240 s vor, die ein striktes Ablehnen unbrauchbar machen würden |
| E3 | t3 = 0 (Verbindungstest abgeschaltet) nur im toleranten Modus | Die Interoperabilitätsliste empfiehlt 1 s bis 48 h; einige Geräte verlangen 0 |
| E4 | Version 1 nur für Linux x86-64 | Zielplattform; der Transport nutzt POSIX-Sockets und `poll` |
| E5 | Dekodieren ist verlustfrei: auch reservierte Bits bleiben erhalten (z. B. RES1–RES4 der CP56Time2a). Kodieren prüft Wertebereiche und schneidet nie ab | Eine dekodierte ASDU ergibt beim erneuten Kodieren dieselben Oktette (wichtig für Gateways, Mitschnitte und den Fuzzer); Programmierfehler werden nicht zu falschen Daten auf der Leitung |
| E6 | Die Library testet sich selbst: Zustandsmaschinen im Speicher gekoppelt (ohne Netz) und Selbsttest-Programme über TCP | Prüfung ohne fremde Gegenstelle, Grundlage für manuelle Tests (`docs/04_Selbsttest.md`) |
| E7 | Befehlsmodell je IOA: ohne Befehlspunkte entscheidet die Anwendung allein; mit Befehlspunkten prüft die Library IOA, Typ, Select/Execute und Auswahl-Timeout, bevor die Anwendung gefragt wird. Eine Auswahl gilt je Verbindung | Bekannte Anforderungen von Gegenstellen (Select/Execute je Datenpunkt, nur Befehle mit Zeitmarke) werden Parameter statt Anwendungslogik |
| E8 | Laufzeitmodell: kein interner Thread; die Anwendung ruft `*_poll()` in ihrem Thread, dort laufen auch alle Callbacks. Ein Handle gehört einem Thread | keine Sperren in der Library, vorhersagbares Verhalten; die Anwendung wählt selbst, ob Poll in einem eigenen Thread läuft |
| E9 | TLS: Der Handshake fällt unter t0, auf Client und Server. Ein Fehler im Handshake oder in der TLS-Schicht schließt die Verbindung (`IEC104_CLOSE_TLS`) und wird als Sicherheitsereignis gemeldet (`on_security`). Neue TLS-Einstellungen gelten für neue Verbindungen, offene behalten ihre Sitzung | t0 begrenzt den Verbindungsaufbau; ohne Grenze hält eine Gegenstelle, die nur TCP öffnet, Ressourcen des Servers fest. Zertifikate lassen sich so ohne Neustart wechseln |
| E10 | Redundanzgruppen: Aktiv ist die zuletzt mit STARTDT gestartete Verbindung; STARTDT auf einer zweiten Verbindung übernimmt (Warnung im Log), STOPDT der aktiven übergibt an eine andere noch gestartete. Zuordnung neuer Verbindungen über die IP-Adresse der Leitstelle, sonst an die Gruppe ohne Adressliste, sonst abgewiesen. Ohne konfigurierte Gruppen ist jede Verbindung ihre eigene Gruppe. Puffer 1000 Ereignisse, bei Überlauf wird das älteste verworfen (umstellbar auf Ablehnen) | Eine Leitstelle, die ohne STOPDT umschaltet, verliert keine Daten. Die Adressliste wirkt zugleich als Absenderprüfung. Ohne Gruppen bleibt das bisherige Verhalten erhalten, ergänzt um Pufferung während STOPDT |
