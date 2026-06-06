#!/usr/bin/env sh
# Remove Nullamp from the current user's plugin directories.
set -eu

rm -rf "$HOME/.lv2/Nullamp.lv2" "$HOME/.vst3/Nullamp.vst3" "$HOME/.clap/Nullamp.clap"
echo "Removed Nullamp from ~/.lv2, ~/.vst3, and ~/.clap."
