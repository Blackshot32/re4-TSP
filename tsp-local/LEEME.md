# Adaptación local a TrimUI Smart Pro

El checkout completo se trasladó de `/tmp/opencode/re4` a:

**`/home/golden/Proyectos/r4`**

Se conservaron `.git`, cambios locales, archivos sin seguimiento, builds y logs.
Tras el traslado se verificaron los contenidos de 10.726 archivos/enlaces mediante
SHA-256 o comparación del destino del enlace.

- Código Linux: `port/whole/host/front.cpp`.
- Renderer: `port/android-decomp/app/src/main/cpp/gx_gl.cpp`.
- Compilados: `build/whole-aarch64/`.
- Logs de diagnóstico: `build/glprof-results/`.
- Scripts de pruebas/resumen: `profile_tsp.py`, `summarize_diagnostics.py`.
- Informes y scripts auxiliares: este directorio.

Los informes copiados conservan rutas históricas. Sustituir mentalmente el prefijo
`/tmp/opencode/re4` por la nueva raíz. La ruta `/src` de los caches de CMake
corresponde al punto de montaje interno del contenedor, no al directorio del host.

Los discos originales, datos extraídos y archivos instalados en la consola no se
movieron. `profile_tsp.py` reinicia el juego: no ejecutarlo sin avisar al usuario.
