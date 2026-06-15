# IoT Voice Call System

## Overview

This project provides full-duplex voice communication between an IoT device and a PC-based AI service.

## Recommended Production Settings

```bash
./sk_live 192.168.1.2 5004 16000 24000 0 5 1
```

### Parameters

| Parameter | Value | Description |
|------------|---------|-------------|
| PC_IP | 192.168.1.2 | PC running senior_voice_call_service.py |
| PORT | 5004 | RTP/UDP port |
| SAMPLE_RATE | 16000 | Audio sample rate |
| BITRATE | 24000 | Opus bitrate |
| MIC_VOLUME | 0 | IoT microphone volume |
| SPEAKER_VOLUME | 5 | IoT speaker volume |
| SPEAKER_GAIN | 1 | Speaker gain |

---

## Running sk_live

```bash
./sk_live <PC_IP> <PORT> <SAMPLE_RATE> <BITRATE> <MIC_VOLUME> <SPEAKER_VOLUME> <SPEAKER_GAIN>
```

Example:

```bash
./sk_live 192.168.1.2 5004 16000 24000 0 5 1
```

---

## Running voice_call.sh

```bash
./voice_call.sh
```

Example script:

```bash
#!/bin/sh

PC_IP=192.168.1.2
PORT=5004

SAMPLE_RATE=16000
BITRATE=24000

MIC_VOLUME=0
SPEAKER_VOLUME=5
SPEAKER_GAIN=1

echo "Starting Voice Call Service..."

./sk_live     ${PC_IP}     ${PORT}     ${SAMPLE_RATE}     ${BITRATE}     ${MIC_VOLUME}     ${SPEAKER_VOLUME}     ${SPEAKER_GAIN}
```

---

## PC Side Startup

```bash
python3 senior_voice_call_service.py   --device-ip 192.168.1.8   --listen-port 5004   --send-port 5004   --whisper-model small   --language en   --piper-bin /home/soo/.local/bin/piper   --piper-model /home/soo/Work/Ollama/rpi/en_US-lessac-medium.onnx
```

---

## Voice Path

```text
IoT MIC
    ↓
Opus/RTP/UDP
    ↓
Whisper
    ↓
SeniorService + Llama3.2:1B
    ↓
Piper
    ↓
Opus/RTP/UDP
    ↓
IoT Speaker
```

---

## Console Text Path

```text
Console Text
    ↓
SeniorService + Llama3.2:1B
    ↓
Piper
    ↓
Opus/RTP/UDP
    ↓
IoT Speaker
```

---

## QUESTION_MODE

Commands:

```text
start query
check my condition
```

After 5 questions:

- Summary saved to `logs/condition_log.txt`
- Summary printed on console
- Voice only says:

```text
Condition check completed.
Summary saved to condition log.
Returning to chat mode.
```

---

## Echo Prevention

During TTS:

```text
Assistant speaking: mic listening OFF
```

After TTS:

```text
Assistant speaking finished: mic listening ON
```

---

## Stop Service

```bash
Ctrl+C
```
