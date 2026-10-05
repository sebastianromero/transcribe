# transcribe

UI minima en C (Dear ImGui vía cimgui + GLFW) para arrastrar un archivo de
audio/video y obtener una transcripción con diarización (quién habla),
editable y exportable a TXT/MD con o sin timestamps.

Motores usados solo a través de su API en C:
- **whisper.cpp** — transcripción (multilingüe: es/en y más).
- **sherpa-onnx** — diarización de locutores (pyannote-onnx).
- **cimgui** — UI.
- **dr_libs** — decodifica wav/mp3/flac; cualquier otro contenedor (mp4, mov,
  m4a...) pasa por un `ffmpeg` bundleado/en PATH.

## Build (desarrollo)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./scripts/fetch_models.sh small   # o: medium
cp -r models build/bin/models
./build/bin/transcribe
```

La primera configuración de CMake descarga y compila glfw, cimgui/imgui,
whisper.cpp y sherpa-onnx vía `FetchContent`; puede tardar varios minutos.

## Empaquetado autocontenido (doble click)

Para distribuir sin dependencias:
1. Compilar en cada OS objetivo (o cross-compilar).
2. Copiar `models/` (whisper + diarization) junto al binario.
3. Incluir un binario estático de `ffmpeg` para esa plataforma junto al
   ejecutable (usado solo para extraer audio de contenedores de video).
4. macOS: empaquetar como `.app` con esos recursos en `Resources/`.
   Windows: carpeta con el `.exe` + `models/` + `ffmpeg.exe`.
   Linux: AppImage con todo dentro.

## Idiomas

whisper multilingüe soporta ES/EN de fábrica y decenas de idiomas más —
basta con cambiar `session.language` (o exponerlo en la UI) al código ISO
correspondiente ("es", "en", "auto" para detección automática).
