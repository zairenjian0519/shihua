# IEC104 Slave 自动化测试报告

## 1. 测试概况

| 项目 | 值 |
| --- | --- |
| 开始时间 | 2026-06-12 14:58:51 |
| 结束时间 | 2026-06-12 15:06:20 |
| 退出码 | 0 |
| Slave 地址 | 192.168.159.209:2404 |
| 公共地址 CA | 1 |
| 总用例数 | 34 |
| 通过 | 31 |
| 失败 | 0 |
| 跳过 | 0 |
| 预期失败 xfail | 3 |
| 意外通过 xpass | 0 |
| 用例执行耗时合计 | 390.964 秒 |
| 诊断随机种子 | 1042026 |
| 诊断随机轮数 | 10 |
| 每轮每类随机点数 | 50 |

## 2. 结果汇总

| 用例 | 中文名称 | 标记 | 结果 | 耗时(s) |
| --- | --- | --- | --- | --- |
| `test_active_upload.py::test_active_yc_upload_uses_timed_measured_value` | 主动遥测带时标上送 | active | 通过 | 0.142 |
| `test_active_upload.py::test_active_yx_upload_does_not_send_untimed_yx` | 主动遥信不应发送无时标帧 | active, xfail | 预期失败 | 3.179 |
| `test_active_upload.py::test_active_yx_upload_has_soe_with_timestamp` | 主动遥信 SOE 带时标上送 | active | 通过 | 0.616 |
| `test_active_upload.py::test_counter_interrogation_reads_diag_dd_value` | 电度召唤回读诊断修改电度值 | active | 通过 | 0.830 |
| `test_active_upload.py::test_general_interrogation_reads_diag_yc_value` | 总召回读诊断修改遥测值 | active | 通过 | 19.231 |
| `test_active_upload.py::test_history_soe_call_0x94` | 历史 SOE 召唤 | active | 通过 | 0.940 |
| `test_active_upload.py::test_interrogations_return_diag_modified_yx_yc_dd_values` | 随机多轮诊断写入与召唤回读一致性 | active | 通过 | 266.534 |
| `test_control_setpoint.py::test_setpoint_scaled_out_of_range_ioa` | 越界遥调拒绝 | negative | 通过 | 0.011 |
| `test_control_setpoint.py::test_setpoint_scaled_select_execute` | 标度化遥调选择执行 | standard | 通过 | 0.052 |
| `test_control_setpoint.py::test_single_command_direct_execute_rejection_sets_negative` | 遥控直接执行拒绝 PN 位 | negative, xfail | 预期失败 | 0.012 |
| `test_control_setpoint.py::test_single_command_out_of_range` | 越界遥控拒绝 | negative | 通过 | 0.011 |
| `test_control_setpoint.py::test_single_command_select_execute` | 单点遥控选择执行 | standard | 通过 | 0.065 |
| `test_custom_services.py::test_all_dynagram_0x91_returns_89_8a_93_then_finish` | 0x91 全部功图召唤 | custom | 通过 | 1.070 |
| `test_custom_services.py::test_custom_word_range_calls[active-power]` | 自定义 word 区间召唤 | custom, parametrize | 通过 | 0.407 |
| `test_custom_services.py::test_custom_word_range_calls[harmonic]` | 自定义 word 区间召唤 | custom, parametrize | 通过 | 0.309 |
| `test_custom_services.py::test_custom_word_range_calls[injection]` | 自定义 word 区间召唤 | custom, parametrize | 通过 | 0.213 |
| `test_custom_services.py::test_custom_word_range_calls[meter-truck]` | 自定义 word 区间召唤 | custom, parametrize | 通过 | 0.204 |
| `test_custom_services.py::test_custom_word_range_calls[reserved-sensor]` | 自定义 word 区间召唤 | custom, parametrize | 通过 | 0.157 |
| `test_custom_services.py::test_custom_word_range_calls[rtu-param]` | 自定义 word 区间召唤 | custom, parametrize | 通过 | 0.107 |
| `test_custom_services.py::test_custom_word_range_calls[sensor-param]` | 自定义 word 区间召唤 | custom, parametrize | 通过 | 3.642 |
| `test_custom_services.py::test_custom_word_range_calls[wellhead-pressure]` | 自定义 word 区间召唤 | custom, parametrize | 通过 | 0.263 |
| `test_custom_services.py::test_dynagram_0x89_has_cp56_tail` | 0x89 示功图带时标尾部 | custom | 通过 | 0.515 |
| `test_custom_services.py::test_history_call_0x8b_finishes` | 0x8B 历史数据召唤结束 | custom | 通过 | 0.012 |
| `test_custom_services.py::test_measure_total_call_0x88` | 0x88 测量总召 | custom | 通过 | 15.822 |
| `test_link.py::test_i_frame_sequence_increments` | I 帧发送序号递增 | link | 通过 | 18.973 |
| `test_link.py::test_startdt_testfr_stopdt` | STARTDT/TESTFR/STOPDT 链路流程 | link | 通过 | 0.019 |
| `test_negative.py::test_custom_call_wrong_cot` | 自定义召唤错误 COT | negative | 通过 | 0.001 |
| `test_negative.py::test_unknown_type_id` | 未知 TypeID 异常响应 | negative | 通过 | 0.001 |
| `test_standard_services.py::test_clock_sync_ack` | 校时确认 | standard | 通过 | 0.001 |
| `test_standard_services.py::test_counter_interrogation_flow` | 电度召唤流程和地址覆盖 | standard | 通过 | 0.564 |
| `test_standard_services.py::test_general_interrogation_does_not_return_custom_or_counter_ranges` | 标准总召不应夹带扩展数据 | standard, xfail | 预期失败 | 18.982 |
| `test_standard_services.py::test_general_interrogation_flow` | 标准总召流程 | standard | 通过 | 19.029 |
| `test_standard_services.py::test_general_interrogation_returns_all_demo_yx_yc_and_values` | 标准总召地址覆盖和关键值 | standard | 通过 | 19.049 |
| `test_standard_services.py::test_general_interrogation_wrong_ca` | 错误 CA 总召异常响应 | standard | 通过 | 0.001 |

## 3. 用例详情

### 3.1 主动遥测带时标上送

- 用例节点：`test_active_upload.py::test_active_yc_upload_uses_timed_measured_value`
- pytest 函数：`test_active_yc_upload_uses_timed_measured_value`
- 标记：active
- 结果：通过
- 耗时：0.142 秒

测试目的：

验证诊断修改遥测后，主动上送使用带时标遥测 TypeID=0x23 且值正确。

测试步骤：

- 通过 iec104_diag 写入 YC 0x4001=321
- 触发 active-upload notify
- 等待 COT=0x03 的 0x23/0x25 主动遥测

检查点：

- 收到 0x23/COT=0x03
- 起始 IOA 为 0x4001
- 载荷满足带 CP56Time2a 格式
- 解析值 0x4001=321

### 3.2 主动遥信不应发送无时标帧

- 用例节点：`test_active_upload.py::test_active_yx_upload_does_not_send_untimed_yx`
- pytest 函数：`test_active_yx_upload_does_not_send_untimed_yx`
- 标记：active, xfail
- 结果：预期失败
- 耗时：3.179 秒

测试目的：

验证主动遥信变位只应发送带时标 SOE，不应额外发送 0x01 无时标遥信。

测试步骤：

- 通过 iec104_diag 写入 YX 6=1
- 触发 active-upload notify
- 收集 3 秒内主动上送帧

检查点：

- 不出现 0x01/COT=0x03 无时标主动遥信

xfail 原因：

当前实现主动 YX 仍会先发无时标 0x01，再发 0x1E SOE

失败/跳过详情：

```text
client = <iec104_auto.iec104_client.IEC104Client object at 0x0000016671E62248>, ca = 1, diag = <function diag.<locals>.run at 0x0000016671E640D8>

    @pytest.mark.active
    @pytest.mark.xfail(reason="当前实现主动 YX 仍会先发无时标 0x01，再发 0x1E SOE")
    def test_active_yx_upload_does_not_send_untimed_yx(client, ca, diag):
        diag("set-yx --ioa 6 --value 0")
        client.drain(timeout=1.0)
        diag("set-yx --ioa 6 --value 1")
        diag("active-upload notify")
        frames = client.recv_for(3)
>       assert not any(f.asdu and f.asdu.type_id == 0x01 and f.asdu.cot == COT_SPONTANEOUS for f in frames)
E       assert not True
E        +  where True = any(<generator object test_active_yx_upload_does_not_send_untimed_yx.<locals>.<genexpr> at 0x00000166718ED748>)

tests\iec104_auto\test_active_upload.py:82: AssertionError
```

### 3.3 主动遥信 SOE 带时标上送

- 用例节点：`test_active_upload.py::test_active_yx_upload_has_soe_with_timestamp`
- pytest 函数：`test_active_yx_upload_has_soe_with_timestamp`
- 标记：active
- 结果：通过
- 耗时：0.616 秒

测试目的：

验证诊断修改遥信后，主动上送包含带 CP56Time2a 的 SOE TypeID=0x1E。

测试步骤：

- 通过 iec104_diag 写入 YX 5=1
- 触发 active-upload notify
- 等待 0x1E/COT=0x03

检查点：

- 收到 0x1E/COT=0x03
- 信息对象包含 IOA=5
- 载荷长度满足带时标遥信格式
- 解析值 5=True

### 3.4 电度召唤回读诊断修改电度值

- 用例节点：`test_active_upload.py::test_counter_interrogation_reads_diag_dd_value`
- pytest 函数：`test_counter_interrogation_reads_diag_dd_value`
- 标记：active
- 结果：通过
- 耗时：0.830 秒

测试目的：

验证 iec104_diag 修改多个电度点后，电度召唤应答返回修改后的电度值。

测试步骤：

- 通过 iec104_diag 写入 DD 0x6401/0x6404/0x6500/0x6600
- 发送电度召唤 0x65
- 解析 0x0F/COT=0x05

检查点：

- 收到 0x65/COT=0x0A 结束
- 电度数据中多个边界和中间点均等于诊断写入值

### 3.5 总召回读诊断修改遥测值

- 用例节点：`test_active_upload.py::test_general_interrogation_reads_diag_yc_value`
- pytest 函数：`test_general_interrogation_reads_diag_yc_value`
- 标记：active
- 结果：通过
- 耗时：19.231 秒

测试目的：

验证 iec104_diag 修改遥测后，总召应答返回修改后的遥测值。

测试步骤：

- 通过 iec104_diag 写入 YC 0x4002=456
- 发送标准总召 0x64
- 解析 0x0B/COT=0x14

检查点：

- 总召遥测中 0x4002=456

### 3.6 历史 SOE 召唤

- 用例节点：`test_active_upload.py::test_history_soe_call_0x94`
- pytest 函数：`test_history_soe_call_0x94`
- 标记：active
- 结果：通过
- 耗时：0.940 秒

测试目的：

验证添加历史 SOE 后，0x94 历史 SOE 召唤能够返回数据并正常结束。

测试步骤：

- 通过 iec104_diag 添加 SOE IOA=7
- 发送 0x94 召唤
- 接收直到 0x94/COT=0x0A

检查点：

- 收到 0x94/COT=0x05 历史 SOE 数据
- 收到 0x94/COT=0x0A 结束

### 3.7 随机多轮诊断写入与召唤回读一致性

- 用例节点：`test_active_upload.py::test_interrogations_return_diag_modified_yx_yc_dd_values`
- pytest 函数：`test_interrogations_return_diag_modified_yx_yc_dd_values`
- 标记：active
- 结果：通过
- 耗时：266.534 秒

测试目的：

随机挑选遥信、遥测、电度点，通过 iec104_diag 写入特定值，多轮总召/电度召唤回读比较。

测试步骤：

- 每轮随机挑选遥信、遥测、电度点
- 通过 set-yx/set-yc/set-dd 写入本轮特定值
- 发送标准总召并校验遥信/遥测
- 发送电度召唤并校验电度
- 所有轮次均成功才通过

检查点：

- 默认 10 轮
- 默认每轮每类 50 点
- VSQ 与载荷对象数量一致
- 所有随机点回读值等于写入值

### 3.8 越界遥调拒绝

- 用例节点：`test_control_setpoint.py::test_setpoint_scaled_out_of_range_ioa`
- pytest 函数：`test_setpoint_scaled_out_of_range_ioa`
- 标记：negative
- 结果：通过
- 耗时：0.011 秒

测试目的：

验证遥调 IOA 越界时返回结束帧表示拒绝。

测试步骤：

- 对 IOA=0x6200 发送遥调选择

检查点：

- 收到 0x31/COT=0x0A

### 3.9 标度化遥调选择执行

- 用例节点：`test_control_setpoint.py::test_setpoint_scaled_select_execute`
- pytest 函数：`test_setpoint_scaled_select_execute`
- 标记：standard
- 结果：通过
- 耗时：0.052 秒

测试目的：

验证 0x31 遥调选择和执行流程。

测试步骤：

- 发送 IOA=0x6201 value=100 选择
- 发送同一 IOA 执行

检查点：

- 收到 0x31/COT=0x07
- 收到 0x31/COT=0x0A

### 3.10 遥控直接执行拒绝 PN 位

- 用例节点：`test_control_setpoint.py::test_single_command_direct_execute_rejection_sets_negative`
- pytest 函数：`test_single_command_direct_execute_rejection_sets_negative`
- 标记：negative, xfail
- 结果：预期失败
- 耗时：0.012 秒

测试目的：

验证未选择直接执行遥控时，拒绝响应应置 PN/negative。

测试步骤：

- 对 IOA=0x6002 直接发送遥控执行

检查点：

- 收到 0x2D/COT=0x0A
- PN/negative 为 true

xfail 原因：

当前拒绝遥控/遥调返回 0x0A 但 negative=false，需与主站兼容性确认

失败/跳过详情：

```text
client = <iec104_auto.iec104_client.IEC104Client object at 0x000001667183B5C8>, ca = 1

    @pytest.mark.negative
    @pytest.mark.xfail(reason="当前拒绝遥控/遥调返回 0x0A 但 negative=false，需与主站兼容性确认")
    def test_single_command_direct_execute_rejection_sets_negative(client, ca):
        client.send_i(build_single_command(ca, 0x6002, state=1, select=False))
        frames = client.recv_until(lambda fs: any(f.asdu and f.asdu.type_id == 0x2D and f.asdu.cot == COT_ACTIVATION_TERMINATION for f in fs), timeout=3)
        asdu = assert_has_asdu(frames, 0x2D, COT_ACTIVATION_TERMINATION, ca)
>       assert asdu.negative
E       AssertionError: assert False
E        +  where False = Asdu(type_id=45, vsq=1, cot_raw=10, cot=10, negative=False, test=False, oa=0, ca=1, payload=b'\x02`\x00\x01').negative

tests\iec104_auto\test_control_setpoint.py:66: AssertionError
```

### 3.11 越界遥控拒绝

- 用例节点：`test_control_setpoint.py::test_single_command_out_of_range`
- pytest 函数：`test_single_command_out_of_range`
- 标记：negative
- 结果：通过
- 耗时：0.011 秒

测试目的：

验证遥控 IOA 越界时返回结束帧表示拒绝。

测试步骤：

- 对 IOA=0x6000 发送遥控选择

检查点：

- 收到 0x2D/COT=0x0A

### 3.12 单点遥控选择执行

- 用例节点：`test_control_setpoint.py::test_single_command_select_execute`
- pytest 函数：`test_single_command_select_execute`
- 标记：standard
- 结果：通过
- 耗时：0.065 秒

测试目的：

验证 0x2D 遥控选择和执行流程。

测试步骤：

- 发送 IOA=0x6001 选择命令
- 发送同一 IOA 执行命令

检查点：

- 收到 0x2D/COT=0x07
- 收到 0x2D/COT=0x0A

### 3.13 0x91 全部功图召唤

- 用例节点：`test_custom_services.py::test_all_dynagram_0x91_returns_89_8a_93_then_finish`
- pytest 函数：`test_all_dynagram_0x91_returns_89_8a_93_then_finish`
- 标记：custom
- 结果：通过
- 耗时：1.070 秒

测试目的：

验证全部功图召唤返回示功图、电功图和井口回压数据并结束。

测试步骤：

- 发送 0x91 召唤
- 接收直到 0x91/COT=0x0A

检查点：

- 收到 0x91 确认
- 收到 0x89/0x8A/0x93 数据
- 收到 0x91 结束

### 3.14 自定义 word 区间召唤

- 用例节点：`test_custom_services.py::test_custom_word_range_calls[active-power]`
- pytest 函数：`test_custom_word_range_calls`
- 标记：custom, parametrize
- 结果：通过
- 耗时：0.407 秒

测试目的：

验证 0x8C/0x8D/0x8E/0x8F/0x90/0x92/0x93/0x95 等自定义区间召唤。

测试步骤：

- 发送参数化自定义 TypeID 召唤
- 接收确认、数据、结束
- 按 word 数组解析返回值

检查点：

- 数据起始 IOA 落在期望范围
- IOA 完整覆盖期望范围
- 关键默认值正确

### 3.15 自定义 word 区间召唤

- 用例节点：`test_custom_services.py::test_custom_word_range_calls[harmonic]`
- pytest 函数：`test_custom_word_range_calls`
- 标记：custom, parametrize
- 结果：通过
- 耗时：0.309 秒

测试目的：

验证 0x8C/0x8D/0x8E/0x8F/0x90/0x92/0x93/0x95 等自定义区间召唤。

测试步骤：

- 发送参数化自定义 TypeID 召唤
- 接收确认、数据、结束
- 按 word 数组解析返回值

检查点：

- 数据起始 IOA 落在期望范围
- IOA 完整覆盖期望范围
- 关键默认值正确

### 3.16 自定义 word 区间召唤

- 用例节点：`test_custom_services.py::test_custom_word_range_calls[injection]`
- pytest 函数：`test_custom_word_range_calls`
- 标记：custom, parametrize
- 结果：通过
- 耗时：0.213 秒

测试目的：

验证 0x8C/0x8D/0x8E/0x8F/0x90/0x92/0x93/0x95 等自定义区间召唤。

测试步骤：

- 发送参数化自定义 TypeID 召唤
- 接收确认、数据、结束
- 按 word 数组解析返回值

检查点：

- 数据起始 IOA 落在期望范围
- IOA 完整覆盖期望范围
- 关键默认值正确

### 3.17 自定义 word 区间召唤

- 用例节点：`test_custom_services.py::test_custom_word_range_calls[meter-truck]`
- pytest 函数：`test_custom_word_range_calls`
- 标记：custom, parametrize
- 结果：通过
- 耗时：0.204 秒

测试目的：

验证 0x8C/0x8D/0x8E/0x8F/0x90/0x92/0x93/0x95 等自定义区间召唤。

测试步骤：

- 发送参数化自定义 TypeID 召唤
- 接收确认、数据、结束
- 按 word 数组解析返回值

检查点：

- 数据起始 IOA 落在期望范围
- IOA 完整覆盖期望范围
- 关键默认值正确

### 3.18 自定义 word 区间召唤

- 用例节点：`test_custom_services.py::test_custom_word_range_calls[reserved-sensor]`
- pytest 函数：`test_custom_word_range_calls`
- 标记：custom, parametrize
- 结果：通过
- 耗时：0.157 秒

测试目的：

验证 0x8C/0x8D/0x8E/0x8F/0x90/0x92/0x93/0x95 等自定义区间召唤。

测试步骤：

- 发送参数化自定义 TypeID 召唤
- 接收确认、数据、结束
- 按 word 数组解析返回值

检查点：

- 数据起始 IOA 落在期望范围
- IOA 完整覆盖期望范围
- 关键默认值正确

### 3.19 自定义 word 区间召唤

- 用例节点：`test_custom_services.py::test_custom_word_range_calls[rtu-param]`
- pytest 函数：`test_custom_word_range_calls`
- 标记：custom, parametrize
- 结果：通过
- 耗时：0.107 秒

测试目的：

验证 0x8C/0x8D/0x8E/0x8F/0x90/0x92/0x93/0x95 等自定义区间召唤。

测试步骤：

- 发送参数化自定义 TypeID 召唤
- 接收确认、数据、结束
- 按 word 数组解析返回值

检查点：

- 数据起始 IOA 落在期望范围
- IOA 完整覆盖期望范围
- 关键默认值正确

### 3.20 自定义 word 区间召唤

- 用例节点：`test_custom_services.py::test_custom_word_range_calls[sensor-param]`
- pytest 函数：`test_custom_word_range_calls`
- 标记：custom, parametrize
- 结果：通过
- 耗时：3.642 秒

测试目的：

验证 0x8C/0x8D/0x8E/0x8F/0x90/0x92/0x93/0x95 等自定义区间召唤。

测试步骤：

- 发送参数化自定义 TypeID 召唤
- 接收确认、数据、结束
- 按 word 数组解析返回值

检查点：

- 数据起始 IOA 落在期望范围
- IOA 完整覆盖期望范围
- 关键默认值正确

### 3.21 自定义 word 区间召唤

- 用例节点：`test_custom_services.py::test_custom_word_range_calls[wellhead-pressure]`
- pytest 函数：`test_custom_word_range_calls`
- 标记：custom, parametrize
- 结果：通过
- 耗时：0.263 秒

测试目的：

验证 0x8C/0x8D/0x8E/0x8F/0x90/0x92/0x93/0x95 等自定义区间召唤。

测试步骤：

- 发送参数化自定义 TypeID 召唤
- 接收确认、数据、结束
- 按 word 数组解析返回值

检查点：

- 数据起始 IOA 落在期望范围
- IOA 完整覆盖期望范围
- 关键默认值正确

### 3.22 0x89 示功图带时标尾部

- 用例节点：`test_custom_services.py::test_dynagram_0x89_has_cp56_tail`
- pytest 函数：`test_dynagram_0x89_has_cp56_tail`
- 标记：custom
- 结果：通过
- 耗时：0.515 秒

测试目的：

验证示功图数据区间、关键值和 7 字节 CP56Time2a 尾部。

测试步骤：

- 发送 0x89 召唤
- 接收直到 0x89/COT=0x0A

检查点：

- IOA 在 0x5401..0x5800
- 完整覆盖区间
- 关键值正确
- 载荷包含 CP56Time2a 尾部

### 3.23 0x8B 历史数据召唤结束

- 用例节点：`test_custom_services.py::test_history_call_0x8b_finishes`
- pytest 函数：`test_history_call_0x8b_finishes`
- 标记：custom
- 结果：通过
- 耗时：0.012 秒

测试目的：

验证历史数据召唤流程能正常返回结束帧。

测试步骤：

- 发送 0x8B 历史召唤
- 接收直到 0x8B/COT=0x0A

检查点：

- 收到 0x8B/COT=0x0A

### 3.24 0x88 测量总召

- 用例节点：`test_custom_services.py::test_measure_total_call_0x88`
- pytest 函数：`test_measure_total_call_0x88`
- 标记：custom
- 结果：通过
- 耗时：15.822 秒

测试目的：

验证自定义测量总召返回遥信完整地址段，并至少包含标准遥测地址段。

测试步骤：

- 发送 0x88/COT=0x06
- 接收直到 0x88/COT=0x0A

检查点：

- 收到确认、遥信、遥测、结束
- 遥信覆盖 0x0001..0x1000
- 遥测至少包含 0x4001..0x5000
- 关键值正确

### 3.25 I 帧发送序号递增

- 用例节点：`test_link.py::test_i_frame_sequence_increments`
- pytest 函数：`test_i_frame_sequence_increments`
- 标记：link
- 结果：通过
- 耗时：18.973 秒

测试目的：

验证总召过程中 slave I 帧发送序号单调递增且不重复。

测试步骤：

- 发送总召
- 接收直到总召结束
- 提取 I 帧发送序号

检查点：

- 至少两个 I 帧
- 发送序号递增
- 发送序号不重复

### 3.26 STARTDT/TESTFR/STOPDT 链路流程

- 用例节点：`test_link.py::test_startdt_testfr_stopdt`
- pytest 函数：`test_startdt_testfr_stopdt`
- 标记：link
- 结果：通过
- 耗时：0.019 秒

测试目的：

验证 IEC104 链路启动、测试帧和停止流程。

测试步骤：

- 发送 STARTDT ACT
- 发送 TESTFR ACT
- 发送 STOPDT ACT

检查点：

- 收到 STARTDT CON
- 收到 TESTFR CON
- 收到 STOPDT CON

### 3.27 自定义召唤错误 COT

- 用例节点：`test_negative.py::test_custom_call_wrong_cot`
- pytest 函数：`test_custom_call_wrong_cot`
- 标记：negative
- 结果：通过
- 耗时：0.001 秒

测试目的：

验证自定义召唤使用错误 COT 时返回 unknown COT。

测试步骤：

- 发送 0x88/COT=0x05

检查点：

- 收到 0x88/COT=0x2D
- PN/negative 为 true

### 3.28 未知 TypeID 异常响应

- 用例节点：`test_negative.py::test_unknown_type_id`
- pytest 函数：`test_unknown_type_id`
- 标记：negative
- 结果：通过
- 耗时：0.001 秒

测试目的：

验证未知 TypeID 返回 unknown TypeID 且 PN 置位。

测试步骤：

- 发送 0xAA/COT=0x06

检查点：

- 收到 0xAA/COT=0x2C
- PN/negative 为 true

### 3.29 校时确认

- 用例节点：`test_standard_services.py::test_clock_sync_ack`
- pytest 函数：`test_clock_sync_ack`
- 标记：standard
- 结果：通过
- 耗时：0.001 秒

测试目的：

验证 0x67 校时命令确认。

测试步骤：

- 发送 0x67/COT=0x06，携带 CP56Time2a

检查点：

- 收到 0x67/COT=0x07
- 响应载荷长度满足 IOA+CP56Time2a

### 3.30 电度召唤流程和地址覆盖

- 用例节点：`test_standard_services.py::test_counter_interrogation_flow`
- pytest 函数：`test_counter_interrogation_flow`
- 标记：standard
- 结果：通过
- 耗时：0.564 秒

测试目的：

验证电度召唤确认、累计量数据、结束和地址覆盖。

测试步骤：

- 发送 0x65/COT=0x06
- 解析 0x0F/COT=0x05

检查点：

- 收到确认、数据、结束
- 电度覆盖 0x6401..0x6600

### 3.31 标准总召不应夹带扩展数据

- 用例节点：`test_standard_services.py::test_general_interrogation_does_not_return_custom_or_counter_ranges`
- pytest 函数：`test_general_interrogation_does_not_return_custom_or_counter_ranges`
- 标记：standard, xfail
- 结果：预期失败
- 耗时：18.982 秒

测试目的：

验证标准总召中不返回电度或自定义扩展数据。

测试步骤：

- 发送标准总召
- 扫描返回 TypeID

检查点：

- 不出现 0x0F/0x89/0x8A/0x8E 等扩展数据

xfail 原因：

当前实现会在标准总召中追加功图/谐波/电度等扩展数据，需与协议口径确认

失败/跳过详情：

```text
client = <iec104_auto.iec104_client.IEC104Client object at 0x000001667183B608>, ca = 1

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
>       assert not unexpected
E       assert not [137, 137, 137, 137, 137, 137, ...]

tests\iec104_auto\test_standard_services.py:90: AssertionError
```

### 3.32 标准总召流程

- 用例节点：`test_standard_services.py::test_general_interrogation_flow`
- pytest 函数：`test_general_interrogation_flow`
- 标记：standard
- 结果：通过
- 耗时：19.029 秒

测试目的：

验证标准总召确认、遥信、遥测和结束流程。

测试步骤：

- 发送 0x64/COT=0x06
- 接收直到 0x64/COT=0x0A

检查点：

- 收到确认、遥信、遥测、结束
- IOA 范围正确
- I 帧序号连续
- VSQ 匹配载荷
- APDU 长度合法

### 3.33 标准总召地址覆盖和关键值

- 用例节点：`test_standard_services.py::test_general_interrogation_returns_all_demo_yx_yc_and_values`
- pytest 函数：`test_general_interrogation_returns_all_demo_yx_yc_and_values`
- 标记：standard
- 结果：通过
- 耗时：19.049 秒

测试目的：

验证 demo 遥信、遥测地址覆盖和关键默认值。

测试步骤：

- 发送标准总召
- 解析 0x01 和 0x0B 数据

检查点：

- 遥信覆盖 0x0001..0x1000
- 遥测包含 0x4001..0x5000
- 关键遥信/遥测值正确

### 3.34 错误 CA 总召异常响应

- 用例节点：`test_standard_services.py::test_general_interrogation_wrong_ca`
- pytest 函数：`test_general_interrogation_wrong_ca`
- 标记：standard
- 结果：通过
- 耗时：0.001 秒

测试目的：

验证错误公共地址返回 unknown CA。

测试步骤：

- 使用 ca+1 发送总召

检查点：

- 收到 0x64/COT=0x2E
- PN/negative 为 true

