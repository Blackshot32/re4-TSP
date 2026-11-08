# Prueba de concepto: RE4 nativo en TrimUI Smart Pro — 2026-11-07

## Resultado en una línea

**Sí sirve.** El código completo de RE4 (main.dol + los 114 REL) compila para ARM64 y
**funciona en la TSP**: los 114 módulos cargan, la interfaz del core pasa sus tests, y el
**renderizador GX dibuja en la PowerVR GE8300** con GLES 3.2. No hay port jugable todavía
porque falta el frontend Linux y los discos, pero la parte más difícil ya está resuelta.

---

## 1. Qué es CubeEvil4 y por qué cambia el plan

`Andiweli/CubeEvil4-Android` **no es solo la decompilación**: es el port nativo del
prototipo G4BE08 con toda la capa de plataforma reescrita.

| Pieza | Estado |
|---|---|
| Código del juego (`src/`, 114 REL) | completo, nativo ARM32/ARM64 |
| GX → GLES 3 | hecho (`gx_gl.cpp`, 6 438 líneas) |
| Audio AX/DSP + CRI ADX | hecho |
| Vídeo SFD/MPV (CRI) | hecho |
| DVD, CARD, ARAM, PAD, VI | hecho |
| Frontend | **solo Android** (JNI/EGL/SDL de la app) |
| Target Linux/TSP | **no existe** |

Los forks son en su mayoría espejos del mismo código; ninguno es un port Linux/TSP.
La rama `re4-standalone` es una variante Android anterior. La fuente buena es `master`.

Lo que en mi estimación anterior eran "6–12 meses" se reduce mucho: la decompilación **y**
la capa de plataforma ya existen. Lo que falta es acotado: un frontend Linux y los datos.

---

## 2. Lo que compilé y ejecuté (todo verificado)

### Build
- Toolchain: **clang 11 + lld**, sysroot aarch64 **glibc 2.31** (Debian 11), en Docker.
- Resultado: **1 395 objetivos, 0 errores**, `libre4_main.so` 3,8 MB + 114 módulos (16 MB).
- Compatibilidad con la consola: **GLIBC ≤ 2.30**, **GLIBCXX ≤ 3.4.22** (la TSP tiene 2.33 / 3.4.28).

### Tests en la propia TSP (`/mnt/SDCARD/re4-poc`)
| Test | Resultado |
|---|---|
| `--self-test` | trampolines entre módulos **OK** |
| `--check-modules` | **114 módulos, 0 fallos de carga, 0 imports sin resolver**, 57 stubs |
| `--runtime-test` | todo OK salvo **1** (ver §5) |
| `--core-test` | **0 fallos**: tarjeta de memoria, pausa, DoF, menú, letterbox |
| Memoria | pico **11,5 MB** (runtime) y **13 MB** (core) de RSS |

### El dato de rendimiento que importa
```
frame: 600 frames in r105: logic 2.0 ms, drawing 5.0 ms, rest 3.0 ms,
       frame end 1.0 ms, waiting 21.3 ms (longest frame 33.3 ms)
```
La CPU de la TSP hace el trabajo del juego en **~10 ms de los 33,3 ms** del frame de 30 Hz.
Quedan ~23 ms para la GPU. **El cuello no es la CPU.**

### Película + renderizador GX en la consola
- Película sintética (320×240, MPEG-1 + ADX) por el middleware CRI:
  **120 frames, 249 campos, 0 errores CRI**, decodificación ADX **1,10 ms** de media.
- `gx_chunk_render` en la TSP:
  `GPU: PowerVR Rogue GE8300, OpenGL ES 3.2 build 1.19@6345021`
  contexto **surfaceless** OK, shader compilado, frame 512×448 dibujado (2 triángulos, 2 texturas).
- Comparación con la referencia de ffmpeg: **PSNR 29,6 dB** (su criterio es 30 dB; la
  diferencia es un redondeo de 2 filas del quad de vídeo, no un fallo de color).

Artefactos en la consola: `/mnt/SDCARD/re4-poc/` (binarios, módulos, `demo/` con el PPM y los chunks).

---

## 2b. Con tus discos (G4BE08 Rev.00, dos discos)

Los `.gcm` de `/home/golden/Descargas/Resident Evil 4/` son **el build de depuración**
(contienen `Bio4.em06.sym`, el fichero de símbolos que solo trae el prototipo), no el retail.
Eso es exactamente lo que espera la decompilación: compatibilidad 100 %.

- Extracción con `whole_disc_folder.py`: **1 154 + 1 090 archivos, 0 problemas**.
- Arranque real en el PC: `DVD: disc 0 ... 1172 FST entries, debug build G4BE08`,
  `main loop reached at frame 501 in room r100`, **800 frames sin crash**.
- Frame 601 de r100: **324 draws, 140 201 vértices, 2 MB de texturas**;
  dibujado con el renderer: 512×448, **94 185 triángulos, 91 texturas, 40 shaders**.
- Pico de RAM del juego real: **64 MB** (la TSP tiene 1 GB).

Este es el pueblo de RE4 corriendo desde el código nativo, no desde Dolphin.

## 2c. Con tus discos, en la TSP (medido)

- Copiado `disc0` (1,3 GB, 1 154 archivos) a `/mnt/SDCARD/re4-data`.
- Arranque en la consola: `1172 FST entries, debug build G4BE08`,
  `main loop reached at frame 501 in room r100`, **600 frames sin crash**.
- **28 s** para ~800 frames con reloj real → **~28,5 fps de media** (CPU, headless).
- Capturas reales dibujadas por la PowerVR: el coche de la intro, la escena de los
  personajes y una pantalla de documento. Idénticas al render del PC:
  **PSNR 50,4 dB, 93 % de píxeles exactos**.

### Coste de GPU medido (con `glFinish`, escenas consecutivas)

| Escena | 1× (640×448) | 0.5× (320×224) |
|---|---|---|
| Ligera (150 draws, 22k tris) | ~56 ms (18 fps) | **~25 ms (34–42 fps)** |
| Pesada / intro (322 draws, 93k tris) | ~70–100 ms (10–14 fps) | **~55 ms (16–19 fps)** |

El primer frame de cada escena tarda **15 s**: compila 38 shaders (~400 ms cada uno).
La caché de programas es imprescindible; sin ella hay tirones de cientos de ms.

**Diagnóstico:** el tiempo escala con los **draw calls** (~0,17 ms/draw en PowerVR),
no con los píxeles ni los triángulos. 322 draws ≈ 55 ms a cualquier resolución.
Para 30 fps hay que bajar de ~190 draws por frame (o aceptar ~20 fps en las escenas
pesadas). `RE4_GX_MERGE=1` apenas cambió nada en esta escena.

**Parche probado:** permitir `halves=1` (0.5×, 320×224) en `InitHalves` y en
`gx_chunk_render`; se gana ~2,3× en escenas ligeras y ~1,3–1,8× en las pesadas.
La imagen a 320×224 escalada se ve blanda pero legible.

## 2d. Frontend jugable (SDL2 + GLES3) y límites reales

Se añadió `port/whole/host/front.cpp` al port: ventana SDL2 (driver `mali` de la
consola), contexto GLES3, audio ALSA a 32 kHz, mando por SDL, GX renderer de la app,
caché de shaders y arranque rápido al pueblo. Se lanza desde **Ports → Resident Evil 4**.

| Escena | Ajustes | FPS | GPU |
|---|---|---|---|
| Título / menús | 0.5× | **30–56** | baja |
| Pueblo r100 | 0.5× (320×264), potato 3 | **12–15** | **26–32 %** |
| Pueblo r100 | 1× (640×528), potato 3 | **10–13** | **46–60 %** |

**4 núcleos (3 hilos de vértices):** 11–15 fps, carga 5,3, **sin mejora**. El core baja
su tiempo de 19,5 a 17,5 ms, pero el renderer sube a 62–85 ms por contención de CPU.
El límite es el **envío de draws en un solo hilo** (~0,2 ms/draw), no la GPU.

**Merge de draws:** el escenario tiene ~37 % de draws que solo cambian uniforms
(`mergeUniform`), pero el merge existente solo aplica a ~8 % (`taken` 10 043 de ~347 000).
No basta para 30 fps.

**CPU:** 1,8 GHz sostenidos con los 4 núcleos (el OPP de 2,0 GHz no se alcanza; 77 °C).
**GPU:** sobra a 0.5×; a 1× sube al 55 % y el FPS baja poco, así que 1× es la opción
si se prefiere nitidez.

Conclusión: **30 fps estables en el pueblo no son alcanzables hoy** con este renderer en
la GE8300; hacen falta batching agresivo (una llamada por muchos draws), recortar más
efectos/objetos, o aceptar ~12–15 fps. El resto (audio, mando, guardado, shaders) funciona.

## 3. Los 30 fps: tu sospecha es correcta... pero el port no lo hace mal

El juego original **piensa a 30 Hz**: la lógica avanza 30 veces por segundo sobre una base
de campos NTSC de **59,94 Hz**. Los 60 fps del port **no** son lógica a 60: son un
**fotograma intermedio interpolado** entre dos imágenes de 30 Hz (`GxRenderer::Interpolate`).

Código que lo confirma:
- `port/docs/60fps-plan.md`: *"Das Spiel rechnet und zeichnet 30 Bilder/s. Die App baut
  zwischen zwei Spielbildern ein Zwischenbild … Anteil 0,5"*. Y descarta explícitamente
  "lógica a 60 Hz" porque obligaría a tocar cientos de sitios.
- `port/whole/runtime/vi.cpp`: los retrazos se entregan cada campo NTSC real
  (`kFieldNs = 1e9*1001/60000`), o se bloquean al vsync del panel.
- `OriginalFrameClock.java`: avanza los frames de 30 Hz **según tiempo real transcurrido**
  (con resto acumulado y catch-up), no según frames dibujados.
- `gx_auto_level.h` (`SmoothPacer`): si la GPU va justa, lo primero que se quita es el
  **fotograma intermedio** (5 s), no el reloj. El juego sigue a 30 Hz y a velocidad real.

**Conclusión:** si un port *reduce el reloj VI a la mitad* para "capar a 30", sí ralentiza
el juego a la mitad. Este port **no** lo hace: mantiene la base de 60 Hz y solo apaga la
interpolación. Por eso **30 fps aquí = velocidad completa**, y es justo lo que le conviene
a la TSP.

---

## 4. Optimizaciones concretas para la TSP

| Palanca | Qué hacer |
|---|---|
| Resolución | EFB 640×528 (nativa GC) y presentar a 640×480 escalado a 720p |
| 60 fps | **Apagarlo** (30 fps estables, menos GPU y menos calor) |
| MSAA | Off |
| AF | Off o 2× |
| Profundidad de campo / FXAA | Off |
| `RE4_GX_MERGE=1` | fusiona draws con las mismas texturas/estado (clave en PowerVR) |
| `RE4_GX_CHUNK_HOLD` | reutiliza la imagen anterior en vez de copiar de nuevo |
| Resolución dinámica | ya existe (`Rescale: Auto`); dejar que baje antes que perder frames |
| Caché de shaders | precompilar (el port ya tiene caché y prewarm) |
| Texturas | si el CMA de 64 MB aprieta, reducirlas a la mitad en el "cook" (a 640×480 no se nota) |
| Swap | evitar el swap en microSD (2 MB/s de escritura); no hace falta: el core usa ~13 MB |
| Térmica | perfil performance, tope 30 fps; a 4 núcleos al 100 % la TSP llega a 80 °C en 18 s |
| Logs | desactivar el menú de debug y los contadores en release |

---

## 5. El único fallo encontrado (y no es la consola)

`runtime-test math: FAILED`: `(u8)300.0f` devuelve **44** en vez de **255**.
Causa: **clang 11** no implementa el clamp al tipo destino con
`-fno-strict-float-cast-overflow`; clang 18+ sí. Lo comprobé en x86:

| Compilador | `(u8)300.0f` |
|---|---|
| clang 11 (nuestro contenedor) | **44** |
| clang 21 (PC) | **255** |

El proyecto usa Ubuntu 24.04 con clang 18, así que con su toolchain pasa. Para producir el
binario con glibc 2.31 hay que usar **clang 18+ con sysroot de Debian 11** (o parchear el
cast con un clamp explícito). No afecta al arranque ni al render; es una diferencia de
redondeo en conversiones float→entero de 8/16 bits.

---

## 6. Lo que falta para un port jugable en la TSP

| Tarea | Detalle | Estimación |
|---|---|---|
| **Frontend Linux** | ventana EGL/GLES (SDL2 o KMSDRM), swap, vsync, input SDL, audio ALSA desde `re4_audio_read`, bucle del core | **2–6 semanas** |
| **Datos de disco** | preparar carpeta `disc0/sys`+`files` en PC (`whole_disc_folder.py`) o leer ISO/RVZ con `nod` | 1–2 semanas |
| **Empaquetado** | launcher PortMaster, swap, perfil 30 fps, icono | días |
| **Ajuste fino** | merge de draws, resolución dinámica, caché de shaders, CMA | 2–4 semanas |
| **Total** | un dev con experiencia, a tiempo completo | **~1–3 meses** |

Comparado con empezar desde la decompilación (6–12+ meses), es otra liga: el 80 % está hecho.

**Bloqueo real:** no tienes discos de RE4 en la consola (busqué ISO/RVZ/GCM). Sin tus
propias imágenes (debug del 25-nov-2004, o retail US/EU/JP) no se puede pasar del título.
No se pueden descargar.

---

## 7. Cómo reproducir el build

```sh
# imagen (Debian 11 + clang/lld + cmake moderno)
docker build -t re4-tsp-builder -f Dockerfile.re4 .

docker run --rm -v $PWD/re4:/src -w /src --user $(id -u):$(id -g) re4-tsp-builder bash -lc '
  cmake -S port/whole -B build/whole-aarch64 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/src/port/whole/cmake/linux-aarch64.cmake \
    -DRE4_OPT_LEVEL=2 \
    -DCMAKE_C_FLAGS="-pthread" \
    -DCMAKE_CXX_FLAGS="-pthread -isystem /usr/aarch64-linux-gnu/include/c++/10/aarch64-linux-gnu" \
    -DCMAKE_EXE_LINKER_FLAGS="-pthread" -DCMAKE_SHARED_LINKER_FLAGS="-pthread"
  ninja -C build/whole-aarch64 -j 16'
```

En la consola:
```sh
cd /mnt/SDCARD/re4-poc
LD_LIBRARY_PATH=. ./re4_host --self-test
LD_LIBRARY_PATH=. ./re4_host --check-modules
LD_LIBRARY_PATH=. ./re4_host --runtime-test
LD_LIBRARY_PATH=. ./re4_host --core-test
```

---

## 8. Legal

El código reconstruido es de Capcom/Nintendo/CRI y se publica solo para investigación y
preservación. El port no incluye datos del juego: hacen falta tus propias copias. No
distribuyas ISOs ni el binario con datos.
