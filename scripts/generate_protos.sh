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

# Workaround: il repository upstream opencardev/aasdk contiene un file corrotto
# (WifiSecurityRequestMessage.proto) con dentro macro C++ (#define BOOST_TEST...)
# che fa crashare il protoc. Dato che non ci serve (usiamo USB), lo rimuoviamo prima di generare.
if [ -f "$AASDK_PROTO_DIR/WifiSecurityRequestMessage.proto" ]; then
    echo "Rimuovo $AASDK_PROTO_DIR/WifiSecurityRequestMessage.proto (file upstream corrotto)..."
    rm -f "$AASDK_PROTO_DIR/WifiSecurityRequestMessage.proto"
fi

echo "Generazione moduli Python per AASDK Protobuf..."
protoc -I="$AASDK_PROTO_DIR" --python_out="$OUT_DIR" "$AASDK_PROTO_DIR"/*.proto

if [ $? -eq 0 ]; then
    echo "Moduli generati con successo in $OUT_DIR"
    
    # Fix import relativi per Python 3
    echo "Correzione degli import relativi per Python 3..."
    if [ "$(uname)" == "Darwin" ]; then
        # macOS sed
        sed -i '' -E 's/^import (.*_pb2)/from . import \1/g' "$OUT_DIR"/*.py
    else
        # Linux sed
        sed -i -E 's/^import (.*_pb2)/from . import \1/g' "$OUT_DIR"/*.py
    fi
    echo "Fatto!"
else
    echo "Errore durante la generazione dei Protobuf."
fi
