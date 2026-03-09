#!/bin/bash

# Verifica che l'utente si trovi nella root del progetto
if [ ! -f "CMakeLists.txt" ]; then
    echo "Per favore, esegui lo script dalla root del progetto (NemoHeadUnit)."
    exit 1
fi

# Assicurati di aver clonato/scaricato le dipendenze via CMake almeno una volta
AASDK_PROTO_DIR="build/_deps/aasdk-src/aasdk_proto"
OUT_DIR="python/aasdk_proto"

if [ ! -d "$AASDK_PROTO_DIR" ]; then
    echo "Directory protobuf AASDK non trovata in $AASDK_PROTO_DIR"
    echo "Per favore, prima esegui 'cmake -B build' per scaricare le dipendenze."
    exit 1
fi

mkdir -p "$OUT_DIR"
touch "$OUT_DIR/__init__.py"

echo "Generazione moduli Python per AASDK Protobuf..."
protoc -I="$AASDK_PROTO_DIR" --python_out="$OUT_DIR" "$AASDK_PROTO_DIR"/*.proto

if [ $? -eq 0 ]; then
    echo "Moduli generati con successo in $OUT_DIR"
    
    # Rinomina eventuali import assoluti a causa del modulo interno
    # protoc non mette sempre l'import relativo se sono nella stessa cartella,
    # ma in Python 3 serve import relativo se non è nel PYTHONPATH.
    # Con sys.path.append() in test_headless lo troviamo ugualmente se passiamo python come radice.
else
    echo "Errore durante la generazione dei Protobuf."
fi
