import pytest

from .iec104_client import IEC104Client


@pytest.mark.link
def test_startdt_testfr_stopdt(slave_host, slave_port, pytestconfig):
    with IEC104Client(slave_host, slave_port, timeout=pytestconfig.getoption("--timeout")) as client:
        client.startdt()
        client.testfr()
        client.stopdt()


@pytest.mark.link
def test_i_frame_sequence_increments(client, ca):
    from .iec104_codec import build_interrogation

    client.send_i(build_interrogation(ca=ca))
    frames = client.recv_until(
        lambda fs: any(f.asdu and f.asdu.type_id == 0x64 and f.asdu.cot == 0x0A for f in fs),
        timeout=45,
    )
    i_frames = [f for f in frames if f.kind == "I"]
    assert len(i_frames) >= 2
    send_seq_values = [f.send_seq for f in i_frames if f.send_seq is not None]
    assert send_seq_values == sorted(send_seq_values)
    assert len(set(send_seq_values)) == len(send_seq_values)
