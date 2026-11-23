#!/usr/bin/env python3
"""Robot vision - caption the ESP32-CAM, speak it, and show it on the OLED.

Grabs one JPEG from the camera (`http://<cam>/last.jpg`), asks a vision model for
a short description, then:
  * publishes it to `robot/say`  -> spoken by brain.py (esp32-01 speaker)
  * publishes it to `tsn/cmd/<dev>/oled` -> shown on the esp32-01 OLED

Run on the RPi/PC (same LAN):
    pip install requests paho-mqtt
    python3 caption.py           # loops every INTERVAL seconds

Environment:
    CAM_URL      default http://192.168.0.170/last.jpg   (SET to your CAM's IP)
    VLM_BACKEND  'ollama' (default) or 'openai'
    VLM_MODEL    default 'moondream' (ollama) | 'gpt-4o-mini' (openai)
    OLLAMA_URL   default http://localhost:11434
    OPENAI_BASE  default https://api.openai.com/v1   (+ OPENAI_API_KEY)
    MQTT_HOST/USER/PASS, DEVICE_ID (esp32-01), SAY_TOPIC (robot/say)
    OLED_TOPIC   default tsn/cmd/esp32-01/display
    INTERVAL     default 15 (seconds between captions)
    PROMPT       default "Describe the scene in one short sentence."
"""
import os
import io
import time
import json
import base64
import textwrap

import requests
import paho.mqtt.client as mqtt

CAM_URL = os.environ.get("CAM_URL", "http://192.168.0.170/last.jpg")
VLM_BACKEND = os.environ.get("VLM_BACKEND", "ollama").lower()
VLM_MODEL = os.environ.get("VLM_MODEL", "moondream" if VLM_BACKEND == "ollama" else "gpt-4o-mini")
OLLAMA_URL = os.environ.get("OLLAMA_URL", "http://localhost:11434")
OPENAI_BASE = os.environ.get("OPENAI_BASE", "https://api.openai.com/v1")
OPENAI_KEY = os.environ.get("OPENAI_API_KEY", "")
PROMPT = os.environ.get("PROMPT", "Describe the scene in one short sentence.")

MQTT_HOST = os.environ.get("MQTT_HOST", "192.168.0.183")
MQTT_USER = os.environ.get("MQTT_USER", "wtsn")
MQTT_PASS = os.environ.get("MQTT_PASS", "")
DEVICE_ID = os.environ.get("DEVICE_ID", "esp32-01")
SAY_TOPIC = os.environ.get("SAY_TOPIC", "robot/say")
OLED_TOPIC = os.environ.get("OLED_TOPIC", f"tsn/cmd/{DEVICE_ID}/display")
INTERVAL = float(os.environ.get("INTERVAL", "15"))


def grab_frame():
    r = requests.get(CAM_URL, timeout=6)
    r.raise_for_status()
    jpg = r.content
    if not jpg or jpg[0] != 0xFF or jpg[1] != 0xD8:
        raise ValueError("not a JPEG frame")
    return jpg


def caption_ollama(jpg_b64):
    r = requests.post(f"{OLLAMA_URL}/api/generate", timeout=120, json={
        "model": VLM_MODEL, "prompt": PROMPT, "images": [jpg_b64], "stream": False,
    })
    r.raise_for_status()
    return r.json().get("response", "").strip()


def caption_openai(jpg_b64):
    r = requests.post(f"{OPENAI_BASE}/chat/completions", timeout=60,
                      headers={"Authorization": f"Bearer {OPENAI_KEY}"},
                      json={"model": VLM_MODEL, "max_tokens": 100, "messages": [{
                          "role": "user",
                          "content": [
                              {"type": "text", "text": PROMPT},
                              {"type": "image_url",
                               "image_url": {"url": f"data:image/jpeg;base64,{jpg_b64}"}},
                          ],
                      }]})
    r.raise_for_status()
    return r.json()["choices"][0]["message"]["content"].strip()


def caption(jpg):
    b64 = base64.b64encode(jpg).decode()
    if VLM_BACKEND == "openai":
        return caption_openai(b64)
    return caption_ollama(b64)


def two_lines(text, width=16):
    text = text.strip()
    if len(text) <= width:
        parts = [text]
    else:
        parts = textwrap.wrap(text, width=width)
    l1 = parts[0] if parts else ""
    l2 = parts[1] if len(parts) > 1 else ""
    return l1[:width], l2[:width]


def main():
    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    except AttributeError:
        client = mqtt.Client()
    if MQTT_USER:
        client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.connect(MQTT_HOST, 1883, 60)
    client.loop_start()

    print(f"[caption] {CAM_URL} -> {VLM_BACKEND}:{VLM_MODEL}, every {INTERVAL}s")
    while True:
        try:
            jpg = grab_frame()
            text = caption(jpg)
            if text:
                print(f"[caption] {text}")
                client.publish(SAY_TOPIC, text)
                l1, l2 = two_lines(text)
                client.publish(OLED_TOPIC, json.dumps({"line1": l1, "line2": l2}))
        except Exception as e:                       # keep the loop alive
            print(f"[caption] error: {e}")
        time.sleep(INTERVAL)


if __name__ == "__main__":
    main()
