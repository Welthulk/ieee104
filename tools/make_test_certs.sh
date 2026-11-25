#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 SOPTIM AG
# SPDX-License-Identifier: LicenseRef-Proprietary
# Author: Udo Schmitz (SOPTIM AG)
# All rights reserved by SOPTIM AG.
# make_test_certs.sh – erzeugt eine kleine Test-PKI für die TLS-Tests.
#
# Aufruf: tools/make_test_certs.sh <zielverzeichnis>
#
# Nur für Tests: Schlüssel ohne Schutz, Namen localhost/127.0.0.1. Der Build
# ruft das Skript selbst auf (Verzeichnis build/<preset>/tls).
#
# | Datei                    | Inhalt                                             |
# |--------------------------|----------------------------------------------------|
# | ca.pem                   | Test-CA                                            |
# | server.pem/.key          | localhost, 127.0.0.1                               |
# | server2.pem/.key         | wie server, neuer Schlüssel (Zertifikatswechsel)   |
# | client.pem/.key          | client.test                                        |
# | client_enc.key           | Schlüssel von client, Passwort "test-password"     |
# | revoked.pem/.key         | von der CA gesperrt (crl.pem)                      |
# | expired.pem/.key         | gültig 2020 bis 2021                               |
# | crl.pem                  | Sperrliste der CA                                  |
# | foreign_ca.pem           | fremde CA                                          |
# | foreign.pem/.key         | localhost, von der fremden CA                      |

set -eu

OUT="${1:?Zielverzeichnis fehlt}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
WORK="$OUT/work"
rm -rf "$WORK"
mkdir -p "$WORK/new"

key() { openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$OUT/$1.key" 2>/dev/null; }

make_ca() { # name, common name
    key "$1"
    openssl req -x509 -new -key "$OUT/$1.key" -subj "/CN=$2" -days 3650 -sha256 \
        -addext "basicConstraints=critical,CA:TRUE" \
        -addext "keyUsage=critical,keyCertSign,cRLSign" -out "$OUT/$1.pem"
}

# Konfiguration der Test-CA für "openssl ca" (Datenbank für die Sperrliste).
cat > "$WORK/ca.cnf" <<EOF
[ ca ]
default_ca = test_ca

[ test_ca ]
database         = $WORK/index.txt
new_certs_dir    = $WORK/new
serial           = $WORK/serial
crlnumber        = $WORK/crlnumber
certificate      = $OUT/ca.pem
private_key      = $OUT/ca.key
default_md       = sha256
default_days     = 3650
default_crl_days = 3650
policy           = policy_any
unique_subject   = no
copy_extensions  = none

[ policy_any ]
commonName = supplied

[ leaf ]
basicConstraints       = CA:FALSE
keyUsage               = critical,digitalSignature,keyAgreement
extendedKeyUsage       = serverAuth,clientAuth
subjectAltName         = \$ENV::SAN
subjectKeyIdentifier   = hash
authorityKeyIdentifier = keyid
EOF
export SAN="DNS:localhost" # wird von der Konfiguration immer gelesen, auch beim Sperren
: > "$WORK/index.txt"
echo 1000 > "$WORK/serial"
echo 1000 > "$WORK/crlnumber"

leaf() { # name, common name, subjectAltName, [weitere Optionen für openssl ca]
    local name="$1" cn="$2" san="$3"
    shift 3
    key "$name"
    openssl req -new -key "$OUT/$name.key" -subj "/CN=$cn" -out "$WORK/$name.csr"
    SAN="$san" openssl ca -batch -notext -config "$WORK/ca.cnf" -extensions leaf \
        -in "$WORK/$name.csr" -out "$OUT/$name.pem" "$@" 2>/dev/null
}

make_ca ca "IEC104 Test CA"
leaf server localhost "DNS:localhost,IP:127.0.0.1"
leaf server2 localhost "DNS:localhost,IP:127.0.0.1"
leaf client client.test "DNS:client.test"
leaf revoked revoked.test "DNS:revoked.test,DNS:localhost,IP:127.0.0.1"
leaf expired expired.test "DNS:expired.test,DNS:localhost,IP:127.0.0.1" \
    -startdate 20200101000000Z -enddate 20210101000000Z

openssl pkey -in "$OUT/client.key" -aes256 -passout pass:test-password -out "$OUT/client_enc.key"

openssl ca -batch -config "$WORK/ca.cnf" -revoke "$OUT/revoked.pem" 2>/dev/null
openssl ca -batch -config "$WORK/ca.cnf" -gencrl -out "$OUT/crl.pem" 2>/dev/null

# Fremde CA mit einem Zertifikat auf dieselben Namen.
make_ca foreign_ca "Foreign Test CA"
key foreign
openssl req -new -key "$OUT/foreign.key" -subj "/CN=localhost" -out "$WORK/foreign.csr"
printf 'basicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth,clientAuth\nsubjectAltName=DNS:localhost,IP:127.0.0.1\n' \
    > "$WORK/foreign.ext"
openssl x509 -req -in "$WORK/foreign.csr" -CA "$OUT/foreign_ca.pem" -CAkey "$OUT/foreign_ca.key" \
    -CAcreateserial -days 3650 -sha256 -extfile "$WORK/foreign.ext" -out "$OUT/foreign.pem" 2>/dev/null

rm -rf "$WORK" "$OUT"/*.srl
echo "make_test_certs: $OUT"
