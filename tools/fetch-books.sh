#!/usr/bin/env bash
# Downloads a few public-domain books from Project Gutenberg into sim-data/sd/books/
# (the simulator's SD card). The files are not committed: run this once after cloning.
#
#   tools/fetch-books.sh            # skip files that already exist
#
# The set covers the reader's cases: EPUB3 with images, EPUB without images, Dutch text with
# accented characters, a long novel, and plain text. Be polite: one request every 2 seconds.
set -euo pipefail

dest="$(cd "$(dirname "$0")/.." && pwd)/sim-data/sd/books"
mkdir -p "$dest"

# id | file name | Gutenberg format suffix
books=(
  "1342|Pride and Prejudice - Jane Austen.epub|epub.noimages"
  "1661|The Adventures of Sherlock Holmes - Arthur Conan Doyle.epub|epub.noimages"
  "84|Frankenstein - Mary Shelley.epub|epub3.images"
  "11|Alice's Adventures in Wonderland - Lewis Carroll.epub|epub3.images"
  "11024|Max Havelaar - Multatuli.epub|epub.noimages"
  "35|The Time Machine - H. G. Wells.txt|txt.utf-8"
)

for entry in "${books[@]}"; do
  IFS='|' read -r id name suffix <<<"$entry"
  out="$dest/$name"
  if [ -s "$out" ]; then
    echo "have   $name"
    continue
  fi
  echo "fetch  $name"
  if curl -fsSL --max-time 120 -A "paperos-fetch-books" -o "$out.part" "https://www.gutenberg.org/ebooks/$id.$suffix"; then
    mv "$out.part" "$out"
  else
    rm -f "$out.part"
    echo "FAILED $name (id $id)" >&2
  fi
  sleep 2
done

ls -lh "$dest"
