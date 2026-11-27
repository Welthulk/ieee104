# IEC 60870-5-104 Library @VERSION@ – fertig gebaut

Geteilte Library für Linux x86-64 (Release, mit TLS).

| Datei | Inhalt |
| --- | --- |
| `lib/libiec104.so.1` | Library |
| `include/iec104/iec104.h` | C-Schnittstelle mit Beschreibung jeder Funktion |
| `LICENSES/` | Lizenzen: Header Apache-2.0, Library proprietär |
| `julia/` | Anbindung für Julia (`IEC104.jl`) mit Client- und Server-Beispiel, Test-GUI im Browser (`gui/`) |
| `pruefen.sh` | prüft System, OpenSSL und Julia: `bash pruefen.sh` |
| `gui.sh` | startet Test-GUI und Prüfstand: `bash gui.sh`, dann `http://localhost:8080` |
| `beispiel.sh` | Julia-Station und -Leitstelle gegeneinander: `bash beispiel.sh` |

## Schnellstart (Linux oder WSL)

```bash
bash pruefen.sh        # alles bereit?
bash gui.sh            # Test-GUI, dann http://localhost:8080 im Browser
bash gui.sh 8080 0.0.0.0   # WSL: wenn Windows localhost nicht erreicht; angezeigte Adresse öffnen
bash gui.sh 8080 127.0.0.1 ~/src/lib60870/lib60870-C   # dazu lib60870 als fremde Leitstelle (ohne Angabe sucht gui.sh testgegenstelle/lib60870-C des Repos, in dem das Paket unter dist/ liegt)
bash beispiel.sh       # Station und Leitstelle in Julia gegeneinander
```

Unter WSL geht das direkt im Windows-Ordner, z. B. `cd /mnt/c/<pfad>/iec104-<version>-linux-x86_64`.

## Voraussetzungen

| | Mindestens | Beispiele |
| --- | --- | --- |
| glibc | 2.34 | AlmaLinux/RHEL 9, Ubuntu 22.04, Debian 12, WSL2 mit diesen |
| libstdc++ | GCC 11 | wie oben |
| OpenSSL | 3.0 (`libssl.so.3`) | Paket `openssl-libs` bzw. `libssl3` |

Prüfen: `ldd lib/libiec104.so.1` darf kein `not found` zeigen.

## Übersetzen und starten

```bash
gcc -Iinclude meine_app.c lib/libiec104.so.1 -Wl,-rpath,'$ORIGIN/lib' -o meine_app
./meine_app
```

`-Wl,-rpath,'$ORIGIN/lib'` lässt das Programm die Library in `lib/` neben sich finden. Liegt sie woanders: `LD_LIBRARY_PATH=/pfad/zu/lib ./meine_app`.

Mit `-liec104` statt Dateiname einmalig einen Link anlegen: `ln -s libiec104.so.1 lib/libiec104.so`, dann `gcc -Iinclude meine_app.c -Llib -liec104 …`.

## Kleinstes Programm

```c
#include <iec104/iec104.h>
#include <stdio.h>

int main(void) {
    printf("iec104 %s, TLS %s\n", iec104_version_string(), iec104_tls_available() ? "ja" : "nein");
    return 0;
}
```

## Julia

```bash
julia julia/server.jl 2404 &          # Station 1, Messwert IOA 100, steigt jede Sekunde
julia julia/client.jl 127.0.0.1 2404  # Generalabfrage, drei spontane Werte
```

Test-GUI und Prüfstand im Browser: `julia julia/gui/iec104_gui.jl`, dann `http://localhost:8080` öffnen (Leitstelle gegen eine Station mit Werteliste, Prozessabbild, Befehlslaufzeiten; Teststation, Selbsttest, Telegramme im Klartext; Anleitung `julia/` bzw. docs/07_Julia.md, Abschnitt 7.4).

`IEC104.jl` findet die Library in `lib/` von selbst. Anleitung: `docs/07_Julia.md` in den Quellen der Library.

Server und Client mit Messwerten, Befehlen, Redundanz und TLS: Anleitung `docs/06_Anleitung.md` in den Quellen der Library (Version @VERSION@).
