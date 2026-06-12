from __future__ import annotations

from datetime import datetime
import pathlib
import time

import pytest

from .iec104_client import IEC104Client
from .remote_linux import RemoteConfig, RemoteLinux


def pytest_addoption(parser):
    parser.addoption("--slave-host", default="192.168.159.209", help="IEC104 slave host")
    parser.addoption("--port", type=int, default=2404, help="IEC104 slave TCP port")
    parser.addoption("--ca", type=int, default=1, help="IEC104 common address")
    parser.addoption("--timeout", type=float, default=3.0, help="socket timeout seconds")
    parser.addoption("--ssh-user", default="sps", help="Linux SSH username")
    parser.addoption("--ssh-pass", default="sps", help="Linux SSH password")
    parser.addoption("--project-dir", default="/home/sps/shihua/104_slave", help="remote 104_slave directory")
    parser.addoption("--manage-slave", action="store_true", help="enable SSH access for iec104_diag; assumes slave is already running")
    parser.addoption("--build-slave", action="store_true", help="build remote slave before tests")
    parser.addoption("--restart-slave", action="store_true", help="restart remote slave before tests")
    parser.addoption("--capture-pcap", action="store_true", help="capture tcpdump pcap on remote host")
    parser.addoption("--artifacts", default="artifacts/iec104_auto", help="local artifact directory")
    parser.addoption("--diag-random-seed", type=int, default=1042026, help="random seed for diag write/readback tests")
    parser.addoption("--diag-random-rounds", type=int, default=10, help="rounds for diag write/readback tests")
    parser.addoption("--diag-random-points", type=int, default=50, help="points per type in each diag write/readback round")
    parser.addoption("--report-md", default="", help="write a Chinese markdown test report to this path")


def pytest_configure(config):
    config._iec104_report_results = {}
    config._iec104_report_started_at = datetime.now()


def pytest_collection_modifyitems(config, items):
    indexed_items = list(enumerate(items))
    indexed_items.sort(key=lambda pair: (1 if pair[1].get_closest_marker("active") else 0, pair[0]))
    items[:] = [item for _, item in indexed_items]


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    if report.when not in ("setup", "call", "teardown"):
        return
    if report.when in ("setup", "teardown") and report.passed:
        return

    results = item.config._iec104_report_results
    test_name = item.originalname or item.name.split("[", 1)[0]
    result = results.setdefault(
        item.nodeid,
        {
            "nodeid": item.nodeid,
            "test_name": test_name,
            "markers": sorted(marker.name for marker in item.iter_markers()),
            "outcome": report.outcome,
            "duration": 0.0,
            "longrepr": "",
            "wasxfail": getattr(report, "wasxfail", ""),
            "when": report.when,
        },
    )
    result["outcome"] = report.outcome
    result["duration"] += getattr(report, "duration", 0.0)
    result["wasxfail"] = getattr(report, "wasxfail", "")
    result["when"] = report.when
    if report.failed:
        result["longrepr"] = str(report.longrepr)
    elif report.skipped:
        result["longrepr"] = str(report.longrepr)


def pytest_sessionfinish(session, exitstatus):
    report_path = session.config.getoption("--report-md")
    if not report_path:
        report_path = str(pathlib.Path(__file__).resolve().parent / "test_report.md")

    results = list(session.config._iec104_report_results.values())
    results.sort(key=lambda item: item["nodeid"])
    path = pathlib.Path(report_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(_build_markdown_report(session.config, results, exitstatus), encoding="utf-8")


def _status_text(result):
    if result.get("wasxfail") and result["outcome"] == "skipped":
        return "预期失败"
    if result.get("wasxfail") and result["outcome"] == "passed":
        return "意外通过"
    return {
        "passed": "通过",
        "failed": "失败",
        "skipped": "跳过",
    }.get(result["outcome"], result["outcome"])


def _format_list(items):
    return "\n".join("- %s" % item for item in items) if items else "- 未定义"


def _truncate(text, limit=1200):
    if not text:
        return ""
    if len(text) <= limit:
        return text
    head = text[: limit // 2]
    tail = text[-(limit // 2) :]
    return head + "\n...中间已截断，保留末尾根因...\n" + tail


def _build_markdown_report(config, results, exitstatus):
    counts = {"passed": 0, "failed": 0, "skipped": 0, "xfailed": 0, "xpassed": 0}
    for result in results:
        if result.get("wasxfail") and result["outcome"] == "skipped":
            counts["xfailed"] += 1
        elif result.get("wasxfail") and result["outcome"] == "passed":
            counts["xpassed"] += 1
        elif result["outcome"] in counts:
            counts[result["outcome"]] += 1

    started_at = config._iec104_report_started_at.strftime("%Y-%m-%d %H:%M:%S")
    finished_at = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    total_duration = sum(result.get("duration", 0.0) for result in results)

    lines = [
        "# IEC104 Slave 自动化测试报告",
        "",
        "## 1. 测试概况",
        "",
        "| 项目 | 值 |",
        "| --- | --- |",
        "| 开始时间 | %s |" % started_at,
        "| 结束时间 | %s |" % finished_at,
        "| 退出码 | %s |" % int(exitstatus),
        "| Slave 地址 | %s:%s |" % (config.getoption("--slave-host"), config.getoption("--port")),
        "| 公共地址 CA | %s |" % config.getoption("--ca"),
        "| 总用例数 | %d |" % len(results),
        "| 通过 | %d |" % counts["passed"],
        "| 失败 | %d |" % counts["failed"],
        "| 跳过 | %d |" % counts["skipped"],
        "| 预期失败 xfail | %d |" % counts["xfailed"],
        "| 意外通过 xpass | %d |" % counts["xpassed"],
        "| 用例执行耗时合计 | %.3f 秒 |" % total_duration,
        "| 诊断随机种子 | %s |" % config.getoption("--diag-random-seed"),
        "| 诊断随机轮数 | %s |" % config.getoption("--diag-random-rounds"),
        "| 每轮每类随机点数 | %s |" % config.getoption("--diag-random-points"),
        "",
        "## 2. 结果汇总",
        "",
        "| 用例 | 中文名称 | 标记 | 结果 | 耗时(s) |",
        "| --- | --- | --- | --- | --- |",
    ]

    for result in results:
        meta = TEST_CASES.get(result["test_name"], {})
        lines.append(
            "| `%s` | %s | %s | %s | %.3f |"
            % (
                result["nodeid"],
                meta.get("name", result["test_name"]),
                ", ".join(result["markers"]),
                _status_text(result),
                result.get("duration", 0.0),
            )
        )

    lines.extend(["", "## 3. 用例详情", ""])

    for index, result in enumerate(results, 1):
        meta = TEST_CASES.get(result["test_name"], {})
        lines.extend(
            [
                "### 3.%d %s" % (index, meta.get("name", result["test_name"])),
                "",
                "- 用例节点：`%s`" % result["nodeid"],
                "- pytest 函数：`%s`" % result["test_name"],
                "- 标记：%s" % (", ".join(result["markers"]) or "无"),
                "- 结果：%s" % _status_text(result),
                "- 耗时：%.3f 秒" % result.get("duration", 0.0),
                "",
                "测试目的：",
                "",
                meta.get("purpose", "未定义"),
                "",
                "测试步骤：",
                "",
                _format_list(meta.get("steps", [])),
                "",
                "检查点：",
                "",
                _format_list(meta.get("checks", [])),
                "",
            ]
        )
        if result.get("wasxfail"):
            lines.extend(["xfail 原因：", "", str(result["wasxfail"]), ""])
        if result.get("longrepr"):
            lines.extend(["失败/跳过详情：", "", "```text", _truncate(result["longrepr"]), "```", ""])

    return "\n".join(lines) + "\n"


TEST_CASES = {
    "test_active_yc_upload_uses_timed_measured_value": {
        "name": "主动遥测带时标上送",
        "purpose": "验证诊断修改遥测后，主动上送使用带时标遥测 TypeID=0x23 且值正确。",
        "steps": ["通过 iec104_diag 写入 YC 0x4001=321", "触发 active-upload notify", "等待 COT=0x03 的 0x23/0x25 主动遥测"],
        "checks": ["收到 0x23/COT=0x03", "起始 IOA 为 0x4001", "载荷满足带 CP56Time2a 格式", "解析值 0x4001=321"],
    },
    "test_active_yx_upload_has_soe_with_timestamp": {
        "name": "主动遥信 SOE 带时标上送",
        "purpose": "验证诊断修改遥信后，主动上送包含带 CP56Time2a 的 SOE TypeID=0x1E。",
        "steps": ["通过 iec104_diag 写入 YX 5=1", "触发 active-upload notify", "等待 0x1E/COT=0x03"],
        "checks": ["收到 0x1E/COT=0x03", "信息对象包含 IOA=5", "载荷长度满足带时标遥信格式", "解析值 5=True"],
    },
    "test_active_yx_upload_does_not_send_untimed_yx": {
        "name": "主动遥信不应发送无时标帧",
        "purpose": "验证主动遥信变位只应发送带时标 SOE，不应额外发送 0x01 无时标遥信。",
        "steps": ["通过 iec104_diag 写入 YX 6=1", "触发 active-upload notify", "收集 3 秒内主动上送帧"],
        "checks": ["不出现 0x01/COT=0x03 无时标主动遥信"],
    },
    "test_history_soe_call_0x94": {
        "name": "历史 SOE 召唤",
        "purpose": "验证添加历史 SOE 后，0x94 历史 SOE 召唤能够返回数据并正常结束。",
        "steps": ["通过 iec104_diag 添加 SOE IOA=7", "发送 0x94 召唤", "接收直到 0x94/COT=0x0A"],
        "checks": ["收到 0x94/COT=0x05 历史 SOE 数据", "收到 0x94/COT=0x0A 结束"],
    },
    "test_general_interrogation_reads_diag_yc_value": {
        "name": "总召回读诊断修改遥测值",
        "purpose": "验证 iec104_diag 修改遥测后，总召应答返回修改后的遥测值。",
        "steps": ["通过 iec104_diag 写入 YC 0x4002=456", "发送标准总召 0x64", "解析 0x0B/COT=0x14"],
        "checks": ["总召遥测中 0x4002=456"],
    },
    "test_counter_interrogation_reads_diag_dd_value": {
        "name": "电度召唤回读诊断修改电度值",
        "purpose": "验证 iec104_diag 修改多个电度点后，电度召唤应答返回修改后的电度值。",
        "steps": ["通过 iec104_diag 写入 DD 0x6401/0x6404/0x6500/0x6600", "发送电度召唤 0x65", "解析 0x0F/COT=0x05"],
        "checks": ["收到 0x65/COT=0x0A 结束", "电度数据中多个边界和中间点均等于诊断写入值"],
    },
    "test_interrogations_return_diag_modified_yx_yc_dd_values": {
        "name": "随机多轮诊断写入与召唤回读一致性",
        "purpose": "随机挑选遥信、遥测、电度点，通过 iec104_diag 写入特定值，多轮总召/电度召唤回读比较。",
        "steps": ["每轮随机挑选遥信、遥测、电度点", "通过 set-yx/set-yc/set-dd 写入本轮特定值", "发送标准总召并校验遥信/遥测", "发送电度召唤并校验电度", "所有轮次均成功才通过"],
        "checks": ["默认 10 轮", "默认每轮每类 50 点", "VSQ 与载荷对象数量一致", "所有随机点回读值等于写入值"],
    },
    "test_single_command_select_execute": {
        "name": "单点遥控选择执行",
        "purpose": "验证 0x2D 遥控选择和执行流程。",
        "steps": ["发送 IOA=0x6001 选择命令", "发送同一 IOA 执行命令"],
        "checks": ["收到 0x2D/COT=0x07", "收到 0x2D/COT=0x0A"],
    },
    "test_single_command_out_of_range": {
        "name": "越界遥控拒绝",
        "purpose": "验证遥控 IOA 越界时返回结束帧表示拒绝。",
        "steps": ["对 IOA=0x6000 发送遥控选择"],
        "checks": ["收到 0x2D/COT=0x0A"],
    },
    "test_setpoint_scaled_select_execute": {
        "name": "标度化遥调选择执行",
        "purpose": "验证 0x31 遥调选择和执行流程。",
        "steps": ["发送 IOA=0x6201 value=100 选择", "发送同一 IOA 执行"],
        "checks": ["收到 0x31/COT=0x07", "收到 0x31/COT=0x0A"],
    },
    "test_setpoint_scaled_out_of_range_ioa": {
        "name": "越界遥调拒绝",
        "purpose": "验证遥调 IOA 越界时返回结束帧表示拒绝。",
        "steps": ["对 IOA=0x6200 发送遥调选择"],
        "checks": ["收到 0x31/COT=0x0A"],
    },
    "test_single_command_direct_execute_rejection_sets_negative": {
        "name": "遥控直接执行拒绝 PN 位",
        "purpose": "验证未选择直接执行遥控时，拒绝响应应置 PN/negative。",
        "steps": ["对 IOA=0x6002 直接发送遥控执行"],
        "checks": ["收到 0x2D/COT=0x0A", "PN/negative 为 true"],
    },
    "test_measure_total_call_0x88": {
        "name": "0x88 测量总召",
        "purpose": "验证自定义测量总召返回遥信完整地址段，并至少包含标准遥测地址段。",
        "steps": ["发送 0x88/COT=0x06", "接收直到 0x88/COT=0x0A"],
        "checks": ["收到确认、遥信、遥测、结束", "遥信覆盖 0x0001..0x1000", "遥测至少包含 0x4001..0x5000", "关键值正确"],
    },
    "test_custom_word_range_calls": {
        "name": "自定义 word 区间召唤",
        "purpose": "验证 0x8C/0x8D/0x8E/0x8F/0x90/0x92/0x93/0x95 等自定义区间召唤。",
        "steps": ["发送参数化自定义 TypeID 召唤", "接收确认、数据、结束", "按 word 数组解析返回值"],
        "checks": ["数据起始 IOA 落在期望范围", "IOA 完整覆盖期望范围", "关键默认值正确"],
    },
    "test_dynagram_0x89_has_cp56_tail": {
        "name": "0x89 示功图带时标尾部",
        "purpose": "验证示功图数据区间、关键值和 7 字节 CP56Time2a 尾部。",
        "steps": ["发送 0x89 召唤", "接收直到 0x89/COT=0x0A"],
        "checks": ["IOA 在 0x5401..0x5800", "完整覆盖区间", "关键值正确", "载荷包含 CP56Time2a 尾部"],
    },
    "test_all_dynagram_0x91_returns_89_8a_93_then_finish": {
        "name": "0x91 全部功图召唤",
        "purpose": "验证全部功图召唤返回示功图、电功图和井口回压数据并结束。",
        "steps": ["发送 0x91 召唤", "接收直到 0x91/COT=0x0A"],
        "checks": ["收到 0x91 确认", "收到 0x89/0x8A/0x93 数据", "收到 0x91 结束"],
    },
    "test_history_call_0x8b_finishes": {
        "name": "0x8B 历史数据召唤结束",
        "purpose": "验证历史数据召唤流程能正常返回结束帧。",
        "steps": ["发送 0x8B 历史召唤", "接收直到 0x8B/COT=0x0A"],
        "checks": ["收到 0x8B/COT=0x0A"],
    },
    "test_startdt_testfr_stopdt": {
        "name": "STARTDT/TESTFR/STOPDT 链路流程",
        "purpose": "验证 IEC104 链路启动、测试帧和停止流程。",
        "steps": ["发送 STARTDT ACT", "发送 TESTFR ACT", "发送 STOPDT ACT"],
        "checks": ["收到 STARTDT CON", "收到 TESTFR CON", "收到 STOPDT CON"],
    },
    "test_i_frame_sequence_increments": {
        "name": "I 帧发送序号递增",
        "purpose": "验证总召过程中 slave I 帧发送序号单调递增且不重复。",
        "steps": ["发送总召", "接收直到总召结束", "提取 I 帧发送序号"],
        "checks": ["至少两个 I 帧", "发送序号递增", "发送序号不重复"],
    },
    "test_unknown_type_id": {
        "name": "未知 TypeID 异常响应",
        "purpose": "验证未知 TypeID 返回 unknown TypeID 且 PN 置位。",
        "steps": ["发送 0xAA/COT=0x06"],
        "checks": ["收到 0xAA/COT=0x2C", "PN/negative 为 true"],
    },
    "test_custom_call_wrong_cot": {
        "name": "自定义召唤错误 COT",
        "purpose": "验证自定义召唤使用错误 COT 时返回 unknown COT。",
        "steps": ["发送 0x88/COT=0x05"],
        "checks": ["收到 0x88/COT=0x2D", "PN/negative 为 true"],
    },
    "test_general_interrogation_flow": {
        "name": "标准总召流程",
        "purpose": "验证标准总召确认、遥信、遥测和结束流程。",
        "steps": ["发送 0x64/COT=0x06", "接收直到 0x64/COT=0x0A"],
        "checks": ["收到确认、遥信、遥测、结束", "IOA 范围正确", "I 帧序号连续", "VSQ 匹配载荷", "APDU 长度合法"],
    },
    "test_general_interrogation_returns_all_demo_yx_yc_and_values": {
        "name": "标准总召地址覆盖和关键值",
        "purpose": "验证 demo 遥信、遥测地址覆盖和关键默认值。",
        "steps": ["发送标准总召", "解析 0x01 和 0x0B 数据"],
        "checks": ["遥信覆盖 0x0001..0x1000", "遥测包含 0x4001..0x5000", "关键遥信/遥测值正确"],
    },
    "test_general_interrogation_does_not_return_custom_or_counter_ranges": {
        "name": "标准总召不应夹带扩展数据",
        "purpose": "验证标准总召中不返回电度或自定义扩展数据。",
        "steps": ["发送标准总召", "扫描返回 TypeID"],
        "checks": ["不出现 0x0F/0x89/0x8A/0x8E 等扩展数据"],
    },
    "test_general_interrogation_wrong_ca": {
        "name": "错误 CA 总召异常响应",
        "purpose": "验证错误公共地址返回 unknown CA。",
        "steps": ["使用 ca+1 发送总召"],
        "checks": ["收到 0x64/COT=0x2E", "PN/negative 为 true"],
    },
    "test_counter_interrogation_flow": {
        "name": "电度召唤流程和地址覆盖",
        "purpose": "验证电度召唤确认、累计量数据、结束和地址覆盖。",
        "steps": ["发送 0x65/COT=0x06", "解析 0x0F/COT=0x05"],
        "checks": ["收到确认、数据、结束", "电度覆盖 0x6401..0x6600"],
    },
    "test_clock_sync_ack": {
        "name": "校时确认",
        "purpose": "验证 0x67 校时命令确认。",
        "steps": ["发送 0x67/COT=0x06，携带 CP56Time2a"],
        "checks": ["收到 0x67/COT=0x07", "响应载荷长度满足 IOA+CP56Time2a"],
    },
}


@pytest.fixture(scope="session")
def ca(pytestconfig) -> int:
    return int(pytestconfig.getoption("--ca"))


@pytest.fixture(scope="session")
def slave_host(pytestconfig) -> str:
    return str(pytestconfig.getoption("--slave-host"))


@pytest.fixture(scope="session")
def slave_port(pytestconfig) -> int:
    return int(pytestconfig.getoption("--port"))


@pytest.fixture(scope="session")
def remote(pytestconfig):
    markexpr = getattr(pytestconfig.option, "markexpr", "") or ""
    needs_remote = (
        pytestconfig.getoption("--manage-slave")
        or pytestconfig.getoption("--build-slave")
        or pytestconfig.getoption("--restart-slave")
        or pytestconfig.getoption("--capture-pcap")
        or "active" in markexpr
        or "slow" in markexpr
    )
    if not needs_remote:
        yield None
        return

    cfg = RemoteConfig(
        host=pytestconfig.getoption("--slave-host"),
        user=pytestconfig.getoption("--ssh-user"),
        password=pytestconfig.getoption("--ssh-pass"),
        project_dir=pytestconfig.getoption("--project-dir"),
    )
    try:
        remote = RemoteLinux(cfg)
    except RuntimeError as exc:
        pytest.skip(str(exc))
    if pytestconfig.getoption("--build-slave"):
        remote.build()
    if pytestconfig.getoption("--restart-slave"):
        remote.start_slave()
    if pytestconfig.getoption("--capture-pcap"):
        remote.start_tcpdump()
    yield remote
    if pytestconfig.getoption("--capture-pcap"):
        remote.stop_tcpdump()
        artifact_dir = pathlib.Path(pytestconfig.getoption("--artifacts"))
        try:
            remote.fetch_file("/tmp/iec104_test.pcap", artifact_dir / "iec104_test.pcap")
        except Exception:
            pass
    try:
        artifact_dir = pathlib.Path(pytestconfig.getoption("--artifacts"))
        artifact_dir.mkdir(parents=True, exist_ok=True)
        (artifact_dir / "slave_run.log").write_text(remote.fetch_text(f"{cfg.project_dir}/run.log"), encoding="utf-8")
    except Exception:
        pass
    remote.close()


@pytest.fixture
def client(pytestconfig, slave_host, slave_port):
    with IEC104Client(slave_host, slave_port, timeout=pytestconfig.getoption("--timeout")) as c:
        c.startdt()
        yield c


@pytest.fixture
def diag(remote):
    if remote is None:
        pytest.skip("diag tests require --manage-slave, --build-slave, --restart-slave, or --capture-pcap so SSH is available")
    return remote.diag


def wait_for(predicate, timeout: float = 5.0, interval: float = 0.1):
    end = time.monotonic() + timeout
    last = None
    while time.monotonic() < end:
        last = predicate()
        if last:
            return last
        time.sleep(interval)
    return last
