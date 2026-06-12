import pytest

from .iec104_codec import (
    COT_ACTIVATION,
    COT_UNKNOWN_COT,
    COT_UNKNOWN_TYPE_ID,
    assert_has_asdu,
    build_asdu,
    build_custom_call,
)


@pytest.mark.negative
def test_unknown_type_id(client, ca):
    client.send_i(build_asdu(0xAA, COT_ACTIVATION, ca=ca, ioa=0, payload=b"\x45"))
    frames = client.recv_until(lambda fs: any(f.asdu and f.asdu.cot == COT_UNKNOWN_TYPE_ID for f in fs), timeout=3)
    asdu = assert_has_asdu(frames, 0xAA, COT_UNKNOWN_TYPE_ID, ca)
    assert asdu.negative


@pytest.mark.negative
def test_custom_call_wrong_cot(client, ca):
    client.send_i(build_asdu(0x88, 0x05, ca=ca, ioa=0, payload=b"\x45"))
    frames = client.recv_until(lambda fs: any(f.asdu and f.asdu.cot == COT_UNKNOWN_COT for f in fs), timeout=3)
    asdu = assert_has_asdu(frames, 0x88, COT_UNKNOWN_COT, ca)
    assert asdu.negative
