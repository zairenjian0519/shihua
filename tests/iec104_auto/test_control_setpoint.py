import pytest

from .iec104_codec import (
    COT_ACTIVATION_CON,
    COT_ACTIVATION_TERMINATION,
    assert_has_asdu,
    build_setpoint_scaled,
    build_single_command,
)


@pytest.mark.standard
def test_single_command_select_execute(client, ca):
    client.send_i(build_single_command(ca, 0x6001, state=1, select=True))
    frames = client.recv_until(lambda fs: any(f.asdu and f.asdu.type_id == 0x2D for f in fs), timeout=3)
    assert_has_asdu(frames, 0x2D, COT_ACTIVATION_CON, ca)

    client.send_i(build_single_command(ca, 0x6001, state=1, select=False))
    frames = client.recv_until(lambda fs: any(f.asdu and f.asdu.type_id == 0x2D and f.asdu.cot == COT_ACTIVATION_TERMINATION for f in fs), timeout=3)
    assert_has_asdu(frames, 0x2D, COT_ACTIVATION_TERMINATION, ca)


@pytest.mark.negative
def test_single_command_out_of_range(client, ca):
    client.send_i(build_single_command(ca, 0x6000, state=1, select=True))
    frames = client.recv_until(
        lambda fs: any(
            f.asdu and f.asdu.type_id == 0x2D and f.asdu.cot == COT_ACTIVATION_TERMINATION
            for f in fs
        ),
        timeout=3,
    )
    assert_has_asdu(frames, 0x2D, COT_ACTIVATION_TERMINATION, ca)


@pytest.mark.standard
def test_setpoint_scaled_select_execute(client, ca):
    client.send_i(build_setpoint_scaled(ca, 0x6201, value=100, select=True))
    frames = client.recv_until(lambda fs: any(f.asdu and f.asdu.type_id == 0x31 for f in fs), timeout=3)
    assert_has_asdu(frames, 0x31, COT_ACTIVATION_CON, ca)

    client.send_i(build_setpoint_scaled(ca, 0x6201, value=100, select=False))
    frames = client.recv_until(lambda fs: any(f.asdu and f.asdu.type_id == 0x31 and f.asdu.cot == COT_ACTIVATION_TERMINATION for f in fs), timeout=3)
    assert_has_asdu(frames, 0x31, COT_ACTIVATION_TERMINATION, ca)


@pytest.mark.negative
def test_setpoint_scaled_out_of_range_ioa(client, ca):
    client.send_i(build_setpoint_scaled(ca, 0x6200, value=100, select=True))
    frames = client.recv_until(
        lambda fs: any(
            f.asdu and f.asdu.type_id == 0x31 and f.asdu.cot == COT_ACTIVATION_TERMINATION
            for f in fs
        ),
        timeout=3,
    )
    assert_has_asdu(frames, 0x31, COT_ACTIVATION_TERMINATION, ca)


@pytest.mark.negative
@pytest.mark.xfail(reason="当前拒绝遥控/遥调返回 0x0A 但 negative=false，需与主站兼容性确认")
def test_single_command_direct_execute_rejection_sets_negative(client, ca):
    client.send_i(build_single_command(ca, 0x6002, state=1, select=False))
    frames = client.recv_until(lambda fs: any(f.asdu and f.asdu.type_id == 0x2D and f.asdu.cot == COT_ACTIVATION_TERMINATION for f in fs), timeout=3)
    asdu = assert_has_asdu(frames, 0x2D, COT_ACTIVATION_TERMINATION, ca)
    assert asdu.negative
