#!/bin/bash
# Deprecated wrapper — use ansible/playbook-gnome-kiosk.yml directly.
set -euo pipefail

LIMIT="${LIMIT:-rhel10}"
if [ -n "${HOST:-}" ]; then
    case "$HOST" in
        192.168.122.79) LIMIT=fedora44 ;;
        192.168.122.81) LIMIT=rhel10 ;;
        *) LIMIT="$HOST" ;;
    esac
fi

cd "$(dirname "$0")/../ansible"
exec ansible-playbook playbook-gnome-kiosk.yml -l "$LIMIT" "$@"
