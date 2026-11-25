#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# setup_tools.sh – Werkzeuge für die IEC-104-Library (C++17, C-API, OpenSSL, Tests)
#
# Aufruf:
#   ./setup_tools.sh                  Grundausstattung installieren
#   ./setup_tools.sh --with-lib60870  zusätzlich lib60870-C als Testgegenstelle bauen
#                                     (GPLv3, nur für Tests, liegt außerhalb des Repos)
#   ./setup_tools.sh --with-aflpp     zusätzlich AFL++ aus den Quellen bauen
#                                     (für RHEL/AlmaLinux 9, dort gibt es kein Paket)
#
# Unterstützt: apt (Debian/Ubuntu und Ableger), dnf (Fedora/RHEL), zypper (openSUSE).
# Pakete werden einzeln installiert. Fehlt ein Paket, läuft das Skript weiter
# und listet es am Ende auf.

set -u

WITH_LIB60870=0
WITH_AFLPP=0
for arg in "$@"; do
    case "$arg" in
        --with-lib60870) WITH_LIB60870=1 ;;
        --with-aflpp) WITH_AFLPP=1 ;;
        -h|--help) sed -n '6,18p' "$0"; exit 0 ;;
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
        libgtest-dev libgmock-dev
        # Fuzzing
        afl++
        # Trace-Analyse, Netzwerk-Namespaces, Testverbindungen
        tshark iproute2 netcat-openbsd
        # Doku
        doxygen
    )
elif command -v dnf >/dev/null 2>&1; then
    PM="dnf"
    # Nicht erreichbare Zusatz-Paketquellen überspringen statt abbrechen
    UPDATE="$SUDO dnf -y --setopt=skip_if_unavailable=True makecache"
    INSTALL="$SUDO dnf install -y --setopt=skip_if_unavailable=True"
    PACKAGES=(
        gcc gcc-c++ make clang lld llvm cmake ninja-build pkgconf-pkg-config ccache git
        clang-tools-extra cppcheck
        gdb valgrind libasan libubsan compiler-rt
        openssl openssl-devel mbedtls-devel
        gtest-devel gmock-devel
        # AFL: kein Paket für RHEL/AlmaLinux 9, dafür --with-aflpp
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
        googletest-devel
        afl
        wireshark iproute2 netcat-openbsd
        doxygen
    )
else
    echo "Kein unterstützter Paketmanager gefunden (apt, dnf, zypper)."
    exit 1
fi

# AFL++ braucht für afl-clang-fast/-lto die LLVM- und Clang-Entwicklerpakete
if [ "$WITH_AFLPP" -eq 1 ]; then
    case "$PM" in
        apt)         PACKAGES+=(llvm-dev libclang-dev) ;;
        dnf|zypper)  PACKAGES+=(llvm-devel clang-devel) ;;
    esac
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
# Optional: AFL++ aus den Quellen (nur Quellcode-Modi, ohne QEMU/Frida/Unicorn)
# ---------------------------------------------------------------------------
AFLPP_DIR="${THIRD_PARTY_DIR}/AFLplusplus"
if [ "$WITH_AFLPP" -eq 1 ]; then
    echo "==> AFL++ nach ${AFLPP_DIR}"
    mkdir -p "$THIRD_PARTY_DIR"
    if [ ! -d "${AFLPP_DIR}/.git" ]; then
        git clone --depth 1 https://github.com/AFLplusplus/AFLplusplus.git \
            "$AFLPP_DIR" || FAILED+=("AFL++ (clone)")
    fi
    if [ -f "${AFLPP_DIR}/Makefile" ]; then
        make -C "$AFLPP_DIR" -j"$(nproc)" source-only >/dev/null 2>&1 \
            || FAILED+=("AFL++ (build, Log: make -C ${AFLPP_DIR} source-only)")
    fi
    case ":${PATH}:" in
        *":${AFLPP_DIR}:"*) ;;
        *)  echo "    In ~/.bashrc eintragen:"
            echo "      export PATH=\"\$HOME/src/third_party/AFLplusplus:\$PATH\""
            ;;
    esac
    # afl-system-config NICHT verwenden: schaltet u. a. ASLR und Audit ab.
    # Stattdessen nur für den Fuzzer-Prozess:
    echo "    afl-fuzz ohne Systemänderung starten mit:"
    echo "      AFL_SKIP_CPUFREQ=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 afl-fuzz ..."
    echo "    afl-system-config NICHT ausführen (schaltet ASLR und Audit ab)."
fi
# Für die Prüfung unten auch ein bereits gebautes AFL++ finden
[ -x "${AFLPP_DIR}/afl-fuzz" ] && PATH="${AFLPP_DIR}:${PATH}"

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
