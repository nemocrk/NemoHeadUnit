#!/bin/bash

# Verifica che l'utente si trovi nella root del progetto
if [ ! -f "CMakeLists.txt" ]; then
    echo "Per favore, esegui lo script dalla root del progetto (NemoHeadUnit)."
    exit 1
fi

# Assicurati di aver clonato/scaricato le dipendenze via CMake almeno una volta
AASDK_SRC_DIR="build/_deps/aasdk-src"
OUT_DIR="python/aasdk_proto"

if [ ! -d "$AASDK_SRC_DIR" ]; then
    echo "Directory sorgenti AASDK non trovata in $AASDK_SRC_DIR"
    echo "Per favore, prima esegui 'cmake -B build' per scaricare le dipendenze."
    exit 1
fi

mkdir -p "$OUT_DIR"
touch "$OUT_DIR/__init__.py"

# Pulizia preventiva della directory di output
rm -rf "$OUT_DIR"/*
touch "$OUT_DIR/__init__.py"

# Workaround: il repository upstream opencardev/aasdk contiene un file corrotto
# (WifiSecurityRequestMessage.proto) con dentro macro C++ (#define BOOST_TEST...)
# che fa crashare il protoc. Dato che non ci serve (usiamo USB), lo rimuoviamo prima di generare.
if [ -f "$AASDK_SRC_DIR/aasdk_proto/WifiSecurityRequestMessage.proto" ]; then
    echo "Rimuovo WifiSecurityRequestMessage.proto (file upstream corrotto)..."
    rm -f "$AASDK_SRC_DIR/aasdk_proto/WifiSecurityRequestMessage.proto"
fi
if [ -f "$AASDK_SRC_DIR/protobuf/aap_protobuf/control/WifiSecurityRequestMessage.proto" ]; then
    echo "Rimuovo aap_protobuf/.../WifiSecurityRequestMessage.proto (file upstream corrotto)..."
    rm -f "$AASDK_SRC_DIR/protobuf/aap_protobuf/control/WifiSecurityRequestMessage.proto"
fi

echo "Generazione moduli Python per TUTTI i Protobuf in $AASDK_SRC_DIR..."

# Troviamo tutte le directory che contengono file .proto per passarle come -I
PROTO_DIRS=$(find "$AASDK_SRC_DIR" -name "*.proto" -exec dirname {} \; | sort -u)
INCLUDE_ARGS=""
for dir in $PROTO_DIRS; do
    INCLUDE_ARGS="$INCLUDE_ARGS -I=$dir"
    # Se i proto usano path relativi tipo 'import "service/control/Message.proto"'
    # Dobbiamo includere anche la radice di quei path (es. protobuf/aap_protobuf)
done

# Aggiungiamo anche le cartelle radice principali per gli import nei proto
INCLUDE_ARGS="-I=$AASDK_SRC_DIR/aasdk_proto -I=$AASDK_SRC_DIR/protobuf/aap_protobuf $INCLUDE_ARGS"

# Raccogliamo tutti i file .proto e li passiamo a protoc in un colpo solo
find "$AASDK_SRC_DIR" -name "*.proto" -print0 | xargs -0 protoc $INCLUDE_ARGS --python_out="$OUT_DIR"

if [ $? -eq 0 ]; then
    echo "Moduli generati con successo in $OUT_DIR"
    
    # Siccome i proto potrebbero creare sottocartelle (es. service/control/...),
    # dobbiamo assicurarci che ogni directory abbia un __init__.py per l'import python.
    find "$OUT_DIR" -type d -exec touch {}/__init__.py \;

    # Fix import relativi per Python 3: converte 'import Pippo_pb2' in 'from . import Pippo_pb2'
    # e gestisce anche i sub-package generati.
    echo "Correzione degli import relativi per Python 3..."
    if [ "$(uname)" == "Darwin" ]; then
        # macOS sed
        find "$OUT_DIR" -name "*.py" -exec sed -i '' -E 's/^import (.*_pb2)/from . import \1/g' {} +
    else
        # Linux sed
        find "$OUT_DIR" -name "*.py" -exec sed -i -E 's/^import (.*_pb2)/from . import \1/g' {} +
    fi
    echo "Fatto!"
else
    echo "Errore durante la generazione dei Protobuf."
fi
