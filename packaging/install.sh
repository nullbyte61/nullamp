#!/usr/bin/env sh
# Install Nullamp into the current user's plugin directories.
set -eu

here="$(cd "$(dirname "$0")" && pwd)"

mkdir -p "$HOME/.lv2" "$HOME/.vst3" "$HOME/.clap"
rm -rf "$HOME/.lv2/Nullamp.lv2" "$HOME/.vst3/Nullamp.vst3" "$HOME/.clap/Nullamp.clap"

cp -r "$here/Nullamp.lv2"  "$HOME/.lv2/"
cp -r "$here/Nullamp.vst3" "$HOME/.vst3/"
cp    "$here/Nullamp.clap" "$HOME/.clap/"

echo "Installed Nullamp to ~/.lv2, ~/.vst3, and ~/.clap."
echo "Rescan plugins in your DAW to pick it up."
echo "The standalone JACK app is at: $here/Nullamp"
