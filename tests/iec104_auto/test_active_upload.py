import random
import re
import time

import pytest

from .iec104_codec import (
    COT_ACTIVATION_TERMINATION,
    COT_INTERROGATED_BY_STATION,
    COT_REQUEST,
    COT_SPONTANEOUS,
    assert_all_vsq_match_payload,
    assert_has_asdu,
    assert_values_equal,
    build_counter_interrogation,
    build_interrogation,
    collect_value_map,
)


@pytest.fixture
def diag(remote):
    if remote is None:
        pytest.skip("diag tests require --manage-slave, --build-slave, --restart-slave, or --capture-pcap so SSH is available")

    touched = {"yx": set(), "yc": set(), "dd": set()}
    set_pattern = re.compile(r"set-(yx|yc|dd)\s+--ioa\s+(\d+)")

    def run(command):
        match = set_pattern.search(command)
        if match:
            kind, ioa_text = match.groups()
            touched[kind].add(int(ioa_text, 0))
        return remote.diag(command)

    yield run

    for ioa in sorted(touched["yx"]):
        run("set-yx --ioa %d --value %d" % (ioa, 1 if ioa % 2 == 0 else 0))
    for ioa in sorted(touched["yc"]):
        run("set-yc --ioa %d --value %d" % (ioa, ioa - 0x4001 + 10))
    for ioa in sorted(touched["dd"]):
        run("set-dd --ioa %d --value %d" % (ioa, (ioa - 0x6401 + 10) * 100))


@pytest.mark.active
def test_active_yc_upload_uses_timed_measured_value(client, ca, diag):
    diag("set-yc --ioa 16385 --value 321")
    diag("active-upload notify")
    frames = client.recv_until(lambda fs: any(f.asdu and f.asdu.type_id in (0x23, 0x25) and f.asdu.cot == COT_SPONTANEOUS for f in fs), timeout=5)
    assert_all_vsq_match_payload(frames)
    asdu = assert_has_asdu(frames, 0x23, COT_SPONTANEOUS, ca)
    assert asdu.first_ioa == 0x4001
    assert len(asdu.payload) >= 13
    values = collect_value_map(frames, 0x23, COT_SPONTANEOUS)
    assert_values_equal(values, {0x4001: 321})


@pytest.mark.active
def test_active_yx_upload_has_soe_with_timestamp(client, ca, diag):
    diag("set-yx --ioa 5 --value 0")
    client.drain(timeout=1.0)
    diag("set-yx --ioa 5 --value 1")
    diag("active-upload notify")
    frames = client.recv_until(lambda fs: any(f.asdu and f.asdu.type_id == 0x1E and f.asdu.cot == COT_SPONTANEOUS for f in fs), timeout=5)
    assert_all_vsq_match_payload(frames)
    asdu = assert_has_asdu(frames, 0x1E, COT_SPONTANEOUS, ca)
    assert 5 in asdu.object_ioas()
    assert len(asdu.payload) >= 11
    values = collect_value_map(frames, 0x1E, COT_SPONTANEOUS)
    assert_values_equal(values, {5: True})


@pytest.mark.active
@pytest.mark.xfail(reason="当前实现主动 YX 仍会先发无时标 0x01，再发 0x1E SOE")
def test_active_yx_upload_does_not_send_untimed_yx(client, ca, diag):
    diag("set-yx --ioa 6 --value 0")
    client.drain(timeout=1.0)
    diag("set-yx --ioa 6 --value 1")
    diag("active-upload notify")
    frames = client.recv_for(3)
    assert not any(f.asdu and f.asdu.type_id == 0x01 and f.asdu.cot == COT_SPONTANEOUS for f in frames)


@pytest.mark.active
def test_history_soe_call_0x94(client, ca, diag):
    from .iec104_codec import build_custom_call

    diag("soe add --ioa 7 --value 1")
    time.sleep(0.2)
    client.send_i(build_custom_call(0x94, ca=ca))
    frames = client.recv_until(lambda fs: any(f.asdu and f.asdu.type_id == 0x94 and f.asdu.cot == COT_ACTIVATION_TERMINATION for f in fs), timeout=8)
    assert_has_asdu(frames, 0x94, COT_REQUEST, ca)
    assert_has_asdu(frames, 0x94, COT_ACTIVATION_TERMINATION, ca)


@pytest.mark.active
def test_general_interrogation_reads_diag_yc_value(client, ca, diag):
    diag("set-yc --ioa 16386 --value 456")
    client.drain(timeout=1.0)
    client.send_i(build_interrogation(ca=ca))
    frames = client.recv_until(lambda fs: any(f.asdu and f.asdu.type_id == 0x64 and f.asdu.cot == COT_ACTIVATION_TERMINATION for f in fs), timeout=45)
    values = collect_value_map(frames, 0x0B, 0x14)
    assert_values_equal(values, {0x4002: 456})


@pytest.mark.active
def test_counter_interrogation_reads_diag_dd_value(client, ca, diag):
    expected = {
        0x6401: 123456,
        0x6404: 654321,
        0x6500: 234567,
        0x6600: 345678,
    }
    for ioa, value in expected.items():
        diag("set-dd --ioa %d --value %d" % (ioa, value))
    client.drain(timeout=1.0)
    client.send_i(build_counter_interrogation(ca=ca))
    frames = client.recv_until(lambda fs: any(f.asdu and f.asdu.type_id == 0x65 and f.asdu.cot == COT_ACTIVATION_TERMINATION for f in fs), timeout=15)
    assert_all_vsq_match_payload(frames)
    assert_has_asdu(frames, 0x65, COT_ACTIVATION_TERMINATION, ca)
    values = collect_value_map(frames, 0x0F, COT_REQUEST)
    assert_values_equal(values, expected)


@pytest.mark.active
def test_interrogations_return_diag_modified_yx_yc_dd_values(client, ca, diag, pytestconfig):
    seed = pytestconfig.getoption("--diag-random-seed")
    rounds = pytestconfig.getoption("--diag-random-rounds")
    points_per_type = pytestconfig.getoption("--diag-random-points")
    rng = random.Random(seed)

    assert rounds > 0
    assert 1 <= points_per_type <= 512

    for round_index in range(rounds):
        yx_ioas = rng.sample(range(0x0001, 0x1001), points_per_type)
        yc_ioas = rng.sample(range(0x4001, 0x5001), points_per_type)
        dd_ioas = rng.sample(range(0x6401, 0x6601), points_per_type)

        yx_expected = {}
        yc_expected = {}
        dd_expected = {}

        for index, ioa in enumerate(yx_ioas):
            value = ((round_index + index) % 2) == 0
            diag("set-yx --ioa %d --value %d" % (ioa, 1 if value else 0))
            yx_expected[ioa] = value

        for index, ioa in enumerate(yc_ioas):
            value = (round_index + 1) * 1000 + index * 17
            if index % 2:
                value = -value
            diag("set-yc --ioa %d --value %d" % (ioa, value))
            yc_expected[ioa] = value

        for index, ioa in enumerate(dd_ioas):
            value = (round_index + 1) * 100000 + index * 1234
            if index % 2:
                value = -value
            diag("set-dd --ioa %d --value %d" % (ioa, value))
            dd_expected[ioa] = value

        client.drain(timeout=1.0)
        client.send_i(build_interrogation(ca=ca))
        gi_frames = client.recv_until(
            lambda fs: any(
                f.asdu and f.asdu.type_id == 0x64 and f.asdu.cot == COT_ACTIVATION_TERMINATION
                for f in fs
            ),
            timeout=45,
        )
        assert_all_vsq_match_payload(gi_frames)
        assert_has_asdu(gi_frames, 0x64, COT_ACTIVATION_TERMINATION, ca)

        yx_values = collect_value_map(gi_frames, 0x01, COT_INTERROGATED_BY_STATION)
        yc_values = collect_value_map(gi_frames, 0x0B, COT_INTERROGATED_BY_STATION)
        try:
            assert_values_equal(yx_values, yx_expected)
            assert_values_equal(yc_values, yc_expected)
        except AssertionError as exc:
            raise AssertionError(
                "GI readback mismatch seed=%d round=%d yx_expected=%r yc_expected=%r: %s"
                % (seed, round_index + 1, yx_expected, yc_expected, exc)
            )

        client.drain(timeout=2.0)
        client.send_i(build_counter_interrogation(ca=ca))
        ci_frames = client.recv_until(
            lambda fs: any(
                f.asdu and f.asdu.type_id == 0x65 and f.asdu.cot == COT_ACTIVATION_TERMINATION
                for f in fs
            ),
            timeout=15,
        )
        assert_all_vsq_match_payload(ci_frames)
        assert_has_asdu(ci_frames, 0x65, COT_ACTIVATION_TERMINATION, ca)

        dd_values = collect_value_map(ci_frames, 0x0F, COT_REQUEST)
        try:
            assert_values_equal(dd_values, dd_expected)
        except AssertionError as exc:
            raise AssertionError(
                "CI readback mismatch seed=%d round=%d dd_expected=%r: %s"
                % (seed, round_index + 1, dd_expected, exc)
            )
