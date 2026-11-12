#!/usr/bin/env bash
# Arma el paquete de release: el motor compilado (re4-poc) + el script de
# setup que extrae los discos y deja la SD lista.
#
#   port/tools/tsp_package.sh [BUILD_DIR] [OUT_DIR]
#
# BUILD_DIR: la carpeta del build ARM64 (por defecto build/whole-aarch64)
# OUT_DIR:   donde se escribe el ZIP (por defecto dist/)
#
# El ZIP lleva:
#   re4-poc/     el motor: re4_host, las bibliotecas y los 117 modulos
#   setup.py     el script que extrae los .gcm del jugador y arma la SD
#   tools/       el extractor de los discos (whole_disc_folder.py y compañía)
#   LEEME.txt    las instrucciones, en español y en ingles
#
# Los enlaces simbolicos (libEGL.so.1 -> libEGL.so) se guardan como enlaces:
# sin -y, el zip guardaria una copia de cada uno y el paquete ocuparia el
# doble. Un zip con enlaces simbolicos lo descomprime bien unzip, 7-Zip y
# Windows 11 ( Explorador de archivos), que es donde lo abrirá el jugador.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/build/whole-aarch64}"
out="${2:-$root/dist}"
stage="$out/re4-tsp-beta"

if [ ! -x "$build/re4_host" ]; then
    echo "ERROR: no hay un binario re4_host en $build" >&2
    echo "       compila primero el port (port/tools/whole_build.sh aarch64)" >&2
    exit 1
fi

echo "Empaquetando el port (TSP beta) ..."
rm -rf "$stage"
mkdir -p "$stage/re4-poc/modules" "$stage/tools"

# --- el motor ---------------------------------------------------------------
cp -a "$build/re4_host" "$stage/re4-poc/"
for lib in libre4_main.so libre4_host.so libre4_platform.so libre4_detmath.so; do
    [ -f "$build/$lib" ] && cp -a "$build/$lib" "$stage/re4-poc/"
done
cp -a "$build/modules/." "$stage/re4-poc/modules/"

# Las bibliotecas de la consola (SDL2, EGL, GLESv2): las necesita el motor en
# la TrimUI. Sin ellas no arranca.
if [ -d "$root/port/whole/third_party/tsp-libs" ]; then
    cp -a "$root/port/whole/third_party/tsp-libs/." "$stage/re4-poc/"
    # El enlazador las busca por su nombre con version (libEGL.so.1 etc.).
    ( cd "$stage/re4-poc" \
      && ln -sf libEGL.so libEGL.so.1 \
      && ln -sf libGLESv2.so libGLESv2.so.2 \
      && ln -sf libSDL2-2.0.so.0 libSDL2-2.0.so ) 2>/dev/null || true
fi

# El launcher que usa el menu de la consola.
for l in tsp-local/play.sh tsp-local/re4-launcher.sh; do
    [ -f "$root/$l" ] && cp "$root/$l" "$stage/re4-poc/"
done
# El motor y los launchers tienen que quedar ejecutables en la SD (el
# descompresor puede quitar el bit; setup.py lo restituye, pero mejor que
# viaje bien desde el principio).
chmod +x "$stage/re4-poc/re4_host" "$stage/re4-poc/"*.sh 2>/dev/null || true

# --- el script de setup -----------------------------------------------------
cp "$root/tsp-local/setup.py" "$stage/setup.py"
cp "$root/tsp-local/LEEME.txt" "$stage/LEEME.txt"

# El extractor de los discos: solo la biblioteca estandar de Python.
cp "$root/port/tools/whole_disc_folder.py" "$stage/tools/"
cp "$root/port/tools/disc_release.py" "$stage/tools/"
mkdir -p "$stage/tools/original_import"
cp "$root/port/tools/original_import/__init__.py" "$stage/tools/original_import/" 2>/dev/null || :
cp "$root/port/tools/original_import/gcm.py" "$stage/tools/original_import/"
for extra in "$root/port/tools/original_import/"*.py; do
    case "$(basename "$extra")" in
        __init__.py|gcm.py) continue ;;
    esac
    cp "$extra" "$stage/tools/original_import/"
done

# --- el ZIP -----------------------------------------------------------------
mkdir -p "$out"
zip_name="$out/re4-tsp-beta.zip"
rm -f "$zip_name"
( cd "$out" && zip -qry "$(basename "$zip_name")" "$(basename "$stage")" )

size=$(du -h "$zip_name" | cut -f1)
echo "  $zip_name ($size)"
echo
echo "El paquete lleva:"
echo "  re4-poc/   el motor (re4_host + 117 modulos + las libs de la consola)"
echo "  setup.py   el script que extrae los .gcm y arma la SD"
echo "  tools/     el extractor de los discos"
