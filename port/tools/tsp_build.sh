#!/usr/bin/env bash
# Compila el port para la TrimUI Smart Pro (ARM64 Linux) en esta maquina,
# sin Docker y sin WSL.
#
#   port/tools/tsp_build.sh [BUILD_DIR]
#
# BUILD_DIR: donde queda el build (por defecto build/whole-aarch64)
#
# Que hace:
#   1. comprueba (e instala) el toolchain: clang, lld, cmake, ninja, python3
#      y el cross-toolchain aarch64-linux-gnu (sysroot con las cabeceras y
#      las bibliotecas del objetivo);
#   2. compila el port entero (el motor: re4_host y los 117 modulos);
#   3. comprueba el binario con el test de runtime.
#
# Solo para ARM64: el frontend enlaza las bibliotecas de la consola (SDL2,
# EGL, GLESv2), que son binarios aarch64.
#
# El toolchain se busca donde lo pone cada distribucion:
#   Debian, Ubuntu, Void   apt / xbps
#   Fedora, RHEL            dnf
#   Arch                    pacman
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/build/whole-aarch64}"
jobs="${RE4_JOBS:-$(nproc 2>/dev/null || echo 4)}"

have() { command -v "$1" >/dev/null 2>&1; }

# --- 1. el toolchain ---------------------------------------------------------
install_deps() {
    local missing=()
    for tool in clang lld cmake ninja python3; do
        have "$tool" || missing+=("$tool")
    done
    [ ${#missing[@]} -eq 0 ] && return 0

    echo "Faltan herramientas: ${missing[*]}"
    if have apt-get; then
        echo "Instalando con apt-get ..."
        sudo apt-get update -qq
        sudo apt-get install -y -qq clang lld cmake ninja-build python3 \
            g++-aarch64-linux-gnu gcc-aarch64-linux-gnu
    elif have xbps-install; then
        echo "Instalando con xbps-install ..."
        sudo xbps-install -y clang lld cmake ninja python3 \
            cross-aarch64-linux-gnu cross-aarch64-linux-gnu-libc
    elif have dnf; then
        echo "Instalando con dnf ..."
        sudo dnf install -y clang lld cmake ninja-build python3 \
            gcc-aarch64-linux-gnu glibc-aarch64-linux-gnu-devel
    elif have pacman; then
        echo "Instalando con pacman ..."
        sudo pacman -S --needed --noconfirm clang lld cmake ninja python \
            aarch64-linux-gnu-gcc
    else
        echo "ERROR: instala a mano: ${missing[*]}" >&2
        echo "       y el cross-toolchain aarch64-linux-gnu (sysroot)." >&2
        exit 1
    fi
}

install_deps

# El sysroot del objetivo: las cabeceras y las bibliotecas de aarch64.
sysroot=""
for candidate in /usr/aarch64-linux-gnu /usr/local/aarch64-linux-gnu; do
    if [ -d "$candidate/include" ]; then
        sysroot="$candidate"
        break
    fi
done
if [ -z "$sysroot" ]; then
    echo "ERROR: falta el cross-toolchain aarch64-linux-gnu." >&2
    echo "       en Debian/Ubuntu: sudo apt-get install g++-aarch64-linux-gnu" >&2
    echo "       en Void:          sudo xbps-install cross-aarch64-linux-gnu cross-aarch64-linux-gnu-libc" >&2
    exit 1
fi
echo "Sysroot del objetivo: $sysroot"

# --- 2. compilar -------------------------------------------------------------
echo "Compilando el port (ARM64) en $build ..."
cmake -S "$root/port/whole" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/port/whole/cmake/linux-aarch64.cmake" \
    -DRE4_OPT_LEVEL=3 \
    -DRE4_AARCH64_SYSROOT="$sysroot"
cmake --build "$build" --parallel "$jobs"

# --- 3. comprobar -----------------------------------------------------------
echo
echo "Comprobando el motor ..."
"$root/port/tools/tsp_check.sh" "$build"

echo
echo "Listo. Para armar el paquete de release:"
echo "  port/tools/tsp_package.sh $build"
