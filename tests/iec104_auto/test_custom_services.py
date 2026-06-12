import pytest

from .iec104_codec import (
    COT_ACTIVATION_CON,
    COT_ACTIVATION_TERMINATION,
    COT_REQUEST,
    assert_all_vsq_match_payload,
    assert_expected_ioas_present,
    assert_has_asdu,
    assert_no_ioa_outside,
    assert_expected_range_exact,
    assert_values_equal,
    build_custom_call,
    build_history_call,
    collect_custom_word_map,
    collect_value_map,
)


CUSTOM_RANGES = [
    pytest.param(0x8C, 0x8C, 0x1001, 0x103E, {0x1001: 100, 0x1002: 101}, id="rtu-param"),
    pytest.param(0x8D, 0x8D, 0x2001, 0x4000, {0x2001: 200, 0x2002: 201}, id="sensor-param"),
    pytest.param(0x8E, 0x8E, 0x5201, 0x5400, {0x5201: 1012, 0x5202: 1013}, id="harmonic"),
    pytest.param(0x8F, 0x8F, 0x5001, 0x5100, {0x5001: 500, 0x5002: 501}, id="meter-truck"),
    pytest.param(0x90, 0x90, 0x5101, 0x5200, {0x5101: 756, 0x5102: 757}, id="injection"),
    pytest.param(0x92, 0x92, 0x5CB7, 0x5FD6, {0x5CB7: 3754, 0x5CB8: 3755}, id="active-power"),
    pytest.param(0x93, 0x93, 0x5B27, 0x5CB6, {0x5B27: 3354, 0x5B28: 3355}, id="wellhead-pressure"),
    pytest.param(0x95, 0x95, 0x4200, 0x42AA, {}, id="reserved-sensor"),
]


def collect_until(client, type_id, cot, timeout=45):
    return client.recv_until(
        lambda fs: any(f.asdu and f.asdu.type_id == type_id and f.asdu.cot == cot for f in fs),
        timeout=timeout,
    )


@pytest.mark.custom
def test_measure_total_call_0x88(client, ca):
    client.send_i(build_custom_call(0x88, ca=ca))
    frames = collect_until(client, 0x88, COT_ACTIVATION_TERMINATION)
    assert_all_vsq_match_payload(frames)
    assert_has_asdu(frames, 0x88, COT_ACTIVATION_CON, ca)
    yx = assert_has_asdu(frames, 0x01, COT_REQUEST, ca)
    assert_has_asdu(frames, 0x0B, COT_REQUEST, ca)
    assert_has_asdu(frames, 0x88, COT_ACTIVATION_TERMINATION, ca)
    assert_no_ioa_outside(yx, 0x0001, 0x1000)
    yx_values = collect_value_map(frames, 0x01, COT_REQUEST)
    yc_values = collect_value_map(frames, 0x0B, COT_REQUEST)
    assert_expected_range_exact(yx_values.keys(), 0x0001, 0x1000)
    assert_expected_ioas_present(yc_values.keys(), range(0x4001, 0x5001))
    assert_values_equal(yc_values, {0x4001: 10, 0x4002: 11})


@pytest.mark.custom
@pytest.mark.parametrize("request_type,response_type,start,end,expected_values", CUSTOM_RANGES)
def test_custom_word_range_calls(client, ca, request_type, response_type, start, end, expected_values):
    client.send_i(build_custom_call(request_type, ca=ca))
    frames = collect_until(client, request_type, COT_ACTIVATION_TERMINATION)
    assert_all_vsq_match_payload(frames)
    assert_has_asdu(frames, request_type, COT_ACTIVATION_CON, ca)
    data_frames = [f.asdu for f in frames if f.asdu and f.asdu.type_id == response_type and f.asdu.cot == COT_REQUEST]
    assert data_frames, f"missing data frame for 0x{request_type:02X}"
    for asdu in data_frames:
        first = asdu.first_ioa
        assert first is not None
        assert start <= first <= end
    values = collect_custom_word_map(frames, response_type, COT_REQUEST)
    if start <= end and values:
        assert_expected_range_exact(values.keys(), start, end)
    if expected_values:
        assert_values_equal(values, expected_values)
    assert_has_asdu(frames, request_type, COT_ACTIVATION_TERMINATION, ca)


@pytest.mark.custom
def test_dynagram_0x89_has_cp56_tail(client, ca):
    client.send_i(build_custom_call(0x89, ca=ca))
    frames = collect_until(client, 0x89, COT_ACTIVATION_TERMINATION)
    data = [f.asdu for f in frames if f.asdu and f.asdu.type_id == 0x89 and f.asdu.cot == COT_REQUEST]
    assert data
    for asdu in data:
        assert asdu.first_ioa is not None
        assert 0x5401 <= asdu.first_ioa <= 0x5800
        assert len(asdu.payload) >= 3 + 2 + 7
    values = collect_custom_word_map(frames, 0x89, COT_REQUEST)
    assert_expected_range_exact(values.keys(), 0x5401, 0x5800)
    assert_values_equal(values, {0x5401: 1524, 0x5402: 1525})


@pytest.mark.custom
def test_all_dynagram_0x91_returns_89_8a_93_then_finish(client, ca):
    client.send_i(build_custom_call(0x91, ca=ca))
    frames = collect_until(client, 0x91, COT_ACTIVATION_TERMINATION)
    assert_has_asdu(frames, 0x91, COT_ACTIVATION_CON, ca)
    assert_has_asdu(frames, 0x89, COT_REQUEST, ca)
    assert_has_asdu(frames, 0x8A, COT_REQUEST, ca)
    assert_has_asdu(frames, 0x93, COT_REQUEST, ca)
    assert_has_asdu(frames, 0x91, COT_ACTIVATION_TERMINATION, ca)


@pytest.mark.custom
def test_history_call_0x8b_finishes(client, ca):
    client.send_i(build_history_call(0x8B, ca=ca))
    frames = collect_until(client, 0x8B, COT_ACTIVATION_TERMINATION)
    assert_has_asdu(frames, 0x8B, COT_ACTIVATION_TERMINATION, ca)
