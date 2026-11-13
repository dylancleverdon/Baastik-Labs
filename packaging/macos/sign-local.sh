#!/bin/bash
# Signs Cymatica with a self-signed code-signing identity that is created on (and never leaves)
# this Mac. macOS ties the microphone permission to an app's signature; an ad-hoc signature changes
# with every build, so without this every update would silently lose the permission. With a stable
# identity, "Allow microphone" is asked once and survives updates.
#
#   sign-local.sh ensure                 create the identity if it doesn't exist yet (idempotent)
#   sign-local.sh sign <Cymatica.app>    sign with it; prints "identity" (or "adhoc" as a fallback)
#   sign-local.sh requirement <app>      print the app's designated requirement
#
# Everything lives in CYMATICA_SUPPORT_DIR (default ~/Library/Application Support/Cymatica)/signing:
# a dedicated keychain with a random password, so nothing touches the login keychain and no
# password prompt ever appears.
set -euo pipefail

SUPPORT="${CYMATICA_SUPPORT_DIR:-$HOME/Library/Application Support/Cymatica}"
DIR="$SUPPORT/signing"
KC="$DIR/cymatica-signing.keychain-db"
PWFILE="$DIR/keychain-password"
IDFILE="$DIR/identity-sha1"
NAME="Cymatica Local Signing"
BUNDLE_ID="com.baastiklabs.cymatica"
OPENSSL=/usr/bin/openssl

have_identity() {
  [ -f "$KC" ] && [ -f "$IDFILE" ] && [ -f "$PWFILE" ] || return 1
  security unlock-keychain -p "$(cat "$PWFILE")" "$KC" 2>/dev/null || return 1
  security find-identity -p codesigning "$KC" 2>/dev/null | grep -qi "$(cat "$IDFILE")"
}

ensure() {
  if have_identity; then
    return 0
  fi
  mkdir -p "$DIR"
  chmod 700 "$DIR"
  rm -f "$KC" "$IDFILE"
  local pw tmp
  pw="$(LC_ALL=C tr -dc 'A-Za-z0-9' </dev/urandom | head -c 32)"
  printf '%s' "$pw" >"$PWFILE"
  chmod 600 "$PWFILE"
  tmp="$(mktemp -d)"
  cat >"$tmp/cert.cnf" <<EOF
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = $NAME
O = Baastik Labs (this Mac only)
[ext]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, codeSigning
subjectKeyIdentifier = hash
EOF
  "$OPENSSL" req -x509 -newkey rsa:2048 -nodes -days 7300 -config "$tmp/cert.cnf" \
    -keyout "$tmp/key.pem" -out "$tmp/cert.pem" >/dev/null 2>&1
  "$OPENSSL" pkcs12 -export -inkey "$tmp/key.pem" -in "$tmp/cert.pem" -name "$NAME" \
    -out "$tmp/identity.p12" -passout "pass:$pw" >/dev/null 2>&1
  security create-keychain -p "$pw" "$KC"
  security set-keychain-settings "$KC" # never auto-lock
  security unlock-keychain -p "$pw" "$KC"
  security import "$tmp/identity.p12" -k "$KC" -P "$pw" -T /usr/bin/codesign >/dev/null
  security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$pw" "$KC" >/dev/null 2>&1 || true
  "$OPENSSL" x509 -in "$tmp/cert.pem" -noout -fingerprint -sha1 | sed 's/.*=//; s/://g' >"$IDFILE"
  rm -rf "$tmp"
  have_identity
}

sign() {
  local app="$1"
  if ensure >/dev/null 2>&1; then
    if codesign --force --keychain "$KC" --sign "$(cat "$IDFILE")" --identifier "$BUNDLE_ID" \
      "$app" >/dev/null 2>&1; then
      echo identity
      return 0
    fi
  fi
  codesign --force --sign - --identifier "$BUNDLE_ID" "$app" >/dev/null 2>&1
  echo adhoc
}

case "${1:-}" in
  ensure) ensure ;;
  sign) sign "${2:?usage: sign-local.sh sign <app>}" ;;
  requirement) codesign -d -r- "${2:?usage: sign-local.sh requirement <app>}" 2>&1 | sed -n 's/^designated => //p' ;;
  *)
    echo "usage: sign-local.sh ensure | sign <app> | requirement <app>" >&2
    exit 2
    ;;
esac
