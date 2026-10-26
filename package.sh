#!/usr/bin/env sh
# Builds the plugin and the save editor, then writes dist/Victoria2-Console-Commands.zip
set -e
cd "$(dirname "$0")"
./plugin/build.sh
python3 save-editor/build.py
rm -rf dist && mkdir -p dist/pkg/mod/TGCConvertPop dist/pkg/interface dist/pkg/localisation dist/pkg/SaveEditor
cp mod/TGCConvertPop.ini mod/TGCConvertPop.mod dist/pkg/mod/
cp plugin/TGCConvertPop.dll mod/TGCConvertPop/README.txt dist/pkg/mod/TGCConvertPop/
cp interface/zz_v2commands.gui interface/zz_v2commands.gfx dist/pkg/interface/
cp localisation/zz_v2commands.csv dist/pkg/localisation/
mkdir -p dist/pkg/mod/TGCConvertPop/treaties dist/pkg/mod/TGCTreaties/treaties
cp mod/TGCConvertPop/treaties/base.txt dist/pkg/mod/TGCConvertPop/treaties/
cp mod/TGCTreaties.mod dist/pkg/mod/ && cp -r mod/TGCTreaties/* dist/pkg/mod/TGCTreaties/
# window textures are made from your own game files: python tools/make_textures.py "<game>/gfx/interface" "<game>/gfx/interface"
cp "save-editor/Victoria2 Save Editor.html" dist/pkg/SaveEditor/
cp LICENSE docs/INSTALL.md dist/pkg/
(cd dist/pkg && zip -qr ../Victoria2-Console-Commands.zip .)
echo "dist/Victoria2-Console-Commands.zip"
