# Resident Evil 4 nativo en TrimUI Smart Pro: contexto para otro agente

Fecha: 7 de noviembre de 2026. Documento de traspaso, no una promesa de rendimiento.

## 1. Objetivo y reglas de trabajo

Hacer jugable RE4 de GameCube en una **TrimUI Smart Pro original**, ejecutando código
nativo ARM64, y estudiar si puede acercarse a **30 fps reales**.

- Trabajar incrementalmente: una modificación, medir, comparar.
- No implementar ordenación de draws (`RE4_GX_SORT`) todavía.
- No cambiar el render por defecto durante los experimentos.
- Nuevas pruebas mediante variables de entorno, desactivadas por defecto.
- No distribuir discos, archivos extraídos ni capturas de comandos con datos del juego.
- **Avisar antes de pausar, congelar, reiniciar o dejar sin imagen/control la consola.**
  El replay de diagnóstico pausó intencionadamente el juego y el usuario lo interpretó
  como un bloqueo. Se detuvo y se restauró el arranque normal.
- No confundir resultados headless, replay aislado y juego interactivo.

## 2. De qué proyecto se parte

Repositorio configurado como origen en el checkout local:
https://github.com/Andiweli/CubeEvil4-Android.git

Checkout: `/tmp/opencode/re4`, rama configurada `master`.

No partimos únicamente de una decompilación que hubiera que portar desde cero.
El proyecto base ya contiene el código del juego y una extensa capa de plataforma
para ejecutarlo nativamente, originalmente integrada en una aplicación Android.

La versión utilizada es el **build debug de GameCube G4BE08 Rev.00**. El usuario
dispone de sus dos discos `.gcm` en:
`/home/golden/Descargas/Resident Evil 4/`.

No asumir compatibilidad con cualquier ISO retail: las pruebas de esta adaptación
se hicieron con esos discos concretos.

### Arquitectura, simplificada

```text
Código del juego compilado para ARM64 + 114 módulos REL convertidos a bibliotecas
    |
    +-- runtime: memoria, DVD, ARAM, CARD, PAD, VI, audio AX/DSP/CRI...
    |
    +-- captura/decodificación de comandos GX y trabajo de vértices
            |
            +-- Re4GxChunk: operaciones, vértices, índices, datos de texturas
                    |
                    +-- GxRenderer::Run en el hilo de render
                            - interpreta estado GX/BP/XF/TEV
                            - selecciona/genera shaders GLES
                            - actualiza UBO, texturas y buffers
                            - emite geometría, copias EFB y pasadas auxiliares
                            |
                            +-- Present + SDL_GL_SwapWindow -> pantalla
```

No es Dolphin: no se está emulando una CPU PowerPC instrucción por instrucción.
Sí se reproduce el comportamiento de APIs y hardware de GameCube, particularmente
GX, mediante un runtime y un traductor a GLES. Eso todavía puede ser costoso.

El frontend Linux usa el core en el mismo proceso, con hilo de juego y trabajo
auxiliar. No se utiliza aquí el modo IPC Android de core en otro proceso.

`re4_host` es un cargador que preserva el rango de memoria baja necesario y carga
`libre4_host.so`. Ese último contiene el frontend Linux y el renderer.

## 3. Hardware y entorno real

| Elemento | Información comprobada durante el trabajo |
|---|---|
| CPU | Cuatro Cortex-A53; 1,8 GHz efectivos observados |
| GPU | **PowerVR Rogue GE8300**, TBDR; no Mali |
| API | OpenGL ES 3.2, driver identificado como `1.19@6345021` |
| RAM | Aproximadamente 1 GB; CMA configurada de 64 MB |
| Pantalla | 1280 × 720 |
| Sistema consola | Linux aarch64, kernel 4.9.191 |
| Perfilado disponible | No se encontró `perf`, `gprof` ni `perf_event` utilizable |
| Temperaturas | Aproximadamente 77–82 °C durante distintas pruebas largas |

**Trampa importante:** SDL usa un driver de vídeo llamado `mali`, pero la GPU es
PowerVR. Es el nombre del backend SDL de la consola, no la identidad del hardware.
El intento con `kmsdrm` falló: ese SDL no trae dicho backend disponible.

Se observaron escrituras a 2 GHz que no producían esa frecuencia. El perfil local
de la consola fija 1,8 GHz y documenta la limitación del A133P. No contar con una
subida a 2 GHz como mejora disponible. La frecuencia máxima listada no prueba la real.

PC de trabajo: Void Linux. No usar gestores de paquetes de Debian en el host.
El **contenedor** de compilación sí es Debian 11; son entornos distintos.

## 4. Adaptaciones realizadas en esta sesión

Estas son aportaciones locales; no atribuirlas al proyecto original ni viceversa.

1. Compilación aarch64 del juego/runtime/módulos en el contenedor
   `re4-tsp-builder`, con clang 11, lld y entorno glibc 2.31.
2. Carga de los 114 módulos y pruebas del runtime/core en la consola.
   `--core-test` pasó sin fallos. Hubo un fallo conocido de conversión float->entero
   en `--runtime-test`, relacionado con clang 11; no decir que todos los tests pasaron.
3. Extracción de los discos propios y preparación de los archivos del juego.
   El disco 1 está instalado como `disc0` en la SD; la copia del segundo no quedó
   confirmada en este trabajo.
4. Creación de `port/whole/host/front.cpp`, frontend Linux jugable:
   SDL2, contexto GLES3, input de mando, audio ALSA desde el core,
   presentación, caché de shaders, uso del frame-skip existente y logs de tiempos.
5. Integración de `--play` en `port/whole/host/main.cpp`.
6. Integración del frontend y renderer en `port/whole/CMakeLists.txt`.
   Headers y bibliotecas SDL2/EGL/GLES de la TSP en `port/whole/third_party/`.
7. Cambio del mínimo de `halves` en `gx_gl.cpp`: de 2 a 1, para permitir 0,5×.
8. Scripts de arranque y dos entradas en Ports: arranque rápido a r100 y arranque
   normal por título/menús. El arranque rápido utiliza controles programados del core.
9. Configuración del guardado persistente, shader cache y controles.
10. Instrumentación temporal GL y pruebas NODRAW/pasadas descritas más abajo.

No se ha completado una campaña de estabilidad del juego entero. Arrancar y jugar
en una sala no equivale a verificar toda la historia, todos los efectos y ambos discos.

### Archivos y ubicaciones importantes

| Ruta | Función |
|---|---|
| `/tmp/opencode/re4/port/whole/host/front.cpp` | Frontend Linux local |
| `/tmp/opencode/re4/port/whole/host/main.cpp` | Entrada `--play` |
| `/tmp/opencode/re4/port/whole/host/loader.c` | Cargador de memoria baja |
| `/tmp/opencode/re4/port/whole/runtime/gx_decode.cpp` | Decodificación GX/trabajo de vértices |
| `/tmp/opencode/re4/port/android-decomp/app/src/main/cpp/gx_gl.cpp` | Renderer |
| Mismo directorio: `gx_gl_shader.cpp`, `gx_texture.cpp` | Shaders y decode de texturas |
| Mismo directorio: `gx_gl_prof.h`, `gx_gl_prof_macros.h` | Instrumentación local nueva |
| `/tmp/opencode/re4/build/whole-aarch64/` | Binarios aarch64 compilados |
| `/tmp/opencode/re4/build/glprof-results/` | Logs de los experimentos recientes |
| `/tmp/opencode/re4/profile_tsp.py` | Automatización de pruebas; **pausa/reinicia la consola** |
| `/mnt/SDCARD/re4-poc/` en la TSP | Binarios instalados, `play.sh`, shaders y logs |
| `/mnt/SDCARD/re4-data/disc0/` en la TSP | Datos propios extraídos |
| `/mnt/SDCARD/re4-data/memcard/card-a.bin` en la TSP | Guardado: protegerlo |

El checkout y los logs están en `/tmp/opencode`: preservarlos antes de limpiar
temporales. Hay modificaciones locales y archivos sin seguimiento; no hacer reset,
checkout destructivo ni limpieza indiscriminada.

## 5. Qué existía ya y qué optimizaciones se probaron

### Mecanismos heredados del proyecto base

- Caché de programas por `ShaderKey`, binarios en disco y prewarm por sala.
- Shaders especializados según estado GX/TEV, no un único uber-shader universal.
- Merge de rachas compatibles: prepara índices concatenados y usa bloques de
  uniforms para varias piezas. No es simplemente un `glMultiDraw`.
- Caché de texturas y samplers.
- Comparación de estado GX, programa actual, texturas por unidad y atributos.
- Comparación de bloques de uniforms para evitar algunas subidas idénticas.
- Stream de UBO en anillo de cuatro partes, con fences; mapeo persistente si está
  disponible o mapas por bloque no sincronizados como alternativa.
- Opción de vertex ring persistente, además de la ruta de uploads por chunk.
- Modo potato y opciones de efectos, interpolación y calidad.

Que existan estos mecanismos no significa que todas sus rutas estén activadas en
la TSP ni que toda llamada GL redundante esté eliminada.

### Experimentos/configuración realizados

| Prueba | Resultado observado y límites |
|---|---|
| Render 0,5× | EFB 320×264; mejora respecto a 1×, imagen más borrosa |
| Render 1× | EFB 640×528; GPU más ocupada y algo menos de FPS |
| Potato 3 | Activado en las comparaciones; omite categorías de efectos previstas por el core |
| MSAA/AF/FXAA | Frontend con samples=1, anisotropía=1, FXAA apagado |
| Interpolación a 60 | No usada en este frontend; objetivo de lógica/juego a 30 |
| Más workers GX | Se probaron 1, 2 y 3 auxiliares; no hubo mejora global convincente con 3 |
| Merge activado | Mejora variable por rachas; su eficacia depende de la escena y de shaders preparados |
| Performance CPU | Se solicitó y se comprobaron frecuencias; controlar también temperatura |
| Shader cache | Evita repetir gran parte del coste de compilación inicial; no elimina todos los tirones |

No se ha implementado sort, atlas, uber-shader nuevo, PGO ni una campaña medida
de `-O3 -mcpu=cortex-a53 -flto`. El build inicial se configuró con `RE4_OPT_LEVEL=2`.
No afirmar mejoras de compilación que no se hayan construido y comparado.

## 6. Rendimiento interactivo de referencia

Condiciones habituales: r100, potato=3, merge=1, un auxiliar de vértices,
shader cache caliente. Son rangos de distintas ventanas, no un benchmark fijo.

| Caso | FPS de presentación observados | Uso GPU observado |
|---|---:|---:|
| Título/menús/vídeos de título | 30–56 | Variable |
| r100 a 0,5× | Habitualmente 12–15, con ventanas más bajas | Aproximadamente 22–32 % |
| r100 a 1× | Aproximadamente 10–13 | Aproximadamente 46–60 % |
| r100, tres auxiliares GX | Aproximadamente 11–15; sin mejora clara | Sigue sin saturarse sostenidamente |

Se observaron 67–95 ms en el tramo del frontend que llama al renderer, frente a
0,5–0,7 ms de Present y aproximadamente 1–2 ms de swap.

**Matiz de denominador:** el timer inicial del frontend incluye el procesamiento
del lote adquirido, las llamadas a `Skip` y liberaciones, dividido por presents.
No es automáticamente el coste puro de un único `Run` ni de un único draw.
`Skip` también entra internamente en `Run` con `skipDraws`; la instrumentación
reciente se corrigió para excluirlo del contador de runs realmente dibujados.

También hay carga de conversión de vértices en el core: según escena se observaron
tiempos de CPU muy distintos, del orden de 17–55 ms por chunk en pruebas anteriores.
No extrapolar tiempos de una escena headless al coste del pipeline interactivo completo.

## 7. Corrección del diagnóstico anterior

Antes se dividía el tiempo total del renderer entre draws y se obtenían
aproximadamente 0,2–0,27 ms/draw. Se presentó eso como coste del driver por draw.
**Esa atribución era injustificada.** El numerador también contiene buffers,
copias EFB, quads auxiliares, parsing, preparación de estado y otros trabajos.

Tampoco está demostrado que la GPU pueda descartarse de toda la ruta crítica:
un porcentaje medio bajo no excluye sincronizaciones, stores/loads de tiles o
dependencias entre pasadas en una **PowerVR TBDR**.

Los contadores anteriores `progs +3783`, etc. corresponden a **cambios de programa**,
no a miles de links/compilaciones nuevos cada cinco segundos.

El informe antiguo `/tmp/opencode/RE4-POC-TSP-2026-11-07.md` y su copia en la SD
mezclan fases históricas y contienen conclusiones ya superadas. Por ejemplo,
algunos apartados todavía dicen que falta frontend o que falta copiar los discos.
Usarlos como historial, no como descripción única del estado actual.

## 8. Instrumentación reciente: qué se hizo y qué dice

### Interruptores nuevos, todos opt-in

- `RE4_GX_GLPROF=1`: wall-clock CPU por llamada GL mediante CLOCK_MONOTONIC;
  categorías, llamadas, bytes, cambios FBO y copias EFB cada cinco segundos.
- `RE4_GX_NODRAW=1`: omite draws dentro de `Run`, incluidos BaseVertex y quads;
  mantiene el resto de comandos. No omite el dibujo de presentación fuera de Run.
- `RE4_GX_PASSES=1`: logs por frame/chunk de segmentos entre cambios FBO y
  comienzos de copias EFB, con dimensiones y draws.
- `RE4_GX_SKIP_PASS=...`: omite solo los draws de índices o categorías elegidas;
  **no elimina toda la pasada ni sus clears/binds/allocations**.
- `RE4_FRONT_PROFILE_REPLAY=1`: después de unos 45 s retiene un chunk y pausa
  el productor para repetirlo. **El juego deja de responder deliberadamente.**
- `RE4_FRONT_PROFILE_AB=1`: alterna NODRAW en ese replay en fases de diez segundos.
- `RE4_FRONT_PROFILE_SKIP_AB=copy-half`: alterna la omisión de esos draws.

No hay caché GL nueva activable como `RE4_GX_STATE_CACHE`: esa parte quedó pendiente.

### Comparación del MISMO chunk, no del juego vivo

Fuente: `build/glprof-results/step2-replay.log`, secuencia 945.
388 draws GX lógicos, 7.303.744 bytes de vértices y blob de 12.288 bytes.
Se seleccionaron ventanas completas estables, excluyendo las que mezclaban fases.

| Categoría | Con draws: ms / llamadas por frame | NODRAW: ms / llamadas emitidas por frame |
|---|---:|---:|
| **Run total** | **64,53** | **25,42** |
| Draws de geometría BaseVertex | 15,00 / 385 | 0 / 0 |
| DrawArrays auxiliares | **18,24 / 13** | 0 / 0 |
| BufferData + BufferSubData | **8,84 / 4** | **8,78 / 4** |
| Uniforms / bindings UBO | 0,61 / 774 | 0,43 / 774 |
| BindTexture / ActiveTexture / BindSampler | 0,72 / 700 | 0,51 / 700 |
| UseProgram | 0,21 / 133 | 0,14 / 133 |
| TexImage2D | 1,30 / 4 | 0,96 / 4 |
| FBO / copias GL / clears / readbacks | 2,58 / 68 | 0,45 / 68 |
| BindBuffer / VAO / atributos | 0,17 / 108 | 0,10 / 108 |
| Fences/sincronización | 0,39 / ~4 | 0,53 / ~4 |
| Otros estados y operaciones instrumentadas | 1,20 / 612 | 1,08 / 612 |
| LinkProgram / CompileShader en esas ventanas | **0 / 0** | **0 / 0** |

Hay unos 15 ms fuera de los timers GL en ambas variantes. Incluyen trabajo CPU
del renderer e instrumentación; no se han descompuesto todavía.

Hallazgos:

1. La llamada de geometría sale aproximadamente a **39 µs**, no 270 µs.
2. Solo 13 DrawArrays auxiliares suman más tiempo que 385 draws de geometría.
   Hay que investigar sus dependencias/cambios de target, no concluir que todo se
   soluciona ordenando objetos opacos.
3. Subidas de vértices/índices: aproximadamente **8,88 MB por frame**, además de
   ~0,67 MB de escrituras de uniforms persistentes contabilizadas aparte.
4. Las cuatro TexImage2D de esa ventana llevan **datos NULL**: cero bytes de texels
   aportados por CPU no implica coste cero; puede haber realocación de almacenamiento.
5. 26 cambios de draw-FBO y diez copias EFB por frame en ese chunk.
6. Las latencias medidas son wall-clock de las llamadas CPU: pueden incluir espera
   del driver/GPU. No son tiempos exclusivos de ejecución GPU.

### Pasadas y prueba de omisión

En otro chunk retenido, secuencia 1006, se observaron tres segmentos principales
de geometría con **86, 164 y 95 draws**, y múltiples segmentos de un solo quad.
Los targets auxiliares incluían 32×32, 128×98, 128×112, 64×56 y 32×28;
el target principal era 320×264 y la copia de display de ese frame 256×224.

El aspecto de una cadena de reducción de tamaño sugiere efectos/copies, pero NO
se ha identificado con certeza cada pasada como sombra, humo, bloom o DoF.
Los índices de pasadas pueden cambiar entre frames y entre revisiones de la
instrumentación; usar el log del binario exacto, no índices copiados a ciegas.

En `step3-skip-ab.log`, secuencia 992, omitir los draws etiquetados `copy-half`
cambió Run de aproximadamente **58–62 ms a 39–42 ms** en replay.
Eso demuestra sensibilidad a esos comandos, **no que puedan eliminarse sin romper
la imagen**. No se validó corrección visual ni una mejora equivalente jugando.

### Límites del experimento

- El core pausado reduce contención respecto al juego vivo.
- Repetir un chunk también repite cualquier payload que contenga y puede no
  reproducir perfectamente el tráfico de texturas de frames sucesivos.
- La instrumentación agrega búsquedas en mapas, relojes y logs. Falta medir su
  overhead frente a una ejecución idéntica sin instrumentación.
- El swap se midió alrededor de **SDL_GL_SwapWindow**, no interceptando directamente
  eglSwapBuffers. No presentar esas cifras como timer exacto de EGL.
- Los contadores de bytes actuales cubren las rutas usadas; no son una auditoría
  universal de todos los formatos GL ni del tráfico real de memoria del driver.
- No se verificó disponibilidad de glMultiDrawElementsEXT con un ensayo operativo.
  Una recomendación anterior lo dio por soportado: no basarse en ella.

## 9. Auditoría estática útil, todavía incompleta

### Buffers

La ruta observada de vértices/índices sube datos **por chunk**, no por draw:
`glBufferData` para vértices y buffer de índices, y, al preparar merge,
`glBufferData(NULL)` seguido de dos `glBufferSubData` para índices.
Existe una alternativa de ring persistente con fences, pero no se debe asumir
que esté activa solo porque aparece en el código.

Para uniforms sí existe ring persistente o fallback de mapas por bloque.
Los logs de la comparación anterior incluyen escrituras persistentes; no mostraron
MapBufferRange por cada draw en esas ventanas.

### Texturas

`BindTexture` utiliza la clave keyHi/keyLo y busca en `textures`. Si el comando
trae `dataBytes`, llama a `Upload`, que decodifica, crea y sube la textura y sustituye
la entrada previa. Un bind sin payload reutiliza la textura existente.
Falta cerrar la auditoría de cómo se genera/invalida esa clave en el productor;
no afirmar que cada bind decodifica ni que todo contenido está perfectamente deduplicado.

### Programas

`ProgramFor` busca primero por ShaderKey, luego intenta binario en disco, y si falta
enlaza shaders. La compilación perezosa puede ocurrir durante un draw al encontrar
un estado nuevo. En las ventanas estables citadas: cero CompileShader/LinkProgram.

### Caché de estado

Hay caché real de programa, texturas por unidad, algunos uniforms y layout/atributos.
`ApplyState` compara una firma conjunta; al cambiar cualquier parte, puede volver
a emitir estados que individualmente no han cambiado. `Touched` invalida varias
cachés después de pasadas auxiliares.

**Pendiente:** auditar por llamada BlendFunc/Separate, DepthFunc/Mask, CullFace,
BindBuffer/VAO, Viewport/Scissor y Enable/Disable; medir emitidas frente a redundantes
y probar una caché granular opt-in. No está implementada ni medida todavía.

## 10. Qué pedir a la siguiente IA

> Revisa arquitectura, código y logs sin asumir que todo el tiempo de Run es submit
> por draw. Prioriza explicar por qué unos pocos quads de copias/efectos cuestan tanto
> en PowerVR GE8300 y por qué hay uploads de varios MB por chunk. Contrasta stalls,
> cambios FBO, dependencias de lectura/escritura, realocaciones y bandwidth.
> Propón experimentos mínimos y reversibles. No cambies el render normal ni hagas
> sort todavía. Separa evidencia, hipótesis y mejoras realmente medidas.
> No pauses/reinicies la consola ni lances replay sin avisar al usuario.

Siguientes trabajos razonables, todavía no completados:

1. Completar auditorías de caché de estado y buffers/texturas.
2. Medir el coste propio de la instrumentación.
3. Identificar semánticamente las pasadas auxiliares caras y validar sus dependencias.
4. Investigar realocaciones TexImage2D con NULL y volumen de uploads.
5. Repetir las mejoras candidatas en juego vivo, con escena comparable y temperatura
   registrada, comprobando imagen, audio y controles.

Si después se decide ordenar draws: solo opacos con depth test y depth write activos;
no reordenar transparentes ni draws sin z-write. Clave completa de estado y sort estable.
No asumir que multidraw elimina validación o sincronizaciones; concatenar índices
puede ser una alternativa distinta y debe medirse por separado.

## 11. Estado al cerrar este traspaso

Se detuvieron los experimentos y se relanzó el juego sin profiler, NODRAW, replay ni
saltos de pasadas. Última comprobación tras restaurar: proceso vivo en r100 y logs
avanzando, con ventanas de aproximadamente 12–15 fps.

No se implementó sort. No hay todavía una solución demostrada de 30 fps en r100.
La conclusión actual no es «imposible» ni «solo hay que fusionar draws»: falta cerrar
la atribución del coste, especialmente en copias/auxiliares y tráfico de buffers.
