#!/bin/bash
# Compila Boba (VST3 + AU) e instala en tu usuario. Uso:  chmod +x instalar.sh && ./instalar.sh
# Requisitos (una vez): xcode-select --install   y   brew install cmake
set -e
cd "$(dirname "$0")"
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
cmake --build build --config Release --target Boba_VST3 Boba_AU -j8
VST3=$(find build -type d -name Boba.vst3 | head -n1)
AU=$(find build -type d -name Boba.component | head -n1)
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
rm -rf ~/Library/Audio/Plug-Ins/VST3/Boba.vst3 ~/Library/Audio/Plug-Ins/Components/Boba.component
cp -R "$VST3" ~/Library/Audio/Plug-Ins/VST3/
cp -R "$AU"   ~/Library/Audio/Plug-Ins/Components/
codesign --force --deep --sign - ~/Library/Audio/Plug-Ins/VST3/Boba.vst3
codesign --force --deep --sign - ~/Library/Audio/Plug-Ins/Components/Boba.component
xattr -cr ~/Library/Audio/Plug-Ins/VST3/Boba.vst3 ~/Library/Audio/Plug-Ins/Components/Boba.component
echo "Listo. Reinicia tu DAW y vuelve a escanear plugins."
