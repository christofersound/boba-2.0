#!/bin/bash
# Genera Boba-<version>.dmg (arrastrar y soltar) a partir de lo compilado en build/.
set -e
VERSION="${1:-1.0.0}"

VST3=$(find build -type d -name Boba.vst3 | head -n1)
AU=$(find build -type d -name Boba.component | head -n1)
if [ -z "$VST3" ] || [ -z "$AU" ]; then
  echo "No encuentro Boba.vst3 / Boba.component. Compila primero:"
  echo "  cmake -B build -DCMAKE_BUILD_TYPE=Release"
  echo "  cmake --build build --config Release --target Boba_VST3 Boba_AU -j8"
  exit 1
fi

rm -rf dist && mkdir -p dist/dmg
cp -R "$VST3" dist/dmg/
cp -R "$AU"   dist/dmg/

# Firma ad-hoc (gratis; necesaria para que cargue en Apple Silicon)
codesign --force --deep --sign - dist/dmg/Boba.vst3
codesign --force --deep --sign - dist/dmg/Boba.component

# Atajos a las carpetas de destino (donde se arrastra cada plugin)
ln -s /Library/Audio/Plug-Ins/VST3       dist/dmg/VST3
ln -s /Library/Audio/Plug-Ins/Components dist/dmg/Components

rm -f "Boba-$VERSION.dmg"
if command -v create-dmg >/dev/null 2>&1 && [ -z "$CI" ]; then
  # Ventana con los iconos acomodados (requiere sesion grafica de macOS)
  create-dmg \
    --volname "Boba $VERSION" \
    --window-size 640 400 \
    --icon-size 96 \
    --icon "Boba.vst3"      160 100 --icon "VST3"       480 100 \
    --icon "Boba.component" 160 270 --icon "Components" 480 270 \
    --no-internet-enable \
    "Boba-$VERSION.dmg" dist/dmg
else
  # Modo simple (el que usa GitHub Actions): mismo contenido, sin acomodar la ventana
  hdiutil create -volname "Boba $VERSION" -srcfolder dist/dmg -ov -format UDZO "Boba-$VERSION.dmg"
fi

echo "Listo: Boba-$VERSION.dmg"
