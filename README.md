# Boba (VST3 / AU, macOS)

Supresor dinámico de resonancias en C++/JUCE, con motor espectral (STFT por bin). Combina las funciones de un
supresor tipo *soothe* con el compresor espectral por sidechain original de Boba (basado en el dispositivo Max for Live **Boba.amxd**).

## Instalación (macOS)

1. Descarga `Boba-x.y.z.dmg` desde la pestaña **Releases** (o `Boba-dmg` desde **Actions → último build → Artifacts**).
2. Abre el DMG y arrastra `Boba.vst3` sobre **VST3** y `Boba.component` sobre **Components** (pedirá contraseña de administrador).
3. Como el plugin no está notarizado por Apple, macOS puede bloquearlo. En Terminal:

       xattr -cr /Library/Audio/Plug-Ins/VST3/Boba.vst3
       xattr -cr /Library/Audio/Plug-Ins/Components/Boba.component

4. Reinicia tu DAW y vuelve a escanear plugins.

## Compilar y publicar con GitHub (sin necesitar un Mac)

    git init && git add . && git commit -m "Boba 2.0"
    git branch -M main
    git remote add origin https://github.com/TU_USUARIO/Boba.git
    git push -u origin main

    git tag v2.0.0 && git push origin v2.0.0      # genera el DMG y lo publica en Releases

Cada `push` compila, ejecuta los tests DSP y deja el DMG en **Actions → Artifacts**. Las etiquetas `vX.Y.Z` además lo adjuntan a un Release.
(La carpeta `.github` es oculta en Finder: usa Git desde Terminal, o `Cmd+Shift+.` para verla.)

En local (Mac): `./instalar.sh` compila e instala en tu usuario, o `cmake -B build && cmake --build build --target Boba_VST3 Boba_AU -j8 && ./make_dmg.sh 2.0.0`.

## Controles

| Control | Qué hace |
|---|---|
| **Depth** (0-10) | Cuánto se reduce cada resonancia (10 = la aplana hasta la envolvente). |
| **Detail** (0-10) | Anchura de lo que se considera resonancia: bajo = zonas anchas, alto = picos finos. |
| **SOFT / HARD** | Selectividad: *soft* actúa de forma ancha y gradual; *hard* es más estrecho y directo. |
| **Attack / Release** | Tiempos reales en ms de la reducción. |
| **Tilt** (Detail / Attack / Release) | Desplaza cada valor a lo largo del espectro (p. ej. más detalle en agudos, release más lento en graves). |
| **Range** (marcadores rojo/verde) | Arrástralos en el display: solo se procesa entre esas frecuencias. Doble clic los restablece. |
| **MAX CUT** | Límite de reducción (Off, -3 … -24 dB), con curva suave. |
| **L/R - M/S**, **Link**, **Focus**, **EVEN** | Modo estéreo; Link acopla la reducción de ambos canales; Focus la sesga hacia un canal (L o M / R o S); EVEN la centra. |
| **INT / EXT**, **LISTEN** | Detección interna o por sidechain externo; LISTEN escucha la señal de detección. |
| **DELTA** | Escucha solo lo que se elimina. |
| **Wet Trim / Mix / Out** | Ganancia del proceso, mezcla seca/procesada y ganancia de salida. |
| **BYPASS** | Bypass compensado en latencia, sin clics. |
| **A / B / A>B** | Compara dos ajustes (no se guardan con la sesión). |
| **Quality** (Low, Medium, High, Ultra) y **LOW LAT** | FFT 1024/2048/2048/4096. LOW LAT usa FFT 512 (≈10 ms). |
| **Engine** | *Resonance* (nuevo) o *Compressor* (el sidechain espectral original, con **Thresh** y el **EMPH EQ** del sidechain). |
| **Presets** | Selector con ajustes de fábrica (Harsh Female Voc, Sibilance Tamer, Gentle Master, Muddy Low-End, Spectral Ducker). |

El display muestra el espectro de entrada (relleno), la salida (línea), y arriba la **curva de reducción real** del motor (escala -3 … -24 dB con el botón de la esquina).

## Qué se cambió para eliminar la saturación

El motor anterior comparaba cada bin con un umbral fijo y aplicaba la ganancia bin a bin y frame a frame, sin suavizar. Eso produce "ruido musical" y aspereza (suena a saturación). Ahora:

1. **Detección por resonancias**: un bin se reduce solo si sobresale de la envolvente espectral de su entorno (con una pasada robusta para que los picos no arrastren la envolvente) y por encima de un margen de ~2,5 dB.
2. **Detector suavizado en el tiempo** (subida rápida, bajada lenta) para que el ruido aleatorio de cada bin no se confunda con resonancias.
3. **Suavizado en frecuencia de la curva de reducción** (dilatar + difuminar, sin perder profundidad): evita saltos de ganancia entre bins vecinos, que son los que "ensucian" el sonido.
4. **Attack/Release reales en ms** por bin (con Tilt). Antes, con los valores por defecto prácticamente no había suavizado (el modo "Original" del README anterior ya lo advertía). Ese modo se eliminó.
5. **Sin filtro DC de 18 Hz en la ruta de audio**: el audio ya no se filtra; solo la señal de detección.
6. **Sin reasignación de memoria en el hilo de audio** al cambiar la calidad (antes podía dar clics).
7. La ganancia es siempre ≤ 1: el plugin nunca sube el nivel.

## Verificado (tests en C++ puro, `tests/test_core.cpp`)

`g++ -O2 -std=c++17 tests/test_core.cpp -o t && ./t`

* Con Depth 0, la salida es idéntica a la entrada retrasada (error < 2e-6) para todos los tamaños de FFT 128-4096 y solapamientos 2-32. Mix 0 y Bypass también.
* Un tono de 3 kHz sobre un lecho de ruido baja ≈35 dB a Depth 10; el ruido del resto del espectro cambia < 0,5 dB. Depth escala la reducción y Max Cut la limita (6 dB → 6,0 dB).
* Un marcador de rango por encima del tono lo deja intacto.
* **Test anti-saturación**: un tono estable que pasa por el plugin sigue siendo un tono limpio (residuo < -62 dB), en ambos motores y con soft/hard.
* En el peor caso (sin suavizado temporal, hard), la salida nunca gana energía ni supera el pico de entrada.
* Delta = seco retrasado − procesado; el modo Compressor sigue reduciendo solo el bin del sidechain (-49 dB en 1 kHz, 5 kHz intacto).
* CPU: 1,4 % (FFT 512) a 4,2 % (FFT 4096, solape 8) de un núcleo.

## NO verificado - revisa

* **La interfaz y el procesador JUCE no se compilaron donde se escribieron** (sin acceso a JUCE). Los compila GitHub Actions; si falla algún paso, el log dirá la línea exacta.
* No hay comparación A/B con soothe3 ni con tu `.amxd`: los valores de Depth/Detail no equivalen numéricamente a los de oeksound.
* La latencia es `FFT - 1` muestras (≈43 ms en High a 48 kHz, ≈85 ms en Ultra, ≈10 ms con LOW LAT). No es posible latencia cero con este tipo de motor.
* Los parámetros cambiaron de IDs y rangos respecto a la versión 1: las sesiones guardadas con la versión anterior no cargarán los mismos valores.

## Licencia

Este proyecto usa JUCE bajo su licencia AGPLv3, por lo que se distribuye bajo **AGPLv3** (ver `LICENSE`).
