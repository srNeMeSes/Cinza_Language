#!/bin/sh
# Monta o pacote de distribuição (Q5) a partir do build já feito (make e make cinzac):
#
#   distribuicao/empacota.sh windows-x64   →  dist/cinza-<versão>-windows-x64.zip
#   distribuicao/empacota.sh linux-x64     →  dist/cinza-<versão>-linux-x64.tar.gz
#
# O pacote tem bin/ (o cinza, o cinzac e a pasta runtime/ que o cinzac usa para ligar), a licença,
# os avisos das licenças de terceiros (com os textos que o sistema de build tiver em licencas/), o
# README e a spec. Roda da raiz do repositório. Variáveis: LICENCAS (pastas com os textos das
# licenças de terceiros a copiar, separadas por espaço).
set -eu
plataforma=$1
versao=$(sed -n 's/^#define CINZA_VERSAO "\(.*\)"/\1/p' versao.h)
nome=cinza-$versao-$plataforma
pasta=dist/$nome
case $plataforma in
    windows-*) exe=.exe ;;
    *) exe= ;;
esac

rm -rf "$pasta"
mkdir -p "$pasta/bin" "$pasta/licencas"
cp "cinza$exe" "llvm/bin/cinzac$exe" "$pasta/bin/"
cp -r llvm/bin/runtime "$pasta/bin/runtime"
cp LICENSE README.md "$pasta/"
cp distribuicao/LICENCAS-TERCEIROS.md "$pasta/"
cp -r spec "$pasta/spec"
for d in ${LICENCAS:-}; do
    [ -d "$d" ] && cp -r "$d" "$pasta/licencas/$(basename "$d")"
done

# o pacote funciona sozinho: o cinzac dele compila e liga um programa, que roda
printf 'fn main() {\n  print("ação", [1, 2], {{"a", 1}});\n}\n' > dist/ola.cinza
"$pasta/bin/cinzac$exe" dist/ola.cinza -o "dist/ola$exe"
"./dist/ola$exe"
"$pasta/bin/cinza$exe" dist/ola.cinza
rm -f dist/ola.cinza "dist/ola$exe"

cd dist
case $plataforma in
    windows-*) rm -f "$nome.zip" && zip -qr "$nome.zip" "$nome" && echo "dist/$nome.zip" ;;
    *) tar czf "$nome.tar.gz" "$nome" && echo "dist/$nome.tar.gz" ;;
esac
