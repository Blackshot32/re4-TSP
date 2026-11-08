# re4-TSP — TrimUI Smart Pro (experimental)

[Español](#español) · [English](#english) · [Descargas / Downloads](https://github.com/Blackshot32/re4-TSP/releases)

## Español

Adaptación experimental de Resident Evil 4 para **TrimUI Smart Pro (Linux ARM64)**. Basada en la descompilación de [adonis-singh/re4](https://github.com/adonis-singh/re4) y en código y componentes del port [Andiweli/CubeEvil4-Android](https://github.com/Andiweli/CubeEvil4-Android).

Este fork reúne el frontend Linux, cambios del renderer GX/OpenGL ES, diagnóstico, profiling y scripts de pruebas para TSP. **No es el proyecto original ni una versión final: hay problemas gráficos y de rendimiento.** Los informes locales describen pruebas anteriores, no una garantía de funcionamiento para cualquier dispositivo.

### Descargar y probar

- La rama de este trabajo es [`tsp-port`](https://github.com/Blackshot32/re4-TSP/tree/tsp-port).
- La primera prerelease ofrece **código fuente en ZIP**, no un paquete listo para jugar. Requiere compilar y preparar dependencias y datos propios.
- **No se incluyen imágenes de disco ni datos extraídos del juego.** Tampoco se distribuyen las bibliotecas binarias específicas de la consola en `port/whole/third_party/tsp-libs/`.
- Los scripts en `tsp-local/` son herramientas experimentales con rutas de desarrollo, no un instalador. El launcher actual espera `/mnt/SDCARD/re4-poc/` y datos propios en `/mnt/SDCARD/re4-data/`.
- La receta de contenedor existente depende de una imagen local; todavía no es una guía de compilación autónoma y reproducible.

### Archivos útiles

- `port/whole/host/front.cpp`: frontend Linux/TSP.
- `port/android-decomp/app/src/main/cpp/gx_gl.cpp`: renderer compartido.
- `tsp-local/`: launchers e informes de desarrollo.
- `profile_tsp.py`, `summarize_diagnostics.py`: herramientas de diagnóstico. El profiler puede reiniciar el juego en la consola: revisar antes de ejecutarlo.

## English

Experimental Resident Evil 4 adaptation for **TrimUI Smart Pro (ARM64 Linux)**, based on [adonis-singh/re4](https://github.com/adonis-singh/re4), with code and components from [Andiweli/CubeEvil4-Android](https://github.com/Andiweli/CubeEvil4-Android).

This fork contains a Linux frontend, GX/OpenGL ES renderer changes, diagnostics, profiling and TSP test scripts. **This is not the original project or a finished release: graphics and performance issues remain.** Development reports describe earlier local tests, not guaranteed compatibility.

### Download and test

- Development branch: [`tsp-port`](https://github.com/Blackshot32/re4-TSP/tree/tsp-port).
- The initial prerelease is a **source-code ZIP**, not a ready-to-play package. Compilation, dependencies and your own game data are required.
- **No disc images or extracted game assets are included.** Device-specific binaries in `port/whole/third_party/tsp-libs/` are also excluded.
- `tsp-local/` scripts use development paths and are not an installer. The current launcher expects `/mnt/SDCARD/re4-poc/` and your own data at `/mnt/SDCARD/re4-data/`.
- The existing container recipe requires a local base image; a standalone reproducible build guide is not available yet.
- Review diagnostic scripts before running them: `profile_tsp.py` can restart the game on the device.

## Créditos / Credits and licensing

Original decompilation: **adonis-singh/re4**. Android port and shared components: **Andiweli/CubeEvil4-Android**. TSP fork: **Blackshot32/re4-TSP**.

See [LICENSE](LICENSE) for its limited scope: it does not license reconstructed game, SDK or middleware code. Third-party headers retain their own notices. This project is unofficial and is not endorsed by Capcom, Nintendo or CRI Middleware. Excluding game assets is not a guarantee of legal clearance for all included code or compiled binaries.

La documentación heredada de CubeEvil4 se conserva en [docs/README-CubeEvil4-upstream.md](docs/README-CubeEvil4-upstream.md); describe el port Android original, no el estado de esta adaptación TSP.

The inherited [CubeEvil4 README](docs/README-CubeEvil4-upstream.md) documents the upstream Android port, not this TSP adaptation.
