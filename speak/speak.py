#!/usr/bin/env python3
"""
speak.py

PC-side text-to-speech sender for SigmaStar IoT speaker.

Flow:
    Console text
      -> Piper TTS
      -> WAV/PCM
      -> convert to 16 kHz mono 16-bit PCM
      -> Opus encode
      -> RTP/UDP
      -> IoT sk_live / sk_voice_call_main
      -> Opus decode
      -> speaker

Expected IoT side:
    ./sk_live <PC_IP> 5004 16000 24000 10 5 1

Install:
    pip install opuslib

System:
    sudo apt install ffmpeg

Run:
    cd speak
    python3 speak.py --device-ip 192.168.1.8 --piper-model ../ko_KR-kss.onnx

Example text:
    안녕하세요. 테스트 음성입니다.
"""

import argparse
import os
import signal
import socket
import struct
import subprocess
import tempfile
import time
import wave
from pathlib import Path

try:
    import opuslib
except ImportError:
    opuslib = None


RTP_PAYLOAD_TYPE_OPUS = 96
RTP_CLOCK_RATE = 48000

SAMPLE_RATE = 16000
CHANNELS = 1
FRAME_MS = 20
BITRATE = 24000
PCM_WIDTH_BYTES = 2


def build_rtp_packet(payload: bytes, seq: int, timestamp: int, ssrc: int, marker: bool = False) -> bytes:
    b0 = 0x80
    b1 = (0x80 if marker else 0x00) | (RTP_PAYLOAD_TYPE_OPUS & 0x7F)
    return struct.pack("!BBHII", b0, b1, seq & 0xFFFF, timestamp & 0xFFFFFFFF, ssrc & 0xFFFFFFFF) + payload


def wav_to_pcm16_mono(wav_path: str, target_sample_rate: int = SAMPLE_RATE) -> bytes:
    """
    Return raw s16le mono PCM at target_sample_rate.
    Uses ffmpeg when Piper WAV format is not already correct.
    """
    with wave.open(wav_path, "rb") as wf:
        channels = wf.getnchannels()
        sample_rate = wf.getframerate()
        sampwidth = wf.getsampwidth()

    if channels == 1 and sample_rate == target_sample_rate and sampwidth == 2:
        with wave.open(wav_path, "rb") as wf:
            return wf.readframes(wf.getnframes())

    with tempfile.TemporaryDirectory() as td:
        out_pcm = Path(td) / "out.pcm"
        cmd = [
            "ffmpeg",
            "-y",
            "-i",
            wav_path,
            "-ac",
            "1",
            "-ar",
            str(target_sample_rate),
            "-f",
            "s16le",
            str(out_pcm),
        ]
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr.decode("utf-8", errors="ignore"))
        return out_pcm.read_bytes()


class PiperTTS:
    def __init__(self, piper_bin: str, model_path: str):
        self.piper_bin = piper_bin
        self.model_path = model_path

        if not Path(self.piper_bin).exists():
            raise FileNotFoundError(f"Piper binary not found: {self.piper_bin}")
        if not Path(self.model_path).exists():
            raise FileNotFoundError(f"Piper model not found: {self.model_path}")

    def synthesize_pcm(self, text: str) -> bytes:
        with tempfile.TemporaryDirectory() as td:
            wav_path = Path(td) / "tts.wav"

            cmd = [
                self.piper_bin,
                "--model",
                self.model_path,
                "--output_file",
                str(wav_path),
            ]

            proc = subprocess.run(
                cmd,
                input=text.encode("utf-8"),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )

            if proc.returncode != 0:
                raise RuntimeError("Piper failed:\n" + proc.stderr.decode("utf-8", errors="ignore"))

            return wav_to_pcm16_mono(str(wav_path), SAMPLE_RATE)


class RTPVoiceSender:
    def __init__(self, device_ip: str, port: int, bitrate: int):
        if opuslib is None:
            raise RuntimeError("opuslib is not installed. Run: pip install opuslib")

        self.device_addr = (device_ip, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        self.frame_samples = SAMPLE_RATE * FRAME_MS // 1000
        self.frame_bytes = self.frame_samples * CHANNELS * PCM_WIDTH_BYTES
        self.rtp_ts_step = RTP_CLOCK_RATE * FRAME_MS // 1000

        self.encoder = opuslib.Encoder(SAMPLE_RATE, CHANNELS, opuslib.APPLICATION_VOIP)
        self.encoder.bitrate = bitrate

        self.seq = 0
        self.timestamp = 0
        self.ssrc = int(time.time()) ^ os.getpid() ^ 0x53504B31

    def send_pcm(self, pcm: bytes):
        if len(pcm) % self.frame_bytes:
            pcm += b"\x00" * (self.frame_bytes - (len(pcm) % self.frame_bytes))

        total_frames = len(pcm) // self.frame_bytes
        print(f"Sending {total_frames} RTP/Opus frames to {self.device_addr[0]}:{self.device_addr[1]}")

        for off in range(0, len(pcm), self.frame_bytes):
            frame = pcm[off:off + self.frame_bytes]
            payload = self.encoder.encode(frame, self.frame_samples)

            pkt = build_rtp_packet(
                payload=payload,
                seq=self.seq,
                timestamp=self.timestamp,
                ssrc=self.ssrc,
                marker=(self.seq == 0),
            )

            self.sock.sendto(pkt, self.device_addr)

            self.seq = (self.seq + 1) & 0xFFFF
            self.timestamp = (self.timestamp + self.rtp_ts_step) & 0xFFFFFFFF

            # Real-time pacing for stable speaker playback.
            time.sleep(FRAME_MS / 1000.0)


def parse_args():
    parser = argparse.ArgumentParser(description="Console text -> Piper TTS -> RTP/Opus -> IoT speaker")

    parser.add_argument("--device-ip", required=True, help="IoT device IP address")
    parser.add_argument("--port", type=int, default=5004, help="IoT UDP/RTP receive port")
    parser.add_argument("--piper-bin", default="/snap/bin/piper-tts.piper-cli")
    parser.add_argument("--piper-model", required=True, help="Path to Piper .onnx voice model")
    parser.add_argument("--bitrate", type=int, default=BITRATE)

    return parser.parse_args()


def main():
    args = parse_args()

    running = True

    def on_signal(_signum, _frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    tts = PiperTTS(args.piper_bin, args.piper_model)
    sender = RTPVoiceSender(args.device_ip, args.port, args.bitrate)

    print(f"Sending speech to IoT {args.device_ip}:{args.port}")
    print("Type text and press Enter. Type 'exit' or 'quit' to stop.\n")

    while running:
        try:
            text = input("Text> ").strip()
        except EOFError:
            break
        except KeyboardInterrupt:
            break

        if not text:
            continue

        if text.lower() in ("exit", "quit"):
            break

        try:
            pcm = tts.synthesize_pcm(text)
            sender.send_pcm(pcm)
            print("Done.\n")
        except Exception as e:
            print(f"Error: {e}\n")

    print("speak.py stopped")


if __name__ == "__main__":
    main()
