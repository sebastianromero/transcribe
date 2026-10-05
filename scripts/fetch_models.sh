#!/usr/bin/env bash
# Downloads the models needed for local development / self-contained packaging.
# Run from the repo root: ./scripts/fetch_models.sh [small|medium]
set -euo pipefail

SIZE="${1:-small}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODELS_DIR="$ROOT/models"
DIA_DIR="$MODELS_DIR/diarization"

mkdir -p "$MODELS_DIR" "$DIA_DIR"

echo "==> Whisper ggml model ($SIZE)"
case "$SIZE" in
  small)  URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin" ;;
  medium) URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin" ;;
  *) echo "Unknown size: $SIZE (use small or medium)"; exit 1 ;;
esac
OUT="$MODELS_DIR/ggml-$SIZE.bin"
[ -f "$OUT" ] || curl -L --fail -o "$OUT" "$URL"

echo "==> Diarization: pyannote segmentation model (sherpa-onnx)"
SEG_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-segmentation-models/sherpa-onnx-pyannote-segmentation-3-0.tar.bz2"
if [ ! -f "$DIA_DIR/segmentation.onnx" ]; then
  TMP_SEG_DIR="$(mktemp -d)"
  curl -L --fail -o "$TMP_SEG_DIR/seg.tar.bz2" "$SEG_URL"
  tar xjf "$TMP_SEG_DIR/seg.tar.bz2" -C "$TMP_SEG_DIR"
  cp "$TMP_SEG_DIR"/sherpa-onnx-pyannote-segmentation-3-0/model.onnx "$DIA_DIR/segmentation.onnx"
  rm -rf "$TMP_SEG_DIR"
fi

echo "==> Diarization: speaker embedding model (CAM++, via sherpa-onnx)"
EMB_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx"
[ -f "$DIA_DIR/embedding.onnx" ] || curl -L --fail -o "$DIA_DIR/embedding.onnx" "$EMB_URL"

echo "Done. Models are in $MODELS_DIR"
echo "NOTE: model URLs on the sherpa-onnx/whisper.cpp release pages change over time;"
echo "if a download 404s, check https://github.com/k2-fsa/sherpa-onnx/releases and"
echo "https://huggingface.co/ggerganov/whisper.cpp/tree/main for the current asset name."
