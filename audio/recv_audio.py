import os
import socket
import time
import wave

HOST = "0.0.0.0"
PORT = 40000
SAMPLE_RATE = 16000
SAMPLE_BITS = 16
CHANNELS = 1
IDLE_CLOSE_SEC = 1.0
MAX_PACKET = 2048

OUT_DIR = os.path.dirname(os.path.abspath(__file__))


def open_wav():
    ts = time.strftime("%Y%m%d_%H%M%S")
    out_path = os.path.join(OUT_DIR, "audio_%s.wav" % ts)
    wav = wave.open(out_path, "wb")
    wav.setnchannels(CHANNELS)
    wav.setsampwidth(SAMPLE_BITS // 8)
    wav.setframerate(SAMPLE_RATE)
    return wav, out_path


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((HOST, PORT))
    sock.settimeout(0.5)
    print("Listening UDP on %s:%d" % (HOST, PORT))

    wav = None
    out_path = None
    last_rx = 0.0
    total = 0

    while True:
        try:
            data, addr = sock.recvfrom(MAX_PACKET)
        except socket.timeout:
            if wav is not None and (time.time() - last_rx) > IDLE_CLOSE_SEC:
                wav.close()
                print("Saved %d bytes to %s" % (total, out_path))
                wav = None
                out_path = None
                total = 0
            continue

        if not data:
            continue

        if wav is None:
            wav, out_path = open_wav()
            total = 0
            print("Streaming from %s -> %s" % (addr, out_path))

        wav.writeframesraw(data)
        total += len(data)
        last_rx = time.time()


if __name__ == "__main__":
    main()
