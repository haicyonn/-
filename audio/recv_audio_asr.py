import os
import socket
import time
import wave

import numpy as np
from funasr import AutoModel
HOST = "0.0.0.0"
PORT = 40000
SAMPLE_RATE = 16000
SAMPLE_BITS = 16
CHANNELS = 1
IDLE_CLOSE_SEC = 1.0
MAX_PACKET = 2048

MODEL_NAME = os.environ.get("FUNASR_STREAM_MODEL", "paraformer-zh-streaming")
MODEL_REV = os.environ.get("FUNASR_MODEL_REV", "v2.0.4")
PRINT_PARTIAL = False
OFFLINE_FINAL = os.environ.get("FUNASR_OFFLINE_FINAL", "1") != "0"
OFFLINE_MODEL = os.environ.get("FUNASR_OFFLINE_MODEL", "paraformer-zh")
OFFLINE_REV = os.environ.get("FUNASR_OFFLINE_REV", "v2.0.4")
VAD_MODEL = os.environ.get("FUNASR_VAD_MODEL", "fsmn-vad")
VAD_REV = os.environ.get("FUNASR_VAD_REV", "v2.0.4")
PUNC_MODEL = os.environ.get("FUNASR_PUNC_MODEL", "ct-punc-c")
PUNC_REV = os.environ.get("FUNASR_PUNC_REV", "v2.0.4")

CHUNK_SIZE = [0, 10, 5]  # 600ms
ENCODER_LOOK_BACK = 4
DECODER_LOOK_BACK = 1
CHUNK_STRIDE = CHUNK_SIZE[1] * 960  # 16k * 0.6s
BYTES_PER_SAMPLE = SAMPLE_BITS // 8
BYTES_PER_CHUNK = CHUNK_STRIDE * BYTES_PER_SAMPLE

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
TEXT_OUT_HOST = os.environ.get("ASR_TEXT_HOST", "127.0.0.1")
TEXT_OUT_PORT = int(os.environ.get("ASR_TEXT_PORT", "41000"))


def open_wav():
    ts = time.strftime("%Y%m%d_%H%M%S")
    out_path = os.path.join(OUT_DIR, "audio_%s.wav" % ts)
    wav = wave.open(out_path, "wb")
    wav.setnchannels(CHANNELS)
    wav.setsampwidth(BYTES_PER_SAMPLE)
    wav.setframerate(SAMPLE_RATE)
    return wav, out_path


def extract_text(res):
    if not res:
        return ""
    if isinstance(res, list) and len(res) > 0 and isinstance(res[0], dict):
        return res[0].get("text") or ""
    return ""


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    print("Loading FunASR model: %s" % MODEL_NAME)
    model = AutoModel(model=MODEL_NAME, model_revision=MODEL_REV, disable_update=True)
    offline_model = None
    if OFFLINE_FINAL:
        print("Loading FunASR offline model: %s" % OFFLINE_MODEL)
        offline_model = AutoModel(
            model=OFFLINE_MODEL,
            model_revision=OFFLINE_REV,
            vad_model=VAD_MODEL,
            vad_model_revision=VAD_REV,
            punc_model=PUNC_MODEL,
            punc_model_revision=PUNC_REV,
            disable_update=True,
        )

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((HOST, PORT))
    sock.settimeout(0.5)
    print("Listening UDP on %s:%d" % (HOST, PORT))
    text_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    wav = None
    out_path = None
    last_rx = 0.0
    total = 0
    pcm_buf = bytearray()
    cache = {}
    last_text = ""
    pending_text = ""

    def send_text(text, tag):
        payload = ("%s:%s" % (tag, text)).encode("utf-8", errors="ignore")
        try:
            text_sock.sendto(payload, (TEXT_OUT_HOST, TEXT_OUT_PORT))
        except Exception as exc:
            print("ASR text send failed:", exc)

    def process_chunk(is_final):
        nonlocal pcm_buf, cache, last_text, pending_text
        if not pcm_buf and not is_final:
            return
        if pcm_buf:
            chunk = np.frombuffer(pcm_buf, dtype=np.int16).astype(np.float32) / 32768.0
        else:
            chunk = np.zeros(0, dtype=np.float32)
        pcm_buf = bytearray()
        res = model.generate(
            input=chunk,
            cache=cache,
            is_final=is_final,
            chunk_size=CHUNK_SIZE,
            encoder_chunk_look_back=ENCODER_LOOK_BACK,
            decoder_chunk_look_back=DECODER_LOOK_BACK,
        )
        text = extract_text(res)
        if text:
            last_text = text
            if PRINT_PARTIAL and not is_final:
                print("ASR (partial):", text)
        if is_final:
            final_text = last_text
            if final_text:
                print("ASR (final):", final_text)
                pending_text = final_text
            cache = {}
            last_text = ""

    while True:
        try:
            data, addr = sock.recvfrom(MAX_PACKET)
        except socket.timeout:
            if wav is not None and (time.time() - last_rx) > IDLE_CLOSE_SEC:
                process_chunk(is_final=True)
                wav.close()
                print("Saved %d bytes to %s" % (total, out_path))
                final_text = ""
                if offline_model is not None:
                    try:
                        res = offline_model.generate(input=out_path, batch_size_s=300)
                        final_text = extract_text(res)
                        if final_text:
                            print("ASR (offline final):", final_text)
                    except Exception as exc:
                        print("ASR (offline) failed:", exc)
                    if final_text:
                        send_text(final_text, "OFFLINE")
                    elif pending_text:
                        send_text(pending_text, "ASR")
                elif pending_text:
                    send_text(pending_text, "ASR")
                pending_text = ""
                wav = None
                out_path = None
                total = 0
            continue

        if not data:
            continue

        if wav is None:
            wav, out_path = open_wav()
            total = 0
            pcm_buf = bytearray()
            cache = {}
            last_text = ""
            print("Streaming from %s -> %s" % (addr, out_path))

        wav.writeframesraw(data)
        total += len(data)
        last_rx = time.time()

        pcm_buf.extend(data)
        while len(pcm_buf) >= BYTES_PER_CHUNK:
            chunk_bytes = pcm_buf[:BYTES_PER_CHUNK]
            pcm_buf = pcm_buf[BYTES_PER_CHUNK:]
            chunk = np.frombuffer(chunk_bytes, dtype=np.int16).astype(np.float32) / 32768.0
            res = model.generate(
                input=chunk,
                cache=cache,
                is_final=False,
                chunk_size=CHUNK_SIZE,
                encoder_chunk_look_back=ENCODER_LOOK_BACK,
                decoder_chunk_look_back=DECODER_LOOK_BACK,
            )
            text = extract_text(res)
            if text:
                last_text = text
                if PRINT_PARTIAL:
                    print("ASR (partial):", text)


if __name__ == "__main__":
    main()
