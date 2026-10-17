#!/bin/sh
# Downloads the public-domain text and misspelling lists used to build bench/en_eval.json.
set -e
cd "$(dirname "$0")/.."
mkdir -p en_data
for f in big.txt spell-testset1.txt spell-testset2.txt; do curl -sSfL -o en_data/$f https://norvig.com/$f; done
echo "now: python3 bench/scripts/make_en_eval.py ."
