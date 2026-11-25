# IEC 60870-5-104 Library @VERSION@ – fertig gebaut

Geteilte Library für Linux x86-64 (Release, mit TLS).

| Datei | Inhalt |
| --- | --- |
| `lib/libiec104.so.0` | Library |
| `include/iec104/iec104.h` | C-Schnittstelle mit Beschreibung jeder Funktion |
| `LICENSES/` | Lizenzen: Header Apache-2.0, Library proprietär |
| `julia/` | Anbindung für Julia (`IEC104.jl`) mit Client- und Server-Beispiel, Test-GUI im Browser (`gui/`) |

## Voraussetzungen

| | Mindestens | Beispiele |
| --- | --- | --- |
| glibc | 2.34 | AlmaLinux/RHEL 9, Ubuntu 22.04, Debian 12, WSL2 mit diesen |
| libstdc++ | GCC 11 | wie oben |
| OpenSSL | 3.0 (`libssl.so.3`) | Paket `openssl-libs` bzw. `libssl3` |

Prüfen: `ldd lib/libiec104.so.0` darf kein `not found` zeigen.

## Übersetzen und starten

```bash
gcc -Iinclude meine_app.c lib/libiec104.so.0 -Wl,-rpath,'$ORIGIN/lib' -o meine_app
./meine_app
```

`-Wl,-rpath,'$ORIGIN/lib'` lässt das Programm die Library in `lib/` neben sich finden. Liegt sie woanders: `LD_LIBRARY_PATH=/pfad/zu/lib ./meine_app`.

Mit `-liec104` statt Dateiname einmalig einen Link anlegen: `ln -s libiec104.so.0 lib/libiec104.so`, dann `gcc -Iinclude meine_app.c -Llib -liec104 …`.

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

Test-GUI im Browser: `julia julia/gui/iec104_gui.jl`, dann `http://localhost:8080` öffnen (Leitstelle und Station, Abfragen, Befehle, TLS).

`IEC104.jl` findet die Library in `lib/` von selbst. Anleitung: `docs/07_Julia.md` im Quellpaket.

Server und Client mit Messwerten, Befehlen, Redundanz und TLS: Anleitung `docs/06_Anleitung.md` im Quellpaket `iec104-@VERSION@.zip`.
