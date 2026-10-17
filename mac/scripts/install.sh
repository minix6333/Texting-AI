#!/bin/zsh
# Installs build/ZhuyinLLM.app into ~/Library/Input Methods and registers it.
set -euo pipefail
cd "$(dirname "$0")/.."
DEST="$HOME/Library/Input Methods"
mkdir -p "$DEST"
pkill -f "Input Methods/ZhuyinLLM.app/Contents/MacOS/ZhuyinLLM" 2>/dev/null || true
# update in place: removing the bundle of an enabled input source can make System Settings
# (if it is open) hold a stale entry, so adding a mode there silently does nothing
mkdir -p "$DEST/ZhuyinLLM.app"
rsync -a --delete build/ZhuyinLLM.app/ "$DEST/ZhuyinLLM.app/"
xattr -dr com.apple.quarantine "$DEST/ZhuyinLLM.app" 2>/dev/null || true
SUPPORT="$HOME/Library/Application Support/ZhuyinLLM"
mkdir -p "$SUPPORT"
cmp -s build/model.gguf "$SUPPORT/model.gguf" || cp -c build/model.gguf "$SUPPORT/model.gguf"
# Launch the new build once now: macOS scans a new executable on its first launch (seconds),
# and keys typed while the input method is held up by that scan are lost.
t0=$(date +%s)
"$DEST/ZhuyinLLM.app/Contents/MacOS/ZhuyinLLM" --preflight
echo "first-launch check done in $(( $(date +%s) - t0 ))s"
# start it now (in the background) so the first keys after an update are not typed raw
open -g "$DEST/ZhuyinLLM.app" || true
swift scripts/register.swift "$DEST/ZhuyinLLM.app"
