#!/usr/bin/env bash
# Creates a new app from src/apps/_template/ (docs/APPS.md "Checklist for a new app").
#   tools/new-app.sh <id> "<Name>" <Icon>
#   tools/new-app.sh weather "Weather" Sun
# <Icon> is the CamelCase name in src/gen/icons.h (Lucide "sun" -> Sun). If it is missing, add the
# Lucide name to assets/icons.txt and run tools/gen-assets.sh first.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ $# -ne 3 ]; then
  echo 'usage: tools/new-app.sh <id> "<Name>" <Icon>' >&2
  exit 2
fi
id=$1 name=$2 icon=$3

case "$id" in
  [a-z]*) ;;
  *) echo "new-app: id must start with a lowercase letter" >&2; exit 2 ;;
esac
if ! printf '%s' "$id" | grep -Eq '^[a-z][a-z0-9_]*$'; then
  echo "new-app: id may only contain a-z, 0-9 and _" >&2
  exit 2
fi
if [ -e "src/apps/$id" ]; then
  echo "new-app: src/apps/$id already exists" >&2
  exit 1
fi
if ! grep -Eq "^  $icon,\$" src/gen/icons.h; then
  echo "new-app: icon '$icon' is not in src/gen/icons.h." >&2
  echo "         Add its Lucide name to assets/icons.txt and run tools/gen-assets.sh." >&2
  exit 1
fi
if printf '%s' "$name" | grep -q '[\\"]'; then
  echo "new-app: the name may not contain a quote or backslash" >&2
  exit 2
fi

# CamelCase class prefix from the id: my_app -> MyApp
class=$(printf '%s' "$id" | awk -F_ '{ for (i = 1; i <= NF; i++) printf "%s%s", toupper(substr($i, 1, 1)), substr($i, 2) }')

mkdir -p "src/apps/$id"
sed -e "s/__ID__/$id/g" -e "s/__NAME__/$name/g" -e "s/__ICON__/$icon/g" -e "s/__CLASS__/$class/g" \
  "src/apps/_template/__ID___app.cpp" > "src/apps/$id/${id}_app.cpp"

cat > "tests/ui/$id.script" <<SCRIPT
# app: $id
# First screen of $name. Run make ui-test, look at out/ui/$id-home.png, then make ui-accept.
wait 300
shot $id-home
SCRIPT

cat > "docs/apps/$id.md" <<DOC
# $name

Purpose: (one line)

## Screens

(list each screen and what it shows)

## Data sources and config keys

(none yet)
DOC

echo "created src/apps/$id/${id}_app.cpp, tests/ui/$id.script, docs/apps/$id.md"
echo "next: make sim && program --app $id   (or make run), then make ui-test and make ui-accept"
