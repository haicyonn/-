import os
import sys
import time

MELO_OUT_DIR = os.environ.get("MELO_OUT_DIR", os.path.join(os.path.dirname(__file__), "tts_out"))
MELO_LANG = os.environ.get("MELO_LANG", "ZH")
MELO_DEVICE = os.environ.get("MELO_DEVICE", "cuda")
MELO_SPEAKER = os.environ.get("MELO_SPEAKER", "")
MELO_SPEED = float(os.environ.get("MELO_SPEED", "1.0"))


def load_tts():
    try:
        from melo.api import TTS  # type: ignore
        return TTS
    except Exception:
        try:
            from melo import TTS  # type: ignore
            return TTS
        except Exception:
            return None


def pick_speaker(tts):
    spk_map = getattr(getattr(tts, "hps", None), "data", None)
    spk2id = getattr(spk_map, "spk2id", None) if spk_map else None
    if not spk2id:
        return 0
    if MELO_SPEAKER and MELO_SPEAKER in spk2id:
        return spk2id[MELO_SPEAKER]
    if MELO_LANG in spk2id:
        return spk2id[MELO_LANG]
    return next(iter(spk2id.values()))


def main():
    TTS = load_tts()
    if TTS is None:
        print("MeloTTS not installed. Try: pip install -U MeloTTS")
        return 2

    text = None
    if len(sys.argv) > 1:
        text = " ".join(sys.argv[1:]).strip()
    if not text:
        text = sys.stdin.read().strip()
    if not text:
        return 0

    os.makedirs(MELO_OUT_DIR, exist_ok=True)
    tts = TTS(language=MELO_LANG, device=MELO_DEVICE)
    speaker_id = pick_speaker(tts)

    ts = time.strftime("%Y%m%d_%H%M%S")
    out_path = os.path.join(MELO_OUT_DIR, "tts_%s.wav" % ts)
    tts.tts_to_file(text, speaker_id=speaker_id, output_path=out_path, speed=MELO_SPEED)
    print("TTS saved:", out_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
