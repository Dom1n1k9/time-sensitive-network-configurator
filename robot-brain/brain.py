#!/usr/bin/env python3
"""Robot brain - SPEECH OUTPUT daemon.

Text -> TTS -> WAV -> HTTP-served -> MQTT command that makes esp32-01 play it
on its MAX98357A speaker.

Run on the RPi/PC on the same LAN as the nodes:
    pip install paho-mqtt
    python3 brain.py

Then publish any text to MQTT topic `robot/say` and the robot speaks it, e.g.:
    mosquitto_pub -h 192.168.0.183 -u wtsn -P '<pass>' -t robot/say -m "Hi, I see a chair"

Environment:
    MQTT_HOST    default 192.168.0.183
    MQTT_USER    default wtsn
    MQTT_PASS    default empty (set it!)
    DEVICE_ID    default esp32-01        (node that has the speaker)
    BRAIN_IP     default auto (local IP toward the broker) - the host the ESP fetches the WAV from
    HTTP_PORT    default 8080
    SAY_TOPIC    default robot/say
    PIPER_MODEL  path to a piper *.onnx voice (optional, best quality)
TTS fallback chain: piper (if PIPER_MODEL + piper present) -> espeak-ng -> espeak.
"""
import os
import json
import socket
import shutil
import tempfile
import threading
import subprocess
import functools
import http.server

import paho.mqtt.client as mqtt

MQTT_HOST = os.environ.get("MQTT_HOST", "192.168.0.183")
MQTT_USER = os.environ.get("MQTT_USER", "wtsn")
MQTT_PASS = os.environ.get("MQTT_PASS", "")
DEVICE_ID = os.environ.get("DEVICE_ID", "esp32-01")
HTTP_PORT = int(os.environ.get("HTTP_PORT", "8080"))
SAY_TOPIC = os.environ.get("SAY_TOPIC", "robot/say")
PIPER_MODEL = os.environ.get("PIPER_MODEL", "")

AUDIO_DIR = tempfile.mkdtemp(prefix="robot_audio_")
WAV_NAME = "tts.wav"
WAV_PATH = os.path.join(AUDIO_DIR, WAV_NAME)


def local_ip():
    """Local IP used to reach the broker (the address the ESP will fetch from)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((MQTT_HOST, 1883))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


BRAIN_IP = os.environ.get("BRAIN_IP") or local_ip()


def tts(text, out_path):
    """Render `text` to a 16-bit PCM WAV at out_path. Returns True on success."""
    if PIPER_MODEL and shutil.which("piper"):
        try:
            subprocess.run(["piper", "--model", PIPER_MODEL, "--output_file", out_path],
                           input=text.encode("utf-8"), check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return True
        except (OSError, subprocess.CalledProcessError):
            pass
    for exe in ("espeak-ng", "espeak"):
        if shutil.which(exe):
            try:
                subprocess.run([exe, "-w", out_path, text], check=True,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return True
            except (OSError, subprocess.CalledProcessError):
                continue
    return False


def start_http_server():
    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **kw):
            super().__init__(*a, directory=AUDIO_DIR, **kw)

        def log_message(self, *a):
            pass

    httpd = http.server.ThreadingHTTPServer(("", HTTP_PORT), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()


def on_connect(client, userdata, flags, rc, properties=None):
    print(f"[brain] connected to {MQTT_HOST}:{rc}, subscribing {SAY_TOPIC}")
    client.subscribe(SAY_TOPIC, 0)


def on_message(client, userdata, msg):
    text = msg.payload.decode("utf-8", "replace").strip()
    if not text:
        return
    print(f"[brain] say: {text!r}")
    if not tts(text, WAV_PATH):
        print("[brain] no TTS found (install piper/espeak-ng)")
        return
    url = f"http://{BRAIN_IP}:{HTTP_PORT}/{WAV_NAME}"
    client.publish(f"tsn/cmd/{DEVICE_ID}/audio", json.dumps({"url": url}))
    print(f"[brain] -> audio {url}")


def main():
    print(f"[brain] HTTP {BRAIN_IP}:{HTTP_PORT} serving {AUDIO_DIR}")
    start_http_server()

    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    except AttributeError:            # paho-mqtt v1
        client = mqtt.Client()
    if MQTT_USER:
        client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(MQTT_HOST, 1883, 60)
    client.loop_forever()


if __name__ == "__main__":
    main()
