import pytest

from .iec104_codec import (
    COT_ACTIVATION_CON,
    COT_ACTIVATION_TERMINATION,
    COT_INTERROGATED_BY_STATION,
    COT_REQUEST,
    COT_UNKNOWN_CA,
    assert_has_asdu,
    assert_all_vsq_match_payload,
    assert_expected_ioas_present,
    assert_no_i_frame_sequence_gap,
    assert_no_ioa_outside,
    assert_expected_range_exact,
    assert_values_equal,
    build_clock_sync,
    build_counter_interrogation,
    build_interrogation,
    collect_information_objects,
    collect_value_map,
)


def collect_until(client, type_id, cot, timeout=45):
    return client.recv_until(
        lambda fs: any(f.asdu and f.asdu.type_id == type_id and f.asdu.cot == cot for f in fs),
        timeout=timeout,
    )


@pytest.mark.standard
def test_general_interrogation_flow(client, ca):
    client.send_i(build_interrogation(ca=ca))
    frames = collect_until(client, 0x64, COT_ACTIVATION_TERMINATION)

    assert_has_asdu(frames, 0x64, COT_ACTIVATION_CON, ca)
    yx = assert_has_asdu(frames, 0x01, COT_INTERROGATED_BY_STATION, ca)
    yc = assert_has_asdu(frames, 0x0B, COT_INTERROGATED_BY_STATION, ca)
    assert_has_asdu(frames, 0x64, COT_ACTIVATION_TERMINATION, ca)
    assert_no_ioa_outside(yx, 0x0001, 0x1000)
    assert_no_ioa_outside(yc, 0x4001, 0x5000)
    assert_no_i_frame_sequence_gap(frames)
    assert_all_vsq_match_payload(frames)
    for frame in frames:
        assert len(frame.raw) <= 255


@pytest.mark.standard
def test_general_interrogation_returns_all_demo_yx_yc_and_values(client, ca):
    client.send_i(build_interrogation(ca=ca))
    frames = collect_until(client, 0x64, COT_ACTIVATION_TERMINATION)
    assert_all_vsq_match_payload(frames)

    yx_values = collect_value_map(frames, 0x01, COT_INTERROGATED_BY_STATION)
    yc_values = collect_value_map(frames, 0x0B, COT_INTERROGATED_BY_STATION)

    assert_expected_range_exact(yx_values.keys(), 0x0001, 0x1000)
    assert_expected_ioas_present(yc_values.keys(), range(0x4001, 0x5001))
    assert_values_equal(
        yx_values,
        {
            0x0001: False,
            0x0002: True,
            0x0003: False,
            0x0004: True,
        },
    )
    assert_values_equal(
        yc_values,
        {
            0x4001: 10,
            0x4002: 11,
            0x4003: 12,
            0x4004: 13,
        },
    )


@pytest.mark.standard
@pytest.mark.xfail(reason="当前实现会在标准总召中追加功图/谐波/电度等扩展数据，需与协议口径确认")
def test_general_interrogation_does_not_return_custom_or_counter_ranges(client, ca):
    client.send_i(build_interrogation(ca=ca))
    frames = collect_until(client, 0x64, COT_ACTIVATION_TERMINATION)
    unexpected = []
    for frame in frames:
        if not frame.asdu:
            continue
        if frame.asdu.type_id in (0x0F, 0x89, 0x8A, 0x8E):
            unexpected.append(frame.asdu.type_id)
    assert not unexpected


@pytest.mark.standard
def test_general_interrogation_wrong_ca(client, ca):
    client.send_i(build_interrogation(ca=ca + 1))
    frames = client.recv_until(
        lambda fs: any(f.asdu and f.asdu.cot == COT_UNKNOWN_CA for f in fs),
        timeout=3,
    )
    asdu = assert_has_asdu(frames, 0x64, COT_UNKNOWN_CA, ca + 1)
    assert asdu.negative


@pytest.mark.standard
def test_counter_interrogation_flow(client, ca):
    client.send_i(build_counter_interrogation(ca=ca))
    frames = collect_until(client, 0x65, COT_ACTIVATION_TERMINATION)
    assert_all_vsq_match_payload(frames)
    assert_has_asdu(frames, 0x65, COT_ACTIVATION_CON, ca)
    dd = assert_has_asdu(frames, 0x0F, COT_REQUEST, ca)
    assert_has_asdu(frames, 0x65, COT_ACTIVATION_TERMINATION, ca)
    assert_no_ioa_outside(dd, 0x6401, 0x6600)
    dd_values = collect_value_map(frames, 0x0F, COT_REQUEST)
    assert_expected_range_exact(dd_values.keys(), 0x6401, 0x6600)


@pytest.mark.standard
def test_clock_sync_ack(client, ca):
    client.send_i(build_clock_sync(ca=ca))
    frames = client.recv_until(
        lambda fs: any(f.asdu and f.asdu.type_id == 0x67 and f.asdu.cot in (COT_ACTIVATION_CON, COT_ACTIVATION_TERMINATION) for f in fs),
        timeout=3,
    )
    asdu = assert_has_asdu(frames, 0x67, COT_ACTIVATION_CON, ca)
    assert len(asdu.payload) >= 10
