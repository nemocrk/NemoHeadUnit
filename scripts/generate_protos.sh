#!/bin/bash

if [ ! -f "CMakeLists.txt" ]; then
    echo "Per favore, esegui lo script dalla root del progetto (NemoHeadUnit)."
    exit 1
fi

AASDK_SRC_DIR="build/_deps/aasdk-src"
OUT_DIR="python/aasdk_proto"

if [ ! -d "$AASDK_SRC_DIR" ]; then
    echo "Directory sorgenti AASDK non trovata in $AASDK_SRC_DIR"
    echo "Per favore, prima esegui 'cmake -B build' per scaricare le dipendenze."
    exit 1
fi

# Pulizia preventiva della directory di output
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"
touch "$OUT_DIR/__init__.py"

# Rimuoviamo il file corrotto upstream
find "$AASDK_SRC_DIR" -name "WifiSecurityRequestMessage.proto" -delete

ROOT_DIR=$(pwd)
AASDK_PROTO="$ROOT_DIR/$AASDK_SRC_DIR/aasdk_proto"
AAP_PROTO="$ROOT_DIR/$AASDK_SRC_DIR/protobuf"
OUT_ABS="$ROOT_DIR/$OUT_DIR"

echo "Generazione moduli Python per Protobuf in $AASDK_PROTO e $AAP_PROTO..."

# Compiliamo i proto della cartella aasdk_proto
# Posizionandoci nella cartella, gli import relativi (es. import "StatusEnum.proto") verranno risolti correttamente
pushd "$AASDK_PROTO" > /dev/null
find . -name "*.proto" -print0 | xargs -0 protoc -I. -I="$AAP_PROTO" --python_out="$OUT_ABS"
popd > /dev/null

# Compiliamo i proto della cartella protobuf (aap_protobuf/..)
# Gli import in questi file (es. import "aap_protobuf/aaw/Status.proto") verranno 
# risolti grazie a protoc -I. in cui . è $AASDK_SRC_DIR/protobuf
pushd "$AAP_PROTO" > /dev/null
find . -name "*.proto" -print0 | xargs -0 protoc -I. -I="$AASDK_PROTO" --python_out="$OUT_ABS"
popd > /dev/null

if [ $? -eq 0 ]; then
    echo "Moduli generati con successo in $OUT_DIR"
    
    # Crea i file __init__.py per tutti i sub-package generati (es. aap_protobuf, service, ecc.)
    find "$OUT_DIR" -type d -exec touch {}/__init__.py \;

    # Fix import relativi per Python 3 (solo per i file generati senza package, es. StatusEnum_pb2)
    # Questa Regex tocca solo le righe che iniziano con "import X_pb2", lasciando intatti i "from package import X_pb2"
    echo "Correzione degli import relativi per Python 3..."
    if [ "$(uname)" == "Darwin" ]; then
        find "$OUT_DIR" -name "*.py" -exec sed -i '' -E 's/^import (.*_pb2)/from . import \1/g' {} +
    else
        find "$OUT_DIR" -name "*.py" -exec sed -i -E 's/^import (.*_pb2)/from . import \1/g' {} +
    fi
    echo "Fatto!"
else
    echo "Errore durante la generazione dei Protobuf."
fi
