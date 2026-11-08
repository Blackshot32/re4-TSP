# RE4 / TrimUI: vértices, fases CPU y diagnóstico de GPU

Instrumentación de 7 de noviembre de 2026. No se implementaron optimizaciones,
sort ni cambios de calidad. Los interruptores nuevos están apagados por defecto.
Pruebas en r100, merge=1, escala=0,5× y un worker GX, en juego vivo sin replay.

## Vértices: RE4_GX_VTXSTATS=1

Fuente: `/tmp/opencode/re4/build/glprof-results/vtx-phases-offgl.log`.
En los últimos 100 frames medidos hubo un chunk subido por frame. Los frames
omitidos por Skip no se consideran subidas; se compara con el anterior frame
renderizado, no necesariamente con el frame inmediatamente anterior del core.

| Métrica | Media de 100 frames | Últimos 20, tramo más estable |
|---|---:|---:|
| Vértices subidos / frame y chunk | 96.711 | 112.044 |
| Bytes de vértices | 3.467.414 | 4.019.767 |
| Bytes por vértice | ~35,85 | ~35,88 |
| Bytes de índices originales, uint16 | 377.391 | 426.098 |
| Bytes de índices adicionales preparados para merge | 373.757 | 421.850 |
| Bytes idénticos: dirección staging + tamaño + hash | 0 % | 0 % |
| Bytes idénticos: identidad lógica + tamaño + hash | 71,22 % | 85,55 % |
| Bytes idénticos: tamaño + hash, sin exigir dirección | 71,42 % | 85,56 % |

Los porcentajes están ponderados por bytes, no por número de draws. La identidad
lógica utiliza OWNER, función, ordinal de draw, layout y tamaño, además del hash.
No equivale a una dirección de la geometría original. El chunk no transporta la
dirección original de las arrays GameCube, solo offsets en buffers temporales que
rotan: por eso la comparación literal por dirección staging da cero.

Los hashes se calculan por bloque de draw (rangos idénticos repetidos en un chunk
solo se contabilizan una vez). Se validaron 1.033 chunks: sin sobreconteo de bytes
ni padding negativo. Un test CPU comprueba también igualdad, cambio parcial y
entrada desalineada. Hashes iguales son evidencia de contenido repetido, no una
garantía de que se pueda omitir un draw o conservar todos sus estados.

### Formato de vértices ya decodificado para GLES

| Stride | Componentes | Porcentaje de bytes en los 100 frames |
|---:|---|---:|
| **36 B** | posición float32×3 (12) + normal float32×3 (12) + RGBA unorm8×4 (4) + UV float32×2 (8) | **98,874 %** |
| 24 B | posición 12 + color 4 + UV 8 | 0,542 % |
| 32 B | posición 12 + normal 12 + UV 8 | 0,495 % |
| 20 B | posición 12 + UV 8 | 0,037 % |
| 16 B | posición 12 + color 4 | 0,035 % |
| 44 B | posición 12 + normal 12 + UV 8 + índices de matrices empaquetados en tres uint32 (12) | 0,015 % |
| 12 B | posición 12 | 0,003 % |

No son los formatos comprimidos de las arrays GX originales: el productor ya los
expandió. Los layouts admiten más colores/UV y NBT, pero no dominaron esta muestra.
El hash/análisis de bloques costó ~5,71 ms por chunk de media en esos 100 frames,
sin contar toda la escritura de logs. No debe quedar activado al jugar normalmente.

## Fases exclusivas CPU: RE4_GX_PHASES=1

Fuente: últimas tres ventanas de `phases-detail-offgl.log`, 235 runs dibujados.
Se excluyen llamadas GL medidas y se resta el tiempo de las fases hijas; no sumar
tiempos inclusivos. Estas cifras todavía contienen overhead del diagnóstico.

| Fase | ms por Run |
|---|---:|
| Despacho de ops y handlers no separados | 3,567 |
| Merge, excluyendo preparación de índices | 3,109 |
| Preparación/recorrido de índices | 1,708 |
| ApplyState, sin firma ni GL | 0,715 |
| Firma de estado | 0,139 |
| Construcción de ShaderKey | 0,116 |
| Búsqueda/preparación de programas | 0,526 |
| Copias CPU instrumentadas | 0,676 |
| Preparación/subida de uniforms, sin memcpy ni GL | 0,484 |
| Preparación restante del draw | 2,510 |
| Resto de Run | 1,369 |
| Decode/upload CPU de texturas nuevas en estas ventanas | 0,000 |
| **Total fuera de GL** | **14,929** |
| Llamadas GL: wall-clock CPU, puede incluir espera | 45,290 |
| **Run total** | **60,219** |

Memcpy cubre las copias de la ruta caliente: uniforms, bloques de merge, TLUT y
rings cuando se usan. Copias pequeñas de records de ops se incluyen en despacho.
Los buffers que copia internamente el driver son tiempo GL, no memcpy de esta tabla.
No se está midiendo aquí el decoder GX del hilo productor: está fuera de Run.

## Overhead del diagnóstico

Tres últimas ventanas de cada arranque; medias ponderadas por runs:

| Configuración | Run medio |
|---|---:|
| Solo timer total de Run; GLPROF=0, sin fases detalladas ni hashes | 55,531 ms |
| PHASES=1, GLPROF=0, sin hashes | 60,219 ms |
| PHASES=1, GLPROF=1, sin hashes | 62,369 ms |

`RE4_GX_PHASES=total` es el modo de control con solo los relojes de Run.
Las diferencias observadas son +4,688 ms al activar fases y +2,150 ms adicionales
al activar GLPROF. **No son estimaciones causales limpias de overhead:** el juego
estaba vivo, las muestras avanzan distinto y las frecuencias no quedaron constantes.
Se observaron 1,416 y 1,608 GHz y temperaturas de aproximadamente 79–83 °C.
No se cambiaron frecuencias ni configuración térmica para estas pruebas.

Como control independiente se calibraron 40.000 wrappers sobre funciones C++ vacías,
sin emitir comandos GL ni cambiar el render:

| Microbenchmark CPU | ns por llamada |
|---|---:|
| Wrapper con GLPROF apagado | 29,661 |
| Wrapper con GLPROF encendido | 645,076 |
| Diferencia | **615,415** |
| clock_gettime en bucle | ~191,081 |

Es una estimación del coste del wrapper (mapa/string/relojes), no del coste de un
draw ni una cifra exacta de overhead por frame. Depende de frecuencia y mezcla de llamadas.

## Extensiones y GPU: RE4_GX_GPUTIME=1

Extensiones consultadas en el contexto GLES de la consola:

| Extensión | Anunciada |
|---|---|
| GL_EXT_disjoint_timer_query | **No** |
| GL_EXT_buffer_storage | Sí |
| GL_EXT_multi_draw_arrays | Sí |
| GL_EXT_discard_framebuffer | Sí |
| GL_EXT_texture_storage | No; texture storage también existe en GLES 3 core |

No hay tiempos GPU asíncronos válidos por quad/pasada en este hardware con esta API
anunciada. El código de queries opt-in se añadió con resultados uint64, lectura
asíncrona, control de disjoint y alternancia entre frames de pasadas y de auxiliares
para no anidar queries TIME_ELAPSED. **Esa ruta no pudo probarse en esta GE8300.**

### CPU por pasada, sin glFinish añadido

Fuente: últimos 100 runs de `gpu-capabilities.log`. Media por segmento de pasada,
no suma por frame. Medición con PHASES=1, GLPROF=0 y seguimiento de pasadas activo.

| Clase / target | Segmentos | CPU total por segmento | CPU en draws del segmento | GPU |
|---|---:|---:|---:|---|
| Geometría / 320×264 | 1.000 | 3,356 ms | 1,728 ms | No disponible |
| copy-half / 32×32 | 100 | 1,197 ms | 0,809 ms | No disponible |
| copy-half / 128×98 | 100 | 1,168 ms | 1,001 ms | No disponible |
| copy-half / 128×112 | 100 | 1,469 ms | 1,279 ms | No disponible |
| **copy-half / 64×56** | **100** | **4,664 ms** | **4,458 ms** | No disponible |
| copy-half / 32×28 | 500 | 1,082 ms | 0,954 ms | No disponible |
| copy-display / 256×224 | 100 | 1,141 ms | 1,015 ms | No disponible |

Los segmentos llamados geometry también incluyen algunos quads auxiliares fuera
de EfbCopy. La etiqueta es la procedencia del comando, no una identificación visual
segura del efecto. La copia 64×56 destaca, pero su tiempo CPU puede contener espera
por trabajo previo y no demuestra que ese pequeño quad haga 4,46 ms de trabajo GPU.

### Fallback glFinish

La variante `RE4_GX_FINISH_AUX=1` mide por cada DrawArrays dentro de Run:
espera de un glFinish previo, duración CPU de la llamada y espera del glFinish
posterior. La suma draw+post se etiqueta como intervalo aislado, **no tiempo GPU puro**.
Resultados: últimos 200 chunks de `gpu-finish-aux.log`, juego vivo. Medias por
llamada; no son los mismos frames que la tabla sin glFinish.

| Target / clase | Llamadas | Finish previo: trabajo pendiente | CPU DrawArrays | Finish posterior | Draw + finish posterior |
|---|---:|---:|---:|---:|---:|
| 32×32 / copy-half | 200 | 1,992 ms | 0,452 ms | 0,832 ms | 1,284 ms |
| 128×98 / copy-half | 200 | **9,866 ms** | 0,504 ms | 0,912 ms | 1,416 ms |
| 128×112 / copy-half | 200 | **5,467 ms** | 0,479 ms | 1,027 ms | 1,506 ms |
| 64×56 / copy-half | 200 | **4,356 ms** | 0,467 ms | 0,897 ms | 1,364 ms |
| 32×28 / copy-half | 996 | 2,133 ms | 0,271 ms | 0,791 ms | 1,063 ms |
| 256×224 / copy-display | 199 | 2,377 ms | 0,515 ms | 1,032 ms | 1,547 ms |

Sumas medias por chunk en esa muestra: 34,669 ms esperando antes de los quads,
3,765 ms en sus DrawArrays y 8,637 ms esperando después. Intervalos aislados
draw+post suman 12,401 ms/chunk. Varias copias dependen de geometría/efectos previos:
la espera anterior no se puede atribuir al quad que aún no se ha enviado.

La prueba sincronizada alteró claramente la ejecución: Run en sus tres últimas
ventanas fue 95,110 / 90,031 / 87,537 ms. El governor se leyó como performance,
pero se observaron límites/frecuencias efectivas cambiantes; en esta variante la
muestra a 45 s fue 1,608 GHz y ~80,3 °C. No presentar la diferencia con otro arranque
como coste puro de GPU ni overhead exacto de glFinish.

**Conclusión medida:** hay mucho contenido de vértices repetido y espera importante
por trabajo anterior alrededor de los auxiliares. Ni los quads caros ni el resto
de Run pueden explicarse solo como validación CPU por draw. Falta tiempo GPU directo
para atribuir con precisión cada dependencia. No se cambió el orden de draws,
no se añadió una caché de geometría y no se alteraron uploads ni efectos.

## Código y logs

- Checkout: `/tmp/opencode/re4`.
- Código nuevo: `gx_diag.h`, `gx_gpu_diag.h` y modificaciones diagnósticas en
  `gx_gl_prof.h`, `gx_gl.cpp`, `front.cpp`.
- Test CPU: `port/whole/tests/gx_diag_test.cpp`, ejecutado correctamente.
- Logs: `build/glprof-results/`.
- Resumen reproducible: `python3 summarize_diagnostics.py`.

No interpretar el porcentaje de geometría repetida como mejora de FPS conseguida:
no se implementó una caché de geometría. Tampoco se implementó sort ni se eliminaron pasadas.

Al finalizar se relanzó el modo normal con GLPROF, VTXSTATS, PHASES, GPUTIME,
FINISH_AUX, PASSES, NODRAW y replay desactivados explícitamente.
