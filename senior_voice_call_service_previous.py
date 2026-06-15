#!/usr/bin/env python3
"""
senior_voice_call_service.py

Integrated PC-side AI voice-call service.

This file DOES NOT modify senior_service.py.
It imports SeniorService and uses it as-is.

Inputs:
1. IoT MIC input:
   IoT MIC -> RTP/Opus/UDP -> Opus decode -> PCM -> Whisper STT -> text

2. Console keyboard input:
   User types text in terminal -> text

Both inputs use the same reasoning/speaking path:
   text
     -> SeniorService.fetch_and_check()
     -> SeniorService.chat_response(text)
     -> Piper TTS
     -> PCM 16 kHz mono
     -> Opus encode
     -> RTP/UDP
     -> IoT speaker

Echo prevention:
- While Piper/TTS is speaking, IoT MIC listening is ignored.
- After speaking finishes, the service waits briefly, clears VAD state, then listens again.

Expected IoT side:
    ./sk_live <PC_IP> 5004 16000 24000 10 5 1

Install:
    pip install requests numpy opuslib faster-whisper

System:
    sudo apt install ffmpeg

Run example:
    python3 senior_voice_call_service.py \
      --device-ip 192.168.1.8 \
      --listen-port 5004 \
      --send-port 5004 \
      --whisper-model small \
      --language en \
      --piper-bin /home/soo/.local/bin/piper \
      --piper-model /home/soo/Work/Ollama/rpi/en_US-lessac-medium.onnx
"""

import argparse
import audioop
import os
import queue
import signal
import socket
import struct
import subprocess
import tempfile
import threading
import time
import wave
from pathlib import Path
from typing import Optional, Tuple

import numpy as np

try:
    import opuslib
except ImportError:
    opuslib = None

try:
    from faster_whisper import WhisperModel
except ImportError:
    WhisperModel = None

try:
    from senior_service import SeniorService
except Exception as e:
    SeniorService = None
    SENIOR_IMPORT_ERROR = e
else:
    SENIOR_IMPORT_ERROR = None


RTP_VERSION = 2
RTP_PAYLOAD_TYPE_OPUS = 96
RTP_CLOCK_RATE = 48000

SAMPLE_RATE = 16000
CHANNELS = 1
FRAME_MS = 20
PCM_WIDTH_BYTES = 2
MAX_UDP_PACKET = 2048


def parse_rtp_packet(packet: bytes) -> Optional[Tuple[int, int, int, bytes]]:
    if len(packet) < 12:
        return None

    b0, b1, seq, timestamp, ssrc = struct.unpack("!BBHII", packet[:12])

    if (b0 >> 6) != RTP_VERSION:
        return None

    if (b1 & 0x7F) != RTP_PAYLOAD_TYPE_OPUS:
        return None

    cc = b0 & 0x0F
    header_len = 12 + cc * 4

    if len(packet) < header_len:
        return None

    if b0 & 0x10:
        if len(packet) < header_len + 4:
            return None
        ext_words = struct.unpack("!H", packet[header_len + 2:header_len + 4])[0]
        header_len += 4 + ext_words * 4
        if len(packet) < header_len:
            return None

    payload = packet[header_len:]
    if not payload:
        return None

    return seq, timestamp, ssrc, payload


def build_rtp_packet(payload: bytes, seq: int, timestamp: int, ssrc: int, marker: bool = False) -> bytes:
    b0 = 0x80
    b1 = (0x80 if marker else 0x00) | (RTP_PAYLOAD_TYPE_OPUS & 0x7F)
    return struct.pack(
        "!BBHII",
        b0,
        b1,
        seq & 0xFFFF,
        timestamp & 0xFFFFFFFF,
        ssrc & 0xFFFFFFFF,
    ) + payload


class OpusCodec:
    def __init__(self, bitrate: int):
        if opuslib is None:
            raise RuntimeError("opuslib is not installed. Run: pip install opuslib")

        self.frame_samples = SAMPLE_RATE * FRAME_MS // 1000
        self.frame_bytes = self.frame_samples * CHANNELS * PCM_WIDTH_BYTES

        self.encoder = opuslib.Encoder(SAMPLE_RATE, CHANNELS, opuslib.APPLICATION_VOIP)
        self.encoder.bitrate = bitrate

        self.decoder = opuslib.Decoder(SAMPLE_RATE, CHANNELS)

    def decode(self, payload: bytes) -> bytes:
        return self.decoder.decode(payload, self.frame_samples)

    def encode(self, pcm_frame: bytes) -> bytes:
        return self.encoder.encode(pcm_frame, self.frame_samples)


class SpeechSegmenter:
    def __init__(
        self,
        start_rms: int,
        stop_rms: int,
        silence_ms: int,
        preroll_ms: int,
        max_segment_sec: float,
    ):
        self.start_rms = start_rms
        self.stop_rms = stop_rms
        self.silence_frames_needed = max(1, silence_ms // FRAME_MS)
        self.preroll_frames = max(1, preroll_ms // FRAME_MS)
        self.max_frames = int(max_segment_sec * 1000 // FRAME_MS)

        self.in_speech = False
        self.silence_count = 0
        self.frames = []
        self.preroll = []

    def reset(self):
        self.in_speech = False
        self.silence_count = 0
        self.frames = []
        self.preroll = []

    def push(self, pcm: bytes) -> Optional[bytes]:
        rms = audioop.rms(pcm, PCM_WIDTH_BYTES)

        if not self.in_speech:
            self.preroll.append(pcm)
            if len(self.preroll) > self.preroll_frames:
                self.preroll.pop(0)

            if rms >= self.start_rms:
                self.in_speech = True
                self.silence_count = 0
                self.frames = list(self.preroll)
                print(f"speech start rms={rms}", flush=True)

            return None

        self.frames.append(pcm)

        if rms < self.stop_rms:
            self.silence_count += 1
        else:
            self.silence_count = 0

        if self.silence_count >= self.silence_frames_needed or len(self.frames) >= self.max_frames:
            segment = b"".join(self.frames)
            duration = len(segment) / (SAMPLE_RATE * PCM_WIDTH_BYTES)
            print(f"speech end duration={duration:.2f}s", flush=True)

            self.reset()
            return segment

        return None


class WhisperSTT:
    def __init__(self, model_name: str, device: str, compute_type: str):
        if WhisperModel is None:
            raise RuntimeError("faster-whisper is not installed. Run: pip install faster-whisper")

        print(f"Loading Whisper model: {model_name} device={device} compute_type={compute_type}", flush=True)
        self.model = WhisperModel(model_name, device=device, compute_type=compute_type)

    def transcribe_pcm(self, pcm: bytes, language: Optional[str]) -> str:
        audio = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32768.0

        segments, _info = self.model.transcribe(
            audio,
            language=language,
            beam_size=1,
            vad_filter=True,
            condition_on_previous_text=False,
        )

        return " ".join(seg.text.strip() for seg in segments).strip()


def wav_to_pcm16_mono(wav_path: str) -> bytes:
    with wave.open(wav_path, "rb") as wf:
        channels = wf.getnchannels()
        sample_rate = wf.getframerate()
        sampwidth = wf.getsampwidth()

    if channels == 1 and sample_rate == SAMPLE_RATE and sampwidth == 2:
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
            str(SAMPLE_RATE),
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

            return wav_to_pcm16_mono(str(wav_path))


class SeniorVoiceCallService:
    def __init__(self, args):
        self.args = args
        self.stop = False

        if SeniorService is None:
            raise RuntimeError(f"Could not import senior_service.py: {SENIOR_IMPORT_ERROR}")

        self.device_addr = (args.device_ip, args.send_port)

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", args.listen_port))
        self.sock.settimeout(0.2)

        self.codec = OpusCodec(args.bitrate)

        self.segmenter = SpeechSegmenter(
            start_rms=args.vad_start_rms,
            stop_rms=args.vad_stop_rms,
            silence_ms=args.vad_silence_ms,
            preroll_ms=args.vad_preroll_ms,
            max_segment_sec=args.max_segment_sec,
        )

        self.whisper = WhisperSTT(
            model_name=args.whisper_model,
            device=args.whisper_device,
            compute_type=args.whisper_compute_type,
        )

        self.piper = PiperTTS(args.piper_bin, args.piper_model)
        self.senior = SeniorService()

        # Whisper-created PCM segments.
        self.segment_queue = queue.Queue()

        # Text inputs from either IoT mic/Whisper or keyboard console.
        self.text_queue = queue.Queue()

        # Llama answer text to be spoken by Piper.
        self.answer_queue = queue.Queue()

        self.packet_count = 0
        self.ignored_packet_count = 0
        self.last_stat_time = time.time()

        self.send_seq = 0
        self.send_timestamp = 0
        self.send_ssrc = int(time.time()) ^ os.getpid() ^ 0x534B5643
        self.rtp_ts_step = RTP_CLOCK_RATE * FRAME_MS // 1000

        # Echo prevention flag.
        self.is_speaking = False
        self.speaking_lock = threading.Lock()

    def set_speaking(self, value: bool):
        with self.speaking_lock:
            self.is_speaking = value

    def get_speaking(self) -> bool:
        with self.speaking_lock:
            return self.is_speaking

    def start(self):
        print(f"Senior voice-call service listening on UDP {self.args.listen_port}", flush=True)
        print(f"Sending TTS RTP/Opus to IoT {self.device_addr[0]}:{self.device_addr[1]}", flush=True)
        print("Echo prevention: listening is OFF while Piper/TTS is speaking.", flush=True)
        print("Inputs:", flush=True)
        print("  1. Speak into IoT microphone", flush=True)
        print("  2. Type text in this console and press Enter", flush=True)
        print("Type 'exit' or 'quit' in console to stop.\n", flush=True)

        try:
            print("Loading initial sensor data through senior_service.py...", flush=True)
            self.senior.fetch_and_check()
            print("Sensor data loaded.\n", flush=True)
        except Exception as e:
            print(f"Warning: initial sensor fetch failed: {e}", flush=True)

        threads = [
            threading.Thread(target=self.receive_loop, daemon=True),
            threading.Thread(target=self.whisper_loop, daemon=True),
            threading.Thread(target=self.console_input_loop, daemon=True),
            threading.Thread(target=self.text_reasoning_loop, daemon=True),
            threading.Thread(target=self.speak_loop, daemon=True),
        ]

        for t in threads:
            t.start()

        try:
            while not self.stop:
                time.sleep(0.2)
        except KeyboardInterrupt:
            self.stop = True

        self.sock.close()
        print("\nsenior_voice_call_service.py stopped", flush=True)

    def receive_loop(self):
        while not self.stop:
            try:
                packet, _addr = self.sock.recvfrom(MAX_UDP_PACKET)
            except socket.timeout:
                self.print_stats()
                continue
            except OSError:
                break

            parsed = parse_rtp_packet(packet)
            if parsed is None:
                continue

            self.packet_count += 1

            # Ignore IoT mic while assistant is speaking.
            if self.get_speaking():
                self.ignored_packet_count += 1
                continue

            _seq, _timestamp, _ssrc, opus_payload = parsed

            try:
                pcm = self.codec.decode(opus_payload)
            except Exception as e:
                print(f"Opus decode error: {e}", flush=True)
                continue

            segment = self.segmenter.push(pcm)
            if segment:
                self.segment_queue.put(segment)

            self.print_stats()

    def print_stats(self):
        now = time.time()
        if self.args.show_packet_stats and now - self.last_stat_time >= 3.0:
            print(
                f"received RTP packets={self.packet_count}, ignored_while_speaking={self.ignored_packet_count}",
                flush=True,
            )
            self.last_stat_time = now

    def whisper_loop(self):
        while not self.stop:
            try:
                segment = self.segment_queue.get(timeout=0.2)
            except queue.Empty:
                continue

            if self.get_speaking():
                continue

            try:
                user_text = self.whisper.transcribe_pcm(segment, self.args.language)
            except Exception as e:
                print(f"Whisper error: {e}", flush=True)
                continue

            if not user_text:
                print("Whisper text: <empty>", flush=True)
                continue

            print(f"\nIoT MIC said: {user_text}", flush=True)
            self.text_queue.put(("iot_mic", user_text))

    def console_input_loop(self):
        while not self.stop:
            try:
                text = input("Console Text> ").strip()
            except EOFError:
                self.stop = True
                break
            except KeyboardInterrupt:
                self.stop = True
                break

            if not text:
                continue

            if text.lower() in ("exit", "quit"):
                self.stop = True
                break

            print(f"Console typed: {text}", flush=True)
            self.text_queue.put(("console", text))

    def text_reasoning_loop(self):
        while not self.stop:
            try:
                source, user_text = self.text_queue.get(timeout=0.2)
            except queue.Empty:
                continue

            print(f"\nProcessing input from {source}: {user_text}", flush=True)

            try:
                self.senior.fetch_and_check()
            except Exception as e:
                print(f"Warning: sensor refresh failed: {e}", flush=True)

            try:
                answer = self.senior.chat_response(user_text)
            except Exception as e:
                print(f"SeniorService/Llama error: {e}", flush=True)
                answer = "Sorry, I could not generate an answer."

            answer = answer.strip()
            print(f"Assistant: {answer}\n", flush=True)

            if answer:
                self.answer_queue.put(answer)

    def speak_loop(self):
        while not self.stop:
            try:
                answer = self.answer_queue.get(timeout=0.2)
            except queue.Empty:
                continue

            try:
                pcm = self.piper.synthesize_pcm(answer)
            except Exception as e:
                print(f"Piper error: {e}", flush=True)
                continue

            try:
                self.send_pcm_as_rtp_opus(pcm)
            except Exception as e:
                print(f"RTP send error: {e}", flush=True)

    def send_pcm_as_rtp_opus(self, pcm: bytes):
        frame_bytes = self.codec.frame_bytes

        if len(pcm) % frame_bytes:
            pcm += b"\x00" * (frame_bytes - (len(pcm) % frame_bytes))

        total_frames = len(pcm) // frame_bytes

        print("Assistant speaking: mic listening OFF", flush=True)
        self.set_speaking(True)

        try:
            print(f"Sending TTS audio: {total_frames} RTP/Opus frames", flush=True)

            for off in range(0, len(pcm), frame_bytes):
                if self.stop:
                    break

                frame = pcm[off:off + frame_bytes]
                payload = self.codec.encode(frame)

                pkt = build_rtp_packet(
                    payload=payload,
                    seq=self.send_seq,
                    timestamp=self.send_timestamp,
                    ssrc=self.send_ssrc,
                    marker=(self.send_seq == 0),
                )

                self.sock.sendto(pkt, self.device_addr)

                self.send_seq = (self.send_seq + 1) & 0xFFFF
                self.send_timestamp = (self.send_timestamp + self.rtp_ts_step) & 0xFFFFFFFF

                time.sleep(FRAME_MS / 1000.0)

            time.sleep(self.args.listen_resume_delay)
            self.segmenter.reset()

        finally:
            self.set_speaking(False)
            print("Assistant speaking finished: mic listening ON", flush=True)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Integrated IoT voice assistant: IoT MIC/console text -> SeniorService/Llama -> Piper -> RTP/Opus"
    )

    parser.add_argument("--device-ip", required=True, help="IoT device IP address")
    parser.add_argument("--listen-port", type=int, default=5004, help="PC UDP port receiving IoT RTP/Opus")
    parser.add_argument("--send-port", type=int, default=5004, help="IoT UDP/RTP receive port")
    parser.add_argument("--bitrate", type=int, default=24000)

    parser.add_argument("--whisper-model", default="small")
    parser.add_argument("--whisper-device", default="cpu", help="cpu or cuda")
    parser.add_argument("--whisper-compute-type", default="int8", help="int8, float16, float32")
    parser.add_argument("--language", default=None, help="ko, en, or empty for auto")

    parser.add_argument("--piper-bin", default="/home/soo/.local/bin/piper")
    parser.add_argument("--piper-model", required=True)

    parser.add_argument("--vad-start-rms", type=int, default=20)
    parser.add_argument("--vad-stop-rms", type=int, default=10)
    parser.add_argument("--vad-silence-ms", type=int, default=900)
    parser.add_argument("--vad-preroll-ms", type=int, default=300)
    parser.add_argument("--max-segment-sec", type=float, default=8.0)

    parser.add_argument(
        "--listen-resume-delay",
        type=float,
        default=0.5,
        help="Seconds to wait after TTS finishes before mic listening resumes",
    )

    parser.add_argument("--show-packet-stats", action="store_true")

    return parser.parse_args()


def main():
    args = parse_args()
    service = SeniorVoiceCallService(args)

    def on_signal(_signum, _frame):
        service.stop = True

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    service.start()


if __name__ == "__main__":
    main()
