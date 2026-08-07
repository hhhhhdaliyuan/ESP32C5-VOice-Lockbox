#!/usr/bin/env python3
"""Capture ESP32 KWS stereo Base64 output and reconstruct diagnostic WAV files."""

from __future__ import annotations

import argparse
import base64
import re
import sys
import threading
import time
import wave
from array import array
from pathlib import Path

import serial


BEGIN_RE = re.compile(
    r"KWS_STEREO_B64_BEGIN bytes=(\d+) sample_rate=(\d+) "
    r"channels=(\d+) bits=(\d+) chunk_bytes=(\d+)"
)
CHUNK_RE = re.compile(r"KWS_STEREO_B64 (\d+) ([A-Za-z0-9+/=]+)")
END_RE = re.compile(
    r"KWS_STEREO_B64_END chunks=(\d+) bytes=(\d+) status=(\w+)"
)


def write_wav(path: Path, channels: int, sample_rate: int, pcm: bytes) -> None:
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm)


def write_outputs(output_dir: Path, sample_rate: int, pcm: bytes) -> None:
    if len(pcm) % 4:
        raise ValueError(f"stereo PCM length is not frame-aligned: {len(pcm)}")

    samples = array("h")
    samples.frombytes(pcm)
    if sys.byteorder != "little":
        samples.byteswap()

    left = array("h", samples[0::2])
    right = array("h", samples[1::2])
    if sys.byteorder != "little":
        left.byteswap()
        right.byteswap()

    output_dir.mkdir(parents=True, exist_ok=True)
    write_wav(output_dir / "stereo.wav", 2, sample_rate, pcm)
    write_wav(output_dir / "left.wav", 1, sample_rate, left.tobytes())
    write_wav(output_dir / "right.wav", 1, sample_rate, right.tobytes())


def forward_commands(port: serial.Serial) -> None:
    for line in sys.stdin:
        command = line.strip().upper()
        if command in {"GO", "CAPTURE"}:
            port.write(b"CAPTURE\n")
            port.flush()
            print("CAPTURE command sent", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("build") / "diagnostics",
    )
    parser.add_argument(
        "--stay-open",
        action="store_true",
        help="keep monitoring after WAV reconstruction",
    )
    parser.add_argument(
        "--no-reset",
        action="store_true",
        help="do not reset the ESP32 after opening the serial port",
    )
    args = parser.parse_args()

    output_dir = args.output.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    serial_log = output_dir / "serial.log"

    expected_bytes = None
    sample_rate = None
    next_chunk = 0
    chunks: list[str] = []

    print(f"Monitoring {args.port} at {args.baud} baud")
    print(f"Diagnostic output: {output_dir}")

    port = serial.Serial(port=None, baudrate=args.baud, timeout=0.25)
    port.rts = True
    port.dtr = True
    port.port = args.port
    port.open()
    port.setRTS(False)
    port.setDTR(port.dtr)
    port.setDTR(False)
    port.reset_input_buffer()
    if not args.no_reset:
        port.setRTS(True)
        port.setDTR(port.dtr)
        time.sleep(0.1)
        port.setRTS(False)
        port.setDTR(port.dtr)

    with port:
        threading.Thread(
            target=forward_commands,
            args=(port,),
            daemon=True,
        ).start()
        with serial_log.open("a", encoding="utf-8", errors="replace") as log:
            pending = bytearray()
            while True:
                data = port.read(4096)
                if not data:
                    continue
                pending.extend(data)

                while True:
                    newline = pending.find(b"\n")
                    if newline < 0:
                        break
                    raw_line = bytes(pending[:newline])
                    del pending[: newline + 1]
                    line = raw_line.decode(
                        "utf-8", errors="replace"
                    ).rstrip("\r")
                    log.write(line + "\n")
                    log.flush()

                    begin = BEGIN_RE.search(line)
                    if begin:
                        expected_bytes = int(begin.group(1))
                        sample_rate = int(begin.group(2))
                        channels = int(begin.group(3))
                        bits = int(begin.group(4))
                        if channels != 2 or bits != 16:
                            raise ValueError(
                                "unsupported capture format: "
                                f"{channels}ch/{bits}bit"
                            )
                        next_chunk = 0
                        chunks.clear()
                        print(
                            f"Receiving stereo capture: {expected_bytes} bytes, "
                            f"{sample_rate} Hz"
                        )
                        continue

                    chunk = CHUNK_RE.search(line)
                    if chunk:
                        index = int(chunk.group(1))
                        if index != next_chunk:
                            raise ValueError(
                                "Base64 sequence gap: "
                                f"expected {next_chunk}, got {index}"
                            )
                        chunks.append(chunk.group(2))
                        next_chunk += 1
                        if next_chunk % 100 == 0:
                            print(f"Received {next_chunk} Base64 chunks")
                        continue

                    end = END_RE.search(line)
                    if end:
                        reported_chunks = int(end.group(1))
                        reported_bytes = int(end.group(2))
                        status = end.group(3)
                        if status != "ok":
                            raise RuntimeError(
                                "firmware reported Base64 export failure"
                            )
                        if reported_chunks != next_chunk:
                            raise ValueError(
                                "chunk count mismatch: "
                                f"{reported_chunks} != {next_chunk}"
                            )
                        pcm = base64.b64decode(
                            "".join(chunks), validate=True
                        )
                        if expected_bytes is None or sample_rate is None:
                            raise ValueError(
                                "received export end without begin marker"
                            )
                        if (
                            len(pcm) != expected_bytes
                            or len(pcm) != reported_bytes
                        ):
                            raise ValueError(
                                "PCM byte count mismatch: "
                                f"decoded={len(pcm)} "
                                f"expected={expected_bytes} "
                                f"reported={reported_bytes}"
                            )
                        write_outputs(output_dir, sample_rate, pcm)
                        print(f"WAV files written to {output_dir}")
                        if not args.stay_open:
                            return 0
                        continue

                    print(line, flush=True)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)
