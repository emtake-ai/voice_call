#!/usr/bin/env python3
"""
listen.py

PC-side listener for SigmaStar IoT device RTP/Opus microphone stream.

Flow:
    IoT sk_live / sk_voice_call_main
      MIC -> PCM -> Opus -> RTP/UDP
        ↓
    PC listen.py
      UDP receive -> RTP parse -> Opus decode -> PCM -> Whisper STT -> print text

Expected IoT command:
    ./sk_live <PC_IP> 5004 16000 24000 0 5 1

Install:
    pip install numpy opuslib faster-whisper

Run:
    cd listen
    python3 listen.py --listen-port 5004 --whisper-model small

Korean:
    python3 listen.py --listen-port 5004 --whisper-model small --language ko

English:
    python3 listen.py --listen-port 5004 --whisper-model small --language en
"""

import argparse
import audioop
import queue
import signal
import socket
import struct
import threading
import time
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


RTP_VERSION = 2
RTP_PAYLOAD_TYPE_OPUS = 96

SAMPLE_RATE = 16000
CHANNELS = 1
FRAME_MS = 20
PCM_WIDTH_BYTES = 2
MAX_UDP_PACKET = 2048


def parse_rtp_packet(packet: bytes) -> Optional[Tuple[int, int, int, bytes]]:
    """
    Parse RTP packet and return:
        sequence, timestamp, ssrc, opus_payload
    """
    if len(packet) < 12:
        return None

    b0, b1, seq, timestamp, ssrc = struct.unpack("!BBHII", packet[:12])

    version = b0 >> 6
    if version != RTP_VERSION:
        return None

    payload_type = b1 & 0x7F
    if payload_type != RTP_PAYLOAD_TYPE_OPUS:
        return None

    cc = b0 & 0x0F
    header_len = 12 + cc * 4

    if len(packet) < header_len:
        return None

    # RTP extension header
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


class OpusDecoder:
    def __init__(self, sample_rate: int = SAMPLE_RATE, channels: int = CHANNELS):
        if opuslib is None:
            raise RuntimeError("opuslib is not installed. Run: pip install opuslib")

        self.sample_rate = sample_rate
        self.channels = channels
        self.frame_samples = sample_rate * FRAME_MS // 1000
        self.decoder = opuslib.Decoder(sample_rate, channels)

    def decode(self, opus_payload: bytes) -> bytes:
        """
        Decode one Opus packet to signed 16-bit little-endian PCM.
        """
        return self.decoder.decode(opus_payload, self.frame_samples)


class SpeechSegmenter:
    """
    Simple RMS-based VAD.

    It waits for audio above start_rms, records frames,
    then stops after silence_ms of low RMS.
    """

    def __init__(
        self,
        start_rms: int = 500,
        stop_rms: int = 300,
        silence_ms: int = 900,
        preroll_ms: int = 300,
        max_segment_sec: float = 8.0,
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

            self.in_speech = False
            self.silence_count = 0
            self.frames = []
            self.preroll = []
            return segment

        return None


class WhisperTranscriber:
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


class Listener:
    def __init__(self, args):
        self.args = args
        self.stop = False

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", args.listen_port))
        self.sock.settimeout(0.2)

        self.decoder = OpusDecoder()
        self.segmenter = SpeechSegmenter(
            start_rms=args.vad_start_rms,
            stop_rms=args.vad_stop_rms,
            silence_ms=args.vad_silence_ms,
            max_segment_sec=args.max_segment_sec,
        )
        self.whisper = WhisperTranscriber(
            model_name=args.whisper_model,
            device=args.whisper_device,
            compute_type=args.whisper_compute_type,
        )

        self.segment_queue = queue.Queue()
        self.packet_count = 0
        self.last_stat_time = time.time()

    def start(self):
        print(f"Listening for RTP/Opus on UDP port {self.args.listen_port}", flush=True)
        print("Speak into IoT microphone. Press Ctrl+C to stop.\n", flush=True)

        worker = threading.Thread(target=self.transcribe_loop, daemon=True)
        worker.start()

        while not self.stop:
            try:
                packet, addr = self.sock.recvfrom(MAX_UDP_PACKET)
            except socket.timeout:
                self.print_stats()
                continue
            except OSError:
                break

            parsed = parse_rtp_packet(packet)
            if parsed is None:
                continue

            seq, timestamp, ssrc, opus_payload = parsed
            self.packet_count += 1

            try:
                pcm = self.decoder.decode(opus_payload)
            except Exception as e:
                print(f"Opus decode error: {e}", flush=True)
                continue

            segment = self.segmenter.push(pcm)
            if segment:
                self.segment_queue.put(segment)

            self.print_stats()

    def print_stats(self):
        now = time.time()
        if now - self.last_stat_time >= 3.0:
            print(f"received RTP packets={self.packet_count}", flush=True)
            self.last_stat_time = now

    def transcribe_loop(self):
        while not self.stop:
            try:
                segment = self.segment_queue.get(timeout=0.2)
            except queue.Empty:
                continue

            try:
                text = self.whisper.transcribe_pcm(segment, self.args.language)
            except Exception as e:
                print(f"Whisper error: {e}", flush=True)
                continue

            if text:
                print(f"\nWhisper text: {text}\n", flush=True)
            else:
                print("Whisper text: <empty>", flush=True)


def parse_args():
    parser = argparse.ArgumentParser(description="Listen to IoT RTP/Opus MIC stream and transcribe with Whisper")

    parser.add_argument("--listen-port", type=int, default=5004)
    parser.add_argument("--whisper-model", default="small")
    parser.add_argument("--whisper-device", default="cpu", help="cpu or cuda")
    parser.add_argument("--whisper-compute-type", default="int8", help="int8, float16, float32")
    parser.add_argument("--language", default=None, help="ko, en, or empty for auto")

    parser.add_argument("--vad-start-rms", type=int, default=500)
    parser.add_argument("--vad-stop-rms", type=int, default=300)
    parser.add_argument("--vad-silence-ms", type=int, default=900)
    parser.add_argument("--max-segment-sec", type=float, default=8.0)

    return parser.parse_args()


def main():
    args = parse_args()
    listener = Listener(args)

    def on_signal(_signum, _frame):
        listener.stop = True

    signal.signal(signal.SIGINT, on_signal)
    signal.signal(signal.SIGTERM, on_signal)

    try:
        listener.start()
    finally:
        listener.stop = True
        listener.sock.close()
        print("\nlisten.py stopped", flush=True)


if __name__ == "__main__":
    main()
