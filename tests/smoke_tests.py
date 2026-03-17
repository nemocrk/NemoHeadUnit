import sys
from pathlib import Path


def _add_build_dir():
    root = Path(__file__).resolve().parents[1]
    build_dir = root / "build"
    if build_dir.exists():
        sys.path.insert(0, str(build_dir))


def test_protobuf():
    import protobuf as core

    msg = core.GetProtobuf("aap_protobuf.service.media.shared.message.Config")
    assert msg.type_name().endswith(".Config")

    try:
        msg.serialize_to_string()
    except RuntimeError as exc:
        assert "IsInitialized" in str(exc)


def test_imports():
    import event  # noqa: F401
    import audio_event  # noqa: F401


if __name__ == "__main__":
    _add_build_dir()
    test_imports()
    test_protobuf()
    print("Smoke test OK")
