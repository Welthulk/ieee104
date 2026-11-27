# Testgegenstelle lib60870 (fremde Software, GPLv3)

Dieser Ordner enthält drei gebaute Beispielprogramme der Bibliothek lib60870 (Version 2.4.1). Die Test-GUI startet sie als **fremde Leitstelle** gegen ihre eigene Teststation (Reiter „Station und lib60870“, Selbsttest Schritt 27).

| Datei | Inhalt |
| --- | --- |
| `lib60870-C/build/examples/cs104_client/simple_client` | verbindet, STARTDT, Generalabfrage, Testbefehl, endet nach etwa 9 s |
| `lib60870-C/build/examples/cs104_client_async/cs104_client_async` | bleibt verbunden, Generalabfrage alle 10 s |
| `lib60870-C/build/examples/cs104_client_no_threads/cs104_client_no_threads` | bleibt verbunden, nur STARTDT |
| `COPYING` | Lizenz: GNU General Public License, Version 3 |
| `lib60870-2.4.1-quellen.tar.gz` | vollständige Quellen genau dieses Stands (Tag v2.4.1) |

## Abgrenzung

- lib60870 ist **nicht Teil der IEC-104-Library**. Die Library linkt nichts davon und enthält keinen Code daraus; das Binärpaket `iec104-…-linux-x86_64.zip` enthält diesen Ordner nicht.
- Die Programme laufen nur als eigene Prozesse, ausschließlich zum Testen.
- Für sie gilt die GPLv3 (`COPYING`). Wer sie weitergibt, gibt `COPYING` und das Quellarchiv mit.

## Verwendung

`gui.sh` findet den Ordner selbst, wenn das Binärpaket unter `dist/` dieses Repos liegt; beim Start steht dann „Gegenstelle lib60870: …/testgegenstelle/lib60870-C“. Sonst den Ordner angeben:

```bash
bash gui.sh 8080 127.0.0.1 <repo>/testgegenstelle/lib60870-C
```

## So gebaut

Linux x86-64, Release, ohne TLS, Programme gestrippt; sie brauchen nur die C-Laufzeit (glibc ab 2.34).

```bash
tar xzf lib60870-2.4.1-quellen.tar.gz
cmake -S lib60870-2.4.1/lib60870-C -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target simple_client cs104_client_async cs104_client_no_threads
```
