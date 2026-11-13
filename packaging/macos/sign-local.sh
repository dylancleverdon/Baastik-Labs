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
# a dedicated keychain with a random password, so the login keychain isn't touched. The key is
# imported with access for any app (-A) so no permission dialog can ever appear, and every
# security/codesign call runs under a watchdog: if anything would hang, we fall back to ad-hoc.
set -uo pipefail

SUPPORT="${CYMATICA_SUPPORT_DIR:-$HOME/Library/Application Support/Cymatica}"
DIR="$SUPPORT/signing"
KC="$DIR/cymatica-signing.keychain-db"
PWFILE="$DIR/keychain-password"
IDFILE="$DIR/identity-sha1"
LOG="$DIR/signing.log"
NAME="Cymatica Local Signing"
BUNDLE_ID="com.baastiklabs.cymatica"
OPENSSL=/usr/bin/openssl
TIMEOUT="${CYMATICA_SIGN_TIMEOUT:-60}"

log() { mkdir -p "$DIR" && echo "$(date '+%F %T') $*" >>"$LOG"; }

if [ -n "${CYMATICA_SIGN_DEBUG:-}" ]; then
  mkdir -p "$DIR"
  exec 2>>"$LOG"
  set -x
fi

# Runs a command with a time limit (macOS has no `timeout`; perl's alarm does the job).
limited() {
  /usr/bin/perl -e 'alarm shift @ARGV; exec @ARGV or die "exec: $!"' "$TIMEOUT" "$@"
}

have_identity() {
  [ -f "$KC" ] && [ -f "$IDFILE" ] && [ -f "$PWFILE" ] || return 1
  limited security unlock-keychain -p "$(cat "$PWFILE")" "$KC" >>"$LOG" 2>&1 || return 1
  limited security find-identity -p codesigning "$KC" 2>>"$LOG" | grep -qi "$(cat "$IDFILE")"
}

add_to_search_list() {
  # codesign looks identities up through the user's keychain search list: append ours (keeping
  # whatever is already there).
  local current
  current=$(security list-keychains -d user | sed -e 's/^ *"//' -e 's/" *$//')
  if ! printf '%s\n' "$current" | grep -qxF "$KC"; then
    # shellcheck disable=SC2086
    IFS=$'\n' read -r -d '' -a list < <(printf '%s\n' "$current" && printf '\0')
    limited security list-keychains -d user -s "${list[@]}" "$KC" >>"$LOG" 2>&1 || true
  fi
}

ensure() {
  if have_identity; then
    add_to_search_list
    return 0
  fi
  mkdir -p "$DIR"
  chmod 700 "$DIR"
  log "creating signing identity"
  limited security delete-keychain "$KC" >>"$LOG" 2>&1 || true
  rm -f "$KC" "$IDFILE"
  local pw tmp
  # (not `tr < /dev/urandom | head`: where SIGPIPE is ignored, tr never stops reading)
  pw="$("$OPENSSL" rand -hex 16)"
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
    -keyout "$tmp/key.pem" -out "$tmp/cert.pem" >>"$LOG" 2>&1 || { rm -rf "$tmp"; return 1; }
  "$OPENSSL" pkcs12 -export -inkey "$tmp/key.pem" -in "$tmp/cert.pem" -name "$NAME" \
    -out "$tmp/identity.p12" -passout "pass:$pw" >>"$LOG" 2>&1 || { rm -rf "$tmp"; return 1; }
  limited security create-keychain -p "$pw" "$KC" >>"$LOG" 2>&1 || { rm -rf "$tmp"; return 1; }
  limited security set-keychain-settings "$KC" >>"$LOG" 2>&1 # never auto-lock
  limited security unlock-keychain -p "$pw" "$KC" >>"$LOG" 2>&1
  if ! limited security import "$tmp/identity.p12" -k "$KC" -P "$pw" -f pkcs12 -A -T /usr/bin/codesign >>"$LOG" 2>&1; then
    log "PKCS#12 import failed; importing the key and certificate separately"
    limited security import "$tmp/key.pem" -k "$KC" -t priv -f openssl -A -T /usr/bin/codesign >>"$LOG" 2>&1 \
      || { rm -rf "$tmp"; return 1; }
    limited security import "$tmp/cert.pem" -k "$KC" -t cert -f pemseq >>"$LOG" 2>&1 \
      || { rm -rf "$tmp"; return 1; }
  fi
  limited security set-key-partition-list -S apple-tool:,apple:,codesign:,unsigned: -s -k "$pw" "$KC" >>"$LOG" 2>&1 \
    || log "set-key-partition-list failed (codesign may still work)"
  "$OPENSSL" x509 -in "$tmp/cert.pem" -noout -fingerprint -sha1 | sed 's/.*=//; s/://g' >"$IDFILE"
  rm -rf "$tmp"
  add_to_search_list
  security find-identity -p codesigning "$KC" >>"$LOG" 2>&1 || true
  have_identity || { log "the new identity isn't usable for code signing"; return 1; }
}

sign() {
  local app="$1"
  if ensure; then
    limited security unlock-keychain -p "$(cat "$PWFILE")" "$KC" >>"$LOG" 2>&1
    if limited codesign --force --keychain "$KC" --sign "$(cat "$IDFILE")" --identifier "$BUNDLE_ID" \
      "$app" >>"$LOG" 2>&1; then
      log "signed $app with the local identity"
      echo identity
      return 0
    fi
    log "signing with the local identity failed; falling back to ad-hoc"
  else
    log "could not create the local identity; falling back to ad-hoc"
  fi
  limited codesign --force --sign - --identifier "$BUNDLE_ID" "$app" >>"$LOG" 2>&1
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
