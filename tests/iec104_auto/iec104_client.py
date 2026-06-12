from __future__ import annotations

import socket
import time
from typing import Iterable, List

from .iec104_codec import (
    Apdu,
    U_STARTDT_ACT,
    U_STARTDT_CON,
    U_STOPDT_ACT,
    U_STOPDT_CON,
    U_TESTFR_ACT,
    U_TESTFR_CON,
    hx,
    parse_apdu,
)


class IEC104Client:
    def __init__(self, host: str, port: int = 2404, timeout: float = 3.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self.send_seq = 0
        self.recv_seq = 0
        self.received = []

    def __enter__(self) -> "IEC104Client":
        self.connect()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def connect(self) -> None:
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self.sock.settimeout(self.timeout)

    def close(self) -> None:
        if self.sock is not None:
            self.sock.close()
            self.sock = None

    def _send_raw(self, data: bytes) -> None:
        if self.sock is None:
            raise RuntimeError("client is not connected")
        self.sock.sendall(data)

    def startdt(self) -> Apdu:
        self._send_raw(U_STARTDT_ACT)
        try:
            frame = self.recv_one()
        except ConnectionError as exc:
            raise ConnectionError(
                f"TCP connected to {self.host}:{self.port}, but the slave closed the connection "
                "during STARTDT. Check whether iec104_slave is running, whether another master "
                "is already connected when max_open_connections=1, and whether the process log "
                "reports connection refusal or startup failure."
            ) from exc
        if frame.raw != U_STARTDT_CON:
            raise AssertionError(f"expected STARTDT CON {hx(U_STARTDT_CON)}, got {hx(frame.raw)}")
        return frame

    def stopdt(self) -> Apdu:
        self._send_raw(U_STOPDT_ACT)
        frame = self.recv_one()
        if frame.raw != U_STOPDT_CON:
            raise AssertionError(f"expected STOPDT CON {hx(U_STOPDT_CON)}, got {hx(frame.raw)}")
        return frame

    def testfr(self) -> Apdu:
        self._send_raw(U_TESTFR_ACT)
        frame = self.recv_one()
        if frame.raw != U_TESTFR_CON:
            raise AssertionError(f"expected TESTFR CON {hx(U_TESTFR_CON)}, got {hx(frame.raw)}")
        return frame

    def send_i(self, asdu: bytes) -> None:
        ctrl = ((self.send_seq << 1).to_bytes(2, "little") + (self.recv_seq << 1).to_bytes(2, "little"))
        apdu = bytes([0x68, len(ctrl) + len(asdu)]) + ctrl + asdu
        self._send_raw(apdu)
        self.send_seq += 1

    def send_s(self) -> None:
        ctrl = b"\x01\x00" + (self.recv_seq << 1).to_bytes(2, "little")
        self._send_raw(b"\x68\x04" + ctrl)

    def recv_one(self) -> Apdu:
        if self.sock is None:
            raise RuntimeError("client is not connected")
        header = self._recv_exact(2)
        if header[0] != 0x68:
            raise AssertionError(f"invalid start byte 0x{header[0]:02X}")
        body = self._recv_exact(header[1])
        frame = parse_apdu(header + body)
        self._observe(frame)
        return frame

    def recv_until(
        self,
        predicate,
        timeout: float = 5.0,
        min_frames: int = 1,
    ) -> List[Apdu]:
        deadline = time.monotonic() + timeout
        frames = []
        while time.monotonic() < deadline:
            old_timeout = self.sock.gettimeout() if self.sock else self.timeout
            try:
                if self.sock:
                    self.sock.settimeout(max(0.05, min(0.5, deadline - time.monotonic())))
                frame = self.recv_one()
                frames.append(frame)
                if len(frames) >= min_frames and predicate(frames):
                    return frames
            except socket.timeout:
                if len(frames) >= min_frames and predicate(frames):
                    return frames
            finally:
                if self.sock:
                    self.sock.settimeout(old_timeout)
        raise TimeoutError(f"condition not met after {timeout}s; frames={self.summary(frames)}")

    def recv_for(self, seconds: float) -> List[Apdu]:
        end = time.monotonic() + seconds
        frames = []
        while time.monotonic() < end:
            old_timeout = self.sock.gettimeout() if self.sock else self.timeout
            try:
                if self.sock:
                    self.sock.settimeout(max(0.05, min(0.5, end - time.monotonic())))
                frames.append(self.recv_one())
            except socket.timeout:
                pass
            finally:
                if self.sock:
                    self.sock.settimeout(old_timeout)
        return frames

    def drain(self, timeout: float = 1.0, idle: float = 0.1) -> List[Apdu]:
        end = time.monotonic() + timeout
        frames = []
        old_timeout = self.sock.gettimeout() if self.sock else self.timeout
        try:
            while time.monotonic() < end:
                try:
                    if self.sock:
                        self.sock.settimeout(max(0.01, min(idle, end - time.monotonic())))
                    frames.append(self.recv_one())
                except socket.timeout:
                    break
        finally:
            if self.sock:
                self.sock.settimeout(old_timeout)
        return frames

    def _recv_exact(self, size: int) -> bytes:
        if self.sock is None:
            raise RuntimeError("client is not connected")
        chunks = []
        remaining = size
        while remaining > 0:
            chunk = self.sock.recv(remaining)
            if not chunk:
                raise ConnectionError("connection closed")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def _observe(self, frame: Apdu) -> None:
        self.received.append(frame)
        if frame.kind == "I" and frame.send_seq is not None:
            self.recv_seq = max(self.recv_seq, frame.send_seq + 1)
            self.send_s()

    @staticmethod
    def summary(frames: Iterable[Apdu]) -> str:
        parts = []
        for frame in frames:
            if frame.asdu is not None:
                parts.append(f"I:{frame.asdu.type_id:02X}/{frame.asdu.cot:02X}/CA{frame.asdu.ca}")
            else:
                parts.append(f"{frame.kind}:{hx(frame.raw)}")
        return "[" + ", ".join(parts) + "]"
