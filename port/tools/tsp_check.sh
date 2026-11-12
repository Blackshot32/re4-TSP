#!/usr/bin/env bash
# Comprueba el motor ARM64 recien compilado: lo arranca con el test de runtime
# y mira que responde bien.
#
#   port/tools/tsp_check.sh [BUILD_DIR]
#
# El test necesita un emulador (qemu-aarch64) cuando la maquina no es ARM64.
# Las bibliotecas de la consola (SDL2, EGL, GLESv2) faltan en una maquina
# normal: el test se hace con versiones de mentira, y por eso solo comprueba
# el arranque, no el dibujo.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/build/whole-aarch64}"

if [ ! -x "$build/re4_host" ]; then
    echo "ERROR: no hay re4_host en $build" >&2
    exit 1
fi

arch="$(uname -m)"
runner=()
if [ "$arch" != "aarch64" ]; then
    if ! command -v qemu-aarch64 >/dev/null 2>&1; then
        echo "AVISO: no hay qemu-aarch64 (sudo apt-get install qemu-user" >&2
        echo "       o sudo xbps-install qemu-user); se omite el test." >&2
        exit 0
    fi
    sysroot=/usr/aarch64-linux-gnu
    [ -d "$sysroot" ] || sysroot=""
    runner=(qemu-aarch64)
    [ -n "$sysroot" ] && runner+=(-L "$sysroot")
fi

# Las libs de la consola en una maquina normal: el motor las busca por el
# nombre con version. Se crean enlaces y, si faltan los simbolos del driver
# (que en la consola estan en el sistema), se ponen de mentira.
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cp -a "$build/re4_host" "$build/libre4_main.so" "$build/libre4_host.so" \
      "$build/libre4_platform.so" "$build/libre4_detmath.so" "$work/" 2>/dev/null || true
cp -a "$build/modules" "$work/" 2>/dev/null || true
libs="$root/port/whole/third_party/tsp-libs"
if [ -d "$libs" ]; then
    cp -a "$libs/." "$work/"
    ( cd "$work" \
      && ln -sf libEGL.so libEGL.so.1 \
      && ln -sf libGLESv2.so libGLESv2.so.2 \
      && ln -sf libSDL2-2.0.so.0 libSDL2-2.0.so ) 2>/dev/null || true
fi

# Los simbolos que las libs de la consola piden (libsrv_um, libglslcompiler,
# libusc) solo existen en el dispositivo. Se generan de mentira para que el
# test pueda arrancar en un PC.
stubs="$work/stubs"
mkdir -p "$stubs"
if [ -d "$libs" ]; then
    {
        echo '/* stubs: en la consola estas librerias estan en el sistema */'
        for so in "$libs"/libIMGegl.so "$libs"/libGLESv2.so; do
            [ -f "$so" ] || continue
            nm -D --undefined-only "$so" 2>/dev/null | awk '{print $NF}' \
                | grep -v '@' | grep -v '^_'
        done | sort -u | while read -r sym; do
            case "$sym" in
                ""|[a-zA-Z]*) echo "void $sym(void) {}" ;;
            esac
        done
    } > "$stubs/stubs.c"
    for name in libsrv_um libglslcompiler libusc; do
        if [ -s "$stubs/stubs.c" ]; then
            if [ "$arch" = "aarch64" ]; then
                cc -shared -fPIC -o "$stubs/$name.so" "$stubs/stubs.c"
            else
                clang --target=aarch64-linux-gnu --sysroot=/usr/aarch64-linux-gnu \
                    --gcc-toolchain=/usr -shared -fPIC -o "$stubs/$name.so" \
                    "$stubs/stubs.c" 2>/dev/null || true
            fi
        fi
    done
fi

echo "Test de runtime del motor ..."
( cd "$work" && "${runner[@]}" \
    -E LD_LIBRARY_PATH="$work:$stubs" \
    ./re4_host --runtime-test ) | tail -6

echo
echo "El motor responde bien."
