#!/bin/bash

if [ ! -f "CMakeLists.txt" ]; then
    echo "Per favore, esegui lo script dalla root del progetto (NemoHeadUnit)."
    exit 1
fi

AASDK_SRC_DIR="build/_deps/aasdk-src"
OUT_DIR="."
PROTO_PKG="aasdk_proto"
PROTO_OUT_DIR="$OUT_DIR/$PROTO_PKG"

if [ ! -d "$AASDK_SRC_DIR" ]; then
    echo "Directory sorgenti AASDK non trovata in $AASDK_SRC_DIR"
    echo "Per favore, prima esegui 'cmake -B build' per scaricare le dipendenze."
    exit 1
fi

# Pulizia preventiva della directory di output
rm -rf "$PROTO_OUT_DIR"
mkdir -p "$PROTO_OUT_DIR"
touch "$PROTO_OUT_DIR/__init__.py"

# Rimuoviamo il file corrotto upstream
find "$AASDK_SRC_DIR" -name "WifiSecurityRequestMessage.proto" -delete

ROOT_DIR=$(pwd)
AASDK_PROTO="$ROOT_DIR/$AASDK_SRC_DIR/aasdk_proto"
AAP_PROTO="$ROOT_DIR/$AASDK_SRC_DIR/protobuf"
OUT_ABS="$ROOT_DIR/$PROTO_OUT_DIR"

echo "Generazione moduli Python per Protobuf in $AASDK_PROTO e $AAP_PROTO..."

pushd "$AASDK_PROTO" > /dev/null
find . -name "*.proto" -print0 | xargs -0 protoc -I. -I="$AAP_PROTO" --python_out="$OUT_ABS"
popd > /dev/null

pushd "$AAP_PROTO" > /dev/null
find . -name "*.proto" -print0 | xargs -0 protoc -I. -I="$AASDK_PROTO" --python_out="$OUT_ABS"
popd > /dev/null

if [ $? -eq 0 ]; then
    echo "Moduli generati con successo in $PROTO_OUT_DIR"
    
    # Crea i file __init__.py per tutti i sub-package generati
    find "$PROTO_OUT_DIR" -type d -exec touch {}/__init__.py \;

    # Python 3 non supporta import impliciti tra pacchetti sibling o nella stessa dir se non sono specificati come from . o absolute
    # Dato che protoc non inietta il prefisso del pacchetto "aasdk_proto." nei file generati (ma usa solo aap_protobuf.xxx),
    # modifichiamo tutte le righe generatrici di import di aap_protobuf per puntare al package root aasdk_proto.
    echo "Correzione degli import relativi per Python 3..."
    if [ "$(uname)" == "Darwin" ]; then
        find "$PROTO_OUT_DIR" -name "*.py" -exec sed -i '' -E 's/from aap_protobuf/from aasdk_proto.aap_protobuf/g' {} +
        find "$PROTO_OUT_DIR" -name "*.py" -exec sed -i '' -E 's/^import (.*_pb2)/from . import \1/g' {} +
    else
        find "$PROTO_OUT_DIR" -name "*.py" -exec sed -i -E 's/from aap_protobuf/from aasdk_proto.aap_protobuf/g' {} +
        find "$PROTO_OUT_DIR" -name "*.py" -exec sed -i -E 's/^import (.*_pb2)/from . import \1/g' {} +
    fi
    echo "Fatto!"
else
    echo "Errore durante la generazione dei Protobuf."
fi
