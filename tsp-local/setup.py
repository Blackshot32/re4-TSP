#!/usr/bin/env python3
"""Prepara el port de RE4 para la TrimUI Smart Pro a partir de las imagenes
de los dos discos del GameCube.

La persona que juega solo necesita sus propias imagenes (.gcm/.iso). Este
script:

  1. busca las imagenes en la carpeta que se le indique (o en la carpeta
     donde esta el script),
  2. las extrae a la carpeta de datos que el motor lee en la consola
     (re4-data/disc0 y re4-data/disc1), con el extractor del propio port
     (tools/whole_disc_folder.py), que valida la release del disco,
  3. copia el motor ya compilado (re4-poc) al destino,
  4. deja la SD lista para copiar: re4-poc (motor) y re4-data (assets).

El motor (re4-poc) viene compilado en la release; este script no compila
nada, solo extrae los assets y arma la SD.

Uso:
    python setup.py [--isos CARPETA] [--out CARPETA] [--force]

Sin argumentos busca las imagenes en la carpeta actual y arma la SD en
./SD. La cache (carpeta .cache) evita volver a extraer los mismos discos:
se guardan las sumas SHA-256 de las imagenes y se reutiliza la extraccion
anterior si las imagenes no cambiaron.

Solo usa la biblioteca estandar de Python: funciona igual en Windows, Linux
y macOS, sin instalar nada mas.
"""

import argparse
import hashlib
import json
import shutil
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
# El extractor del port vive en tools/ dentro de la release.
sys.path.insert(0, str(HERE / "tools"))


def human(size):
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if size < 1024:
            return f"{size:.1f} {unit}"
        size /= 1024
    return f"{size:.1f} PB"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(8 << 20)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def copy_tree(src, dst):
    """Copia un arbol de archivos. Devuelve (archivos, bytes)."""
    copied = 0
    total = 0
    for item in sorted(src.rglob("*")):
        rel = item.relative_to(src)
        target = dst / rel
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item, target)
        copied += 1
        total += item.stat().st_size
    return copied, total


def make_executable(folder):
    """Deja ejecutables los ficheros que lo son en el paquete.

    El motor (re4_host) y los launchers necesitan el bit de ejecucion. Un
    descompresor puede quitarselo (el Explorador de archivos de Windows y el
    unzip de Python lo hacen), y entonces la consola no arranca el juego.
    """
    fixed = []
    for pattern in ("re4_host", "*.sh"):
        for item in folder.glob(pattern):
            if not item.is_file():
                continue
            mode = item.stat().st_mode
            if not mode & 0o111:
                item.chmod(mode | 0o111)
                fixed.append(item.name)
    return fixed


# El enlazador busca las bibliotecas de la consola por su nombre con version
# (libEGL.so.1, libGLESv2.so.2, libSDL2-2.0.so). Los enlaces los hace el
# empaquetador; un descompresor que no los restaura (unzip de Linux, o el
# Explorador de archivos de Windows antes de la 11) los deja como copias, y
# entonces falta el nombre que el enlazador busca. Se comprueban y se rehacen.
CONSOLE_LIBS = (
    ("libEGL.so", "libEGL.so.1"),
    ("libGLESv2.so", "libGLESv2.so.2"),
    ("libSDL2-2.0.so.0", "libSDL2-2.0.so"),
)


def fix_console_libs(engine_dir):
    """Recrea los enlaces de las libs de la consola si faltan o son copias.

    Un descompresor que no restaura los enlaces simbolicos (unzip de Linux, o
    el Explorador de archivos de Windows antes de la 11) deja el archivo
    real con el nombre con version, y entonces el motor busca un nombre que
    no existe. Se comprueba si el destino es un enlace; si no lo es, se
    rehace.
    """
    fixed = []
    for real, link in CONSOLE_LIBS:
        target = engine_dir / link
        source = engine_dir / real
        if not source.is_file():
            continue
        if target.is_symlink():
            continue
        try:
            if target.exists():
                target.unlink()
            target.symlink_to(real)
        except (OSError, NotImplementedError):
            # Sin symlinks (Windows sin permisos): se copia el archivo.
            if not target.exists():
                shutil.copy2(source, target)
        fixed.append(link)
    return fixed


def find_images(folder):
    """Las imagenes de disco de la carpeta, sin repetir archivos."""
    found = {}
    for pattern in ("*.gcm", "*.iso", "*.GCM", "*.ISO", "*.GCM*", "*.ISO*"):
        for path in sorted(folder.glob(pattern)):
            found[str(path).lower()] = path
    return sorted(found.values(), key=lambda p: p.name.lower())


def main():
    ap = argparse.ArgumentParser(
        description="Prepara el port de RE4 para la TrimUI Smart Pro "
                    "a partir de las imagenes de los dos discos.")
    ap.add_argument("--isos", type=Path, default=None,
                    help="carpeta con las imagenes de los discos "
                         "(por defecto, la carpeta actual)")
    ap.add_argument("--out", type=Path, default=None,
                    help="carpeta donde armar la SD (por defecto, ./SD)")
    ap.add_argument("--force", action="store_true",
                    help="vuelve a extraer aunque la cache diga lo contrario")
    args = ap.parse_args()

    isos_dir = (args.isos or Path.cwd()).resolve()
    out_dir = (args.out or (HERE / "SD")).resolve()
    cache_dir = HERE / ".cache"

    print("RE4 TSP - preparacion del port para la TrimUI Smart Pro")
    print()

    # --- 1. Localizar las imagenes -----------------------------------------
    if not isos_dir.is_dir():
        print(f"ERROR: no existe la carpeta {isos_dir}")
        return 1

    images = find_images(isos_dir)
    if not images:
        print(f"ERROR: no hay imagenes de disco (.gcm o .iso) en {isos_dir}")
        print("       copia los dos discos del GameCube a esa carpeta y "
              "vuelve a ejecutar este script.")
        return 1

    print(f"Imagenes encontradas en {isos_dir}:")
    for img in images:
        print(f"  {img.name}  ({human(img.stat().st_size)})")
    print()

    if len(images) < 2:
        print(f"AVISO: se esperan dos discos (el 1 y el 2); hay "
              f"{len(images)} imagen(es).")
        print()

    # --- 2. Cache por suma -------------------------------------------------
    cache_dir.mkdir(parents=True, exist_ok=True)
    cache_file = cache_dir / "extracted.json"
    state = {}
    if cache_file.is_file() and not args.force:
        try:
            state = json.loads(cache_file.read_text())
        except (ValueError, OSError):
            state = {}

    print("Comprobando las imagenes (SHA-256) ...")
    digests = {str(p): sha256(p) for p in images}

    data_dir = out_dir / "re4-data"
    engine_dir = out_dir / "re4-poc"
    # La cache vale para los datos; el motor se copia siempre (es pequeno y
    # puede haber cambiado en una version nueva del paquete).
    cached = (state.get("digests") == digests and data_dir.is_dir())

    if cached:
        print("Los discos no han cambiado: se reutiliza la extraccion "
              "anterior (cache).")
    else:
        # --- 3. Extraer con el extractor del port --------------------------
        try:
            from whole_disc_folder import from_images, check
        except ImportError as exc:
            print(f"ERROR: falta el extractor del port ({exc}).")
            print("       la release debe incluir la carpeta tools/.")
            return 1

        if data_dir.exists():
            shutil.rmtree(data_dir)
        data_dir.mkdir(parents=True, exist_ok=True)

        print()
        print("Extrayendo los discos (esto tarda unos minutos) ...")
        started = time.time()
        try:
            from_images([str(p) for p in images], data_dir)
        except SystemExit as exc:
            print(f"ERROR: la extraccion fallo: {exc}")
            print("       comprueba que las imagenes sean las del juego "
                  "original de GameCube (G4BE08, G4BP08 o G4BJ08) y que "
                  "no esten corruptas.")
            return 1
        except Exception as exc:  # noqa: BLE001 - mensaje al usuario
            print(f"ERROR: la extraccion fallo: {exc}")
            return 1
        print(f"  listo en {time.time() - started:.1f} s")

        # Comprobacion: todos los archivos del FST presentes con su tamano.
        problems = check(data_dir)
        if problems:
            print("ERROR: la extraccion no esta completa (mira el mensaje "
                  "de arriba).")
            return 1

        cache_file.write_text(json.dumps({"digests": digests}, indent=1))

    # --- 4. Copiar el motor ------------------------------------------------
    engine = HERE / "re4-poc"
    if not engine.is_dir():
        print()
        print(f"ERROR: falta el motor compilado ({engine}).")
        print("       descarga la release completa y vuelve a extraerla aqui.")
        return 1

    if engine.resolve() != engine_dir.resolve():
        if engine_dir.exists():
            shutil.rmtree(engine_dir)
        print()
        print("Copiando el motor (re4-poc) ...")
        copied, size = copy_tree(engine, engine_dir)
        print(f"  {copied} archivos ({human(size)})")

    # Los enlaces de las libs de la consola (el descompresor puede no
    # restaurarlos).
    fixed = fix_console_libs(engine_dir)
    if fixed:
        print(f"  enlaces de las libs de la consola restaurados: "
              f"{', '.join(fixed)}")

    # El bit de ejecucion del motor y de los launchers (el descompresor
    # puede quitarselo). Se hace siempre: aunque la cache acierte, el motor
    # puede haber sido copiado antes de este arreglo.
    made = make_executable(engine_dir)
    if made:
        print(f"  permiso de ejecucion restituido: {', '.join(made)}")

    # --- 5. Resumen -------------------------------------------------------
    total_size = sum(p.stat().st_size for p in out_dir.rglob("*") if p.is_file())
    print()
    print("Listo. La carpeta de la SD es:")
    print(f"  {out_dir}")
    print()
    print("Copia su contenido a la raiz de la tarjeta SD de la TrimUI:")
    print("  /mnt/SDCARD/re4-poc    (el motor)")
    print("  /mnt/SDCARD/re4-data   (los assets de los discos)")
    print()
    print(f"Tamano total: {human(total_size)}")
    print()
    print("En la consola, lanza el juego desde el menu de la TrimUI.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
