import os
import socket
import subprocess
import sys
from datetime import datetime

import requests

TEXT_IN_HOST = os.environ.get("ASR_TEXT_HOST", "127.0.0.1")
TEXT_IN_PORT = int(os.environ.get("ASR_TEXT_PORT", "41000"))

GEMINI_API_KEY = os.environ.get("GEMINI_API_KEY", "").strip()
GEMINI_MODEL = os.environ.get("GEMINI_MODEL", "gemini-3-flash-preview")
GEMINI_TIMEOUT_SEC = float(os.environ.get("GEMINI_TIMEOUT", "20"))
GEMINI_ENABLE = os.environ.get("GEMINI_ENABLE", "1") != "0"
# Static reply constraints (edit here if you want different behavior).
GEMINI_SYSTEM_PROMPT = "回答要简短，最多50字，不要使用括号，不要出现编号。"
GEMINI_MAX_CHARS = 20
GEMINI_STRIP_PARENS = True
LOCAL_TIME_ENABLE = os.environ.get("LOCAL_TIME_ENABLE", "1") != "0"
TTS_ENABLE = os.environ.get("TTS_ENABLE", "0") != "0"
TTS_SCRIPT = os.path.join(os.path.dirname(__file__), "tts_melo.py")


def apply_reply_constraints(text):
    if not text:
        return text
    if GEMINI_STRIP_PARENS:
        text = text.replace("(", "").replace(")", "").replace("\uFF08", "").replace("\uFF09", "")
    if GEMINI_MAX_CHARS > 0 and len(text) > GEMINI_MAX_CHARS:
        text = text[:GEMINI_MAX_CHARS]
    return text.strip()


def call_gemini(prompt):
    if not GEMINI_ENABLE:
        return None
    if not GEMINI_API_KEY:
        print("Gemini disabled: set GEMINI_API_KEY to enable.")
        return None
    if GEMINI_SYSTEM_PROMPT:
        prompt = "%s\n\u7528\u6237\u95EE\u9898: %s" % (GEMINI_SYSTEM_PROMPT, prompt)
    url = "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent" % GEMINI_MODEL
    payload = {
        "contents": [
            {
                "role": "user",
                "parts": [{"text": prompt}],
            }
        ]
    }
    try:
        resp = requests.post(url, params={"key": GEMINI_API_KEY}, json=payload, timeout=GEMINI_TIMEOUT_SEC)
    except Exception as exc:
        print("Gemini request failed:", exc)
        return None

    if resp.status_code != 200:
        print("Gemini HTTP error:", resp.status_code, resp.text)
        return None

    try:
        data = resp.json()
        text = data["candidates"][0]["content"]["parts"][0]["text"]
        return apply_reply_constraints(text)
    except Exception as exc:
        print("Gemini parse failed:", exc)
        return None


def local_time_answer(text):
    if not LOCAL_TIME_ENABLE:
        return None
    q = text.strip().lower()
    if not q:
        return None

    has_weekday = any(k in q for k in [
        "\u661f\u671f",  # 星期
        "\u5468\u51e0",  # 周几
        "\u661f\u671f\u51e0",  # 星期几
    ])
    has_date = any(k in q for k in [
        "\u51e0\u53f7",  # 几号
        "\u65e5\u671f",  # 日期
        "\u51e0\u6708",  # 几月
        "\u51e0\u65e5",  # 几日
    ])
    has_time = any(k in q for k in [
        "\u51e0\u70b9",  # 几点
        "\u65f6\u95f4",  # 时间
    ])

    if any(k in q for k in ["weekday", "what day", "day of week"]):
        has_weekday = True
    if any(k in q for k in ["date", "what date"]):
        has_date = True
    if any(k in q for k in ["time", "what time", "clock"]):
        has_time = True

    if not (has_weekday or has_date or has_time):
        return None

    now = datetime.now()
    weekday_map = ["\u4e00", "\u4e8c", "\u4e09", "\u56db", "\u4e94", "\u516d", "\u65e5"]
    weekday_str = "\u661f\u671f" + weekday_map[now.weekday()]
    date_str = "%d\u5e74%d\u6708%d\u65e5" % (now.year, now.month, now.day)
    time_str = "%02d:%02d:%02d" % (now.hour, now.minute, now.second)

    if has_date and has_weekday and has_time:
        return apply_reply_constraints(
            "\u4eca\u5929\u662f%s%s\uFF0C\u73B0\u5728\u662F%s" % (date_str, weekday_str, time_str)
        )
    if has_date and has_weekday:
        return apply_reply_constraints("\u4eca\u5929\u662f%s%s" % (date_str, weekday_str))
    if has_date and has_time:
        return apply_reply_constraints("\u4eca\u5929\u662f%s\uFF0C\u73B0\u5728\u662F%s" % (date_str, time_str))
    if has_weekday and has_time:
        return apply_reply_constraints("\u4eca\u5929\u662f%s\uFF0C\u73B0\u5728\u662F%s" % (weekday_str, time_str))
    if has_weekday:
        return apply_reply_constraints("\u4eca\u5929\u662f%s" % weekday_str)
    if has_date:
        return apply_reply_constraints("\u4eca\u5929\u662f%s" % date_str)
    return apply_reply_constraints("\u73B0\u5728\u662F%s" % time_str)


def maybe_tts(text):
    if not TTS_ENABLE:
        return
    if not os.path.exists(TTS_SCRIPT):
        print("TTS disabled: tts_melo.py not found")
        return
    try:
        subprocess.run(
            [sys.executable, TTS_SCRIPT],
            input=text.encode("utf-8"),
            check=True,
        )
    except Exception as exc:
        print("TTS failed:", exc)


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((TEXT_IN_HOST, TEXT_IN_PORT))
    print("Listening ASR text on %s:%d" % (TEXT_IN_HOST, TEXT_IN_PORT))

    while True:
        data, addr = sock.recvfrom(8192)
        if not data:
            continue
        msg = data.decode("utf-8", errors="ignore")
        tag = ""
        text = msg
        if ":" in msg:
            tag, text = msg.split(":", 1)
        label = tag or "ASR"
        text = text.strip()
        if not text:
            continue
        print("%s: %s" % (label, text))
        local_reply = local_time_answer(text)
        if local_reply:
            print("Local:", local_reply)
            maybe_tts(local_reply)
            continue
        reply = call_gemini(text)
        if reply:
            print("Gemini:", reply)
            maybe_tts(reply)


if __name__ == "__main__":
    main()
