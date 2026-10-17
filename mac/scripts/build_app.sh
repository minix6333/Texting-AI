#!/bin/zsh
# Builds ZhuyinLLM.app (IMK input method) into ./build, ad-hoc signed.
set -euo pipefail
cd "$(dirname "$0")/.."
MODEL="${MODEL:-$(ls ~/.cache/huggingface/hub/models--unsloth--Qwen3-0.6B-GGUF/snapshots/*/Qwen3-0.6B-Q4_K_M.gguf | head -1)}"
swift build -c release --product ZhuyinLLM
APP=build/ZhuyinLLM.app.staging   # assembled aside, then swapped in
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp .build/release/ZhuyinLLM "$APP/Contents/MacOS/ZhuyinLLM"
cp Resources/Info.plist "$APP/Contents/Info.plist"
# dictionaries shared with the Windows version live in ../data
cp ../data/tsi.csv ../data/en_freq.txt ../data/en_misspellings.txt ../data/en_known_words.txt "$APP/Contents/Resources/"
cp -R Resources/zh-Hant.lproj Resources/en.lproj "$APP/Contents/Resources/"
# the model is installed outside the bundle (see install.sh); keep a copy next to the app
[ -f build/model.gguf ] && cmp -s "$MODEL" build/model.gguf || cp -c "$MODEL" build/model.gguf
swift scripts/make_icons.swift "$APP/Contents/Resources"
codesign --force --sign - "$APP"
rm -rf build/ZhuyinLLM.app
mv "$APP" build/ZhuyinLLM.app
echo "built build/ZhuyinLLM.app ($(du -sh build/ZhuyinLLM.app | cut -f1))"
