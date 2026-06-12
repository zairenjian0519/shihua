from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
import struct
from typing import Dict, Iterable, List, Optional, Tuple


U_STARTDT_ACT = bytes.fromhex("68 04 07 00 00 00")
U_STARTDT_CON = bytes.fromhex("68 04 0B 00 00 00")
U_STOPDT_ACT = bytes.fromhex("68 04 13 00 00 00")
U_STOPDT_CON = bytes.fromhex("68 04 23 00 00 00")
U_TESTFR_ACT = bytes.fromhex("68 04 43 00 00 00")
U_TESTFR_CON = bytes.fromhex("68 04 83 00 00 00")


COT_SPONTANEOUS = 0x03
COT_REQUEST = 0x05
COT_ACTIVATION = 0x06
COT_ACTIVATION_CON = 0x07
COT_DEACTIVATION = 0x08
COT_DEACTIVATION_CON = 0x09
COT_ACTIVATION_TERMINATION = 0x0A
COT_INTERROGATED_BY_STATION = 0x14
COT_UNKNOWN_TYPE_ID = 0x2C
COT_UNKNOWN_COT = 0x2D
COT_UNKNOWN_CA = 0x2E
COT_UNKNOWN_IOA = 0x2F


def hx(data: bytes) -> str:
    return data.hex(" ").upper()


def bcdless_cp56(dt: Optional[datetime] = None) -> bytes:
    if dt is None:
        dt = datetime.now()
    ms = dt.second * 1000 + dt.microsecond // 1000
    return bytes(
        [
            ms & 0xFF,
            (ms >> 8) & 0xFF,
            dt.minute & 0x3F,
            dt.hour & 0x1F,
            dt.day & 0x1F,
            dt.month & 0x0F,
            dt.year % 100,
        ]
    )


def le_u16(value: int) -> bytes:
    return int(value).to_bytes(2, "little", signed=False)


def le_i16(value: int) -> bytes:
    return int(value).to_bytes(2, "little", signed=True)


def ioa3(ioa: int) -> bytes:
    return int(ioa).to_bytes(3, "little", signed=False)


def parse_ioa(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 3], "little")


def build_asdu(
    type_id: int,
    cot: int,
    ca: int = 1,
    ioa: int = 0,
    payload: bytes = b"",
    oa: int = 0,
    num: int = 1,
    sequence: bool = False,
) -> bytes:
    vsq = (0x80 if sequence else 0x00) | (num & 0x7F)
    return bytes([type_id & 0xFF, vsq, cot & 0xFF, oa & 0xFF]) + le_u16(ca) + ioa3(ioa) + payload


def build_interrogation(ca: int = 1, qoi: int = 0x14) -> bytes:
    return build_asdu(0x64, COT_ACTIVATION, ca=ca, ioa=0, payload=bytes([qoi]))


def build_counter_interrogation(ca: int = 1, qcc: int = 0x45) -> bytes:
    return build_asdu(0x65, COT_ACTIVATION, ca=ca, ioa=0, payload=bytes([qcc]))


def build_clock_sync(ca: int = 1, timestamp: Optional[bytes] = None) -> bytes:
    return build_asdu(0x67, COT_ACTIVATION, ca=ca, ioa=0, payload=timestamp or bcdless_cp56())


def build_single_command(ca: int, ioa: int, state: int, select: bool) -> bytes:
    sco = (0x80 if select else 0x00) | (0x01 if state else 0x00)
    return build_asdu(0x2D, COT_ACTIVATION, ca=ca, ioa=ioa, payload=bytes([sco]))


def build_setpoint_scaled(ca: int, ioa: int, value: int, select: bool) -> bytes:
    qos = 0x80 if select else 0x00
    return build_asdu(0x31, COT_ACTIVATION, ca=ca, ioa=ioa, payload=le_i16(value) + bytes([qos]))


def build_custom_call(type_id: int, ca: int = 1, ioa: int = 0, payload: bytes = b"\x45") -> bytes:
    return build_asdu(type_id, COT_ACTIVATION, ca=ca, ioa=ioa, payload=payload)


def build_history_call(
    type_id: int = 0x8B,
    ca: int = 1,
    ioa: int = 0,
    begin: Optional[bytes] = None,
    end: Optional[bytes] = None,
) -> bytes:
    return build_asdu(type_id, COT_ACTIVATION, ca=ca, ioa=ioa, payload=(begin or bytes(7)) + (end or bytes(7)))


@dataclass(frozen=True)
class Apdu:
    raw: bytes
    kind: str
    send_seq: Optional[int] = None
    recv_seq: Optional[int] = None
    u_control: Optional[int] = None
    asdu: Optional["Asdu"] = None


@dataclass(frozen=True)
class InformationValue:
    ioa: int
    value: object
    quality: Optional[int] = None
    timestamp: Optional[bytes] = None
    raw: bytes = b""


@dataclass(frozen=True)
class Asdu:
    type_id: int
    vsq: int
    cot_raw: int
    cot: int
    negative: bool
    test: bool
    oa: int
    ca: int
    payload: bytes

    @property
    def num(self) -> int:
        return self.vsq & 0x7F

    @property
    def sequence(self) -> bool:
        return (self.vsq & 0x80) != 0

    @property
    def first_ioa(self) -> Optional[int]:
        if len(self.payload) < 3:
            return None
        return parse_ioa(self.payload, 0)

    def object_ioas(self) -> List[int]:
        return [obj.ioa for obj in self.information_objects()]

    def information_objects(self) -> List[InformationValue]:
        if len(self.payload) < 3 or self.num == 0:
            return []

        if self.type_id == 0x01:
            return _parse_standard_objects(self, element_size=1, parser=_parse_single_point)
        if self.type_id == 0x0B:
            return _parse_standard_objects(self, element_size=3, parser=_parse_measured_scaled)
        if self.type_id == 0x0D:
            return _parse_standard_objects(self, element_size=5, parser=_parse_measured_short)
        if self.type_id == 0x0F:
            return _parse_standard_objects(self, element_size=5, parser=_parse_counter)
        if self.type_id == 0x1E:
            return _parse_standard_objects(self, element_size=8, parser=_parse_single_point_time)
        if self.type_id == 0x23:
            return _parse_standard_objects(self, element_size=10, parser=_parse_measured_scaled_time)
        if self.type_id == 0x25:
            return _parse_standard_objects(self, element_size=12, parser=_parse_measured_short_time)

        return [InformationValue(self.first_ioa, None, raw=self.payload)] if self.first_ioa is not None else []

    def value_map(self) -> Dict[int, object]:
        return {obj.ioa: obj.value for obj in self.information_objects()}

    def custom_word_values(self) -> Dict[int, int]:
        if len(self.payload) < 3:
            return {}

        first = parse_ioa(self.payload, 0)
        offset = 3
        values: Dict[int, int] = {}
        for index in range(self.num):
            if offset + 2 > len(self.payload):
                break
            values[first + index] = int.from_bytes(self.payload[offset : offset + 2], "little", signed=False)
            offset += 2
        return values


def _parse_standard_objects(asdu: Asdu, element_size: int, parser) -> List[InformationValue]:
    objects: List[InformationValue] = []

    if asdu.sequence:
        if len(asdu.payload) < 3:
            return objects
        first_ioa = parse_ioa(asdu.payload, 0)
        offset = 3
        for index in range(asdu.num):
            if offset + element_size > len(asdu.payload):
                break
            raw = asdu.payload[offset : offset + element_size]
            objects.append(parser(first_ioa + index, raw))
            offset += element_size
        return objects

    offset = 0
    object_size = 3 + element_size
    for _ in range(asdu.num):
        if offset + object_size > len(asdu.payload):
            break
        ioa = parse_ioa(asdu.payload, offset)
        raw = asdu.payload[offset + 3 : offset + object_size]
        objects.append(parser(ioa, raw))
        offset += object_size
    return objects


def _parse_single_point(ioa: int, raw: bytes) -> InformationValue:
    siq = raw[0]
    return InformationValue(ioa=ioa, value=bool(siq & 0x01), quality=siq & 0xF0, raw=raw)


def _parse_single_point_time(ioa: int, raw: bytes) -> InformationValue:
    siq = raw[0]
    return InformationValue(ioa=ioa, value=bool(siq & 0x01), quality=siq & 0xF0, timestamp=raw[1:8], raw=raw)


def _parse_measured_scaled(ioa: int, raw: bytes) -> InformationValue:
    value = int.from_bytes(raw[0:2], "little", signed=True)
    return InformationValue(ioa=ioa, value=value, quality=raw[2], raw=raw)


def _parse_measured_scaled_time(ioa: int, raw: bytes) -> InformationValue:
    value = int.from_bytes(raw[0:2], "little", signed=True)
    return InformationValue(ioa=ioa, value=value, quality=raw[2], timestamp=raw[3:10], raw=raw)


def _parse_measured_short(ioa: int, raw: bytes) -> InformationValue:
    value = struct.unpack("<f", raw[0:4])[0]
    return InformationValue(ioa=ioa, value=value, quality=raw[4], raw=raw)


def _parse_measured_short_time(ioa: int, raw: bytes) -> InformationValue:
    value = struct.unpack("<f", raw[0:4])[0]
    return InformationValue(ioa=ioa, value=value, quality=raw[4], timestamp=raw[5:12], raw=raw)


def _parse_counter(ioa: int, raw: bytes) -> InformationValue:
    value = int.from_bytes(raw[0:4], "little", signed=True)
    return InformationValue(ioa=ioa, value=value, quality=raw[4], raw=raw)


def parse_apdu(raw: bytes) -> Apdu:
    if len(raw) < 6 or raw[0] != 0x68:
        raise ValueError(f"invalid APDU: {hx(raw)}")
    length = raw[1]
    if length + 2 != len(raw):
        raise ValueError(f"APDU length mismatch len={length}: {hx(raw)}")

    c0, c1, c2, c3 = raw[2:6]
    if (c0 & 0x01) == 0:
        send_seq = int.from_bytes(raw[2:4], "little") >> 1
        recv_seq = int.from_bytes(raw[4:6], "little") >> 1
        asdu = parse_asdu(raw[6:]) if len(raw) > 6 else None
        return Apdu(raw=raw, kind="I", send_seq=send_seq, recv_seq=recv_seq, asdu=asdu)

    if (c0 & 0x03) == 0x01:
        recv_seq = int.from_bytes(raw[4:6], "little") >> 1
        return Apdu(raw=raw, kind="S", recv_seq=recv_seq)

    return Apdu(raw=raw, kind="U", u_control=c0)


def parse_asdu(data: bytes) -> Asdu:
    if len(data) < 6:
        raise ValueError(f"ASDU too short: {hx(data)}")
    cot_raw = data[2]
    return Asdu(
        type_id=data[0],
        vsq=data[1],
        cot_raw=cot_raw,
        cot=cot_raw & 0x3F,
        negative=(cot_raw & 0x40) != 0,
        test=(cot_raw & 0x80) != 0,
        oa=data[3],
        ca=int.from_bytes(data[4:6], "little"),
        payload=data[6:],
    )


def asdus(frames: Iterable[Apdu]) -> List[Asdu]:
    return [frame.asdu for frame in frames if frame.kind == "I" and frame.asdu is not None]


def filter_asdus(frames: Iterable[Apdu], type_id: int, cot: Optional[int] = None) -> List[Asdu]:
    return [
        asdu
        for asdu in asdus(frames)
        if asdu.type_id == type_id and (cot is None or asdu.cot == cot)
    ]


def collect_information_objects(frames: Iterable[Apdu], type_id: int, cot: Optional[int] = None) -> List[InformationValue]:
    objects: List[InformationValue] = []
    for asdu in filter_asdus(frames, type_id, cot):
        objects.extend(asdu.information_objects())
    return objects


def collect_value_map(frames: Iterable[Apdu], type_id: int, cot: Optional[int] = None) -> Dict[int, object]:
    return {obj.ioa: obj.value for obj in collect_information_objects(frames, type_id, cot)}


def collect_custom_word_map(frames: Iterable[Apdu], type_id: int, cot: Optional[int] = None) -> Dict[int, int]:
    values: Dict[int, int] = {}
    for asdu in filter_asdus(frames, type_id, cot):
        values.update(asdu.custom_word_values())
    return values


def assert_has_asdu(frames: Iterable[Apdu], type_id: int, cot: int, ca: Optional[int] = None) -> Asdu:
    for asdu in asdus(frames):
        if asdu.type_id == type_id and asdu.cot == cot and (ca is None or asdu.ca == ca):
            return asdu
    got = ", ".join(f"{a.type_id:02X}/{a.cot:02X}/CA{a.ca}" for a in asdus(frames))
    raise AssertionError(f"missing ASDU type=0x{type_id:02X} cot=0x{cot:02X} ca={ca}; got [{got}]")


def assert_no_ioa_outside(asdu: Asdu, start: int, end: int) -> None:
    bad = [ioa for ioa in asdu.object_ioas() if ioa < start or ioa > end]
    if bad:
        raise AssertionError(f"ASDU 0x{asdu.type_id:02X} has IOA outside 0x{start:04X}..0x{end:04X}: {bad[:10]}")


def assert_no_i_frame_sequence_gap(frames: Iterable[Apdu]) -> None:
    seqs = [frame.send_seq for frame in frames if frame.kind == "I" and frame.send_seq is not None]
    if len(seqs) < 2:
        return
    expected = list(range(seqs[0], seqs[0] + len(seqs)))
    if seqs != expected:
        raise AssertionError(f"I-frame sequence gap or duplicate: expected {expected[:10]}..., got {seqs[:10]}...")


def assert_vsq_matches_payload(asdu: Asdu) -> None:
    if asdu.type_id not in (0x01, 0x0B, 0x0D, 0x0F, 0x1E, 0x23, 0x25):
        return
    actual = len(asdu.information_objects())
    if actual != asdu.num:
        raise AssertionError(
            f"ASDU 0x{asdu.type_id:02X} VSQ count mismatch: vsq={asdu.num}, parsed={actual}, payload={hx(asdu.payload)}"
        )


def assert_all_vsq_match_payload(frames: Iterable[Apdu]) -> None:
    for asdu in asdus(frames):
        assert_vsq_matches_payload(asdu)


def assert_expected_ioas_present(actual_ioas: Iterable[int], expected_ioas: Iterable[int]) -> None:
    actual = set(actual_ioas)
    expected = set(expected_ioas)
    missing = sorted(expected - actual)
    if missing:
        raise AssertionError(f"missing IOAs: {[hex(v) for v in missing[:20]]} total_missing={len(missing)}")


def assert_no_extra_ioas(actual_ioas: Iterable[int], allowed_ioas: Iterable[int]) -> None:
    actual = set(actual_ioas)
    allowed = set(allowed_ioas)
    extra = sorted(actual - allowed)
    if extra:
        raise AssertionError(f"extra IOAs: {[hex(v) for v in extra[:20]]} total_extra={len(extra)}")


def assert_expected_range_exact(actual_ioas: Iterable[int], start: int, end: int) -> None:
    expected = range(start, end + 1)
    assert_expected_ioas_present(actual_ioas, expected)
    assert_no_extra_ioas(actual_ioas, expected)


def assert_values_equal(actual: Dict[int, object], expected: Dict[int, object]) -> None:
    missing = sorted(set(expected) - set(actual))
    if missing:
        raise AssertionError(f"missing values for IOAs: {[hex(v) for v in missing[:20]]} total_missing={len(missing)}")

    mismatches: List[Tuple[int, object, object]] = []
    for ioa, expected_value in expected.items():
        if actual[ioa] != expected_value:
            mismatches.append((ioa, expected_value, actual[ioa]))

    if mismatches:
        sample = ", ".join(f"0x{ioa:04X}: expected {exp}, got {got}" for ioa, exp, got in mismatches[:20])
        raise AssertionError(f"value mismatches: {sample}; total_mismatches={len(mismatches)}")
