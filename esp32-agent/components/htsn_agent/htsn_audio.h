/* I2S speaker output for esp32-01 (MAX98357A digital amp).
 *
 * Wiring (esp32-01 free pins):
 *   MAX98357A BCLK -> GPIO23
 *   MAX98357A LRC  -> GPIO4   (LRCLK / WS)
 *   MAX98357A DIN  -> GPIO5
 *   VIN -> 5V, GND -> common GND, SD/GAIN per module
 *
 * The audio itself is generated off-board (the RPi/PC "brain" runs the LLM +
 * TTS and serves a short WAV over HTTP). esp32-01 just plays it:
 *
 *   MQTT  tsn/cmd/esp32-01/audio   body: {"url":"http://<brain>:8080/tts.wav"}
 *   MQTT  tsn/cmd/esp32-01/tone    body: (any) -> plays a 880 Hz test tone
 *
 * Playback runs in its own task, so the MQTT task never blocks on audio.
 */
#ifndef HTSN_AUDIO_H
#define HTSN_AUDIO_H

#include <stdbool.h>

/* Configure the I2S TX peripheral (call once on esp32-01). */
void htsn_audio_init(void);

/* True once the I2S channel is up. */
bool htsn_audio_ready(void);

/* Play a short test tone (blocking-ish; queued task). Used to verify wiring. */
void htsn_audio_tone(int freq_hz, int ms);

/* Download a WAV (16-bit PCM) from `url` and play it. Non-blocking (queued). */
void htsn_audio_play_url(const char *url);

#endif /* HTSN_AUDIO_H */
