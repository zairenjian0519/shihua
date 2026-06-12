# IEC104 自动化测试用例使用说明

本文档说明 `tests/iec104_auto` 自动化测试框架的运行方法、各测试用例的测试方法和判定点。测试框架使用 pytest 和原始 TCP IEC104 客户端，默认连接 Linux 虚拟机上的 IEC104 slave。

## 1. 环境与默认参数

默认目标：

```text
IEC104 slave IP: 192.168.159.209
IEC104 TCP port: 2404
公共地址 CA: 1
SSH 用户/密码: sps / sps
远端工程目录: /home/sps/shihua/104_slave
```

安装基础依赖：

```powershell
py -m pip install -r tests\iec104_auto\requirements.txt
```

如果要使用 `--manage-slave`、`--build-slave`、`--restart-slave`、`--capture-pcap` 或运行 `active` 类用例，需要安装远程管理依赖：

```powershell
py -m pip install -r tests\iec104_auto\requirements-remote.txt
```

如果 Windows 上使用 Python 3.7 和较旧 pip，远程依赖文件已固定为兼容版本；仍安装失败时可先升级 pip：

```powershell
py -m pip install --upgrade "pip<25"
py -m pip install -r tests\iec104_auto\requirements-remote.txt
```

说明：

- 不带 `--manage-slave`、`--build-slave`、`--restart-slave` 或 `--capture-pcap` 时，依赖 `iec104_diag`/SSH 的主动上送和诊断注入用例不会真正执行，会被标记为 `SKIPPED`。
- 主动上送用例需要远程执行诊断命令，例如写入 YC/YX/DD、触发 `active-upload notify`，因此必须安装远程管理依赖并传入 `--manage-slave`。
- `--manage-slave` 现在只启用 SSH/`iec104_diag` 能力，默认认为 slave 已经由人工启动；不会自动编译，也不会自动重启 slave。
- 只有显式传入 `--build-slave` 时才会远程执行 cmake 构建；只有显式传入 `--restart-slave` 时才会停止并重新启动 slave。
- 如果只验证普通链路、总召唤、自定义召唤、负向响应等 TCP 协议交互，用基础依赖即可。

常用参数：

```text
--slave-host     slave 地址，默认 192.168.159.209
--port           IEC104 TCP 端口，默认 2404
--ca             公共地址，默认 1
--timeout        socket 超时，默认 3 秒
--manage-slave   通过 SSH 使用 iec104_diag，默认不编译、不重启 slave
--build-slave    通过 SSH 远程执行 cmake 构建 slave
--restart-slave  通过 SSH 停止并重新启动 slave
--capture-pcap   通过 SSH/tcpdump 抓包
--artifacts      本地测试产物目录
--diag-random-seed     诊断写入回读随机种子，默认 1042026
--diag-random-rounds   诊断写入回读轮数，默认 10
--diag-random-points   每轮每类随机点数，默认 50
--report-md            中文 Markdown 测试报告路径，默认 tests/iec104_auto/test_report.md
```

## 2. 常用执行命令

运行全部普通协议用例：

```powershell
py -m pytest tests\iec104_auto -v --slave-host 192.168.159.209 --port 2404 --ca 1
```

说明：这条命令会收集 `tests\iec104_auto` 下全部测试，但主动上送/诊断注入用例依赖 SSH 和 `iec104_diag`。如果不带 `--manage-slave`，`test_active_upload.py` 中这些用例会显示为 `SKIPPED`，其余普通协议用例会正常执行。

运行真正全部测试用例，包括主动上送、诊断写入回读：

```powershell
py -m pytest tests\iec104_auto -v --manage-slave --slave-host 192.168.159.209 --port 2404 --ca 1 --ssh-user sps --ssh-pass sps --project-dir /home/sps/shihua/104_slave
```

说明：当前 `--manage-slave` 只启用 SSH/`iec104_diag`，默认认为 slave 已经手工启动；不会自动编译，也不会自动重启 slave。

为支持不重启 slave 的一条命令全量测试，测试框架会自动做两类隔离：

- 收集阶段会把 `active` 标记的主动上送/诊断注入用例排到最后执行，避免这些用例修改寄存器后污染普通协议用例的默认值检查。
- `test_active_upload.py` 内部会记录本用例通过 `set-yx`、`set-yc`、`set-dd` 修改过的点，teardown 时按 demo 默认规则恢复这些点，便于下一次不重启继续执行。

运行真正全部测试用例并指定报告输出：

```powershell
py -m pytest tests\iec104_auto -v --manage-slave --slave-host 192.168.159.209 --port 2404 --ca 1 --ssh-user sps --ssh-pass sps --project-dir /home/sps/shihua/104_slave --report-md tests\iec104_auto\test_report.md
```

只运行非主动上送用例，不显示主动用例跳过信息：

```powershell
py -m pytest tests\iec104_auto -v -m "not active" --slave-host 192.168.159.209 --port 2404 --ca 1
```

只运行链路层用例：

```powershell
py -m pytest tests\iec104_auto -v -m "link" --slave-host 192.168.159.209 --port 2404 --ca 1
```

只运行标准服务用例：

```powershell
py -m pytest tests\iec104_auto -v -m "standard" --slave-host 192.168.159.209 --port 2404 --ca 1
```

只运行总召唤相关用例：

```powershell
py -m pytest tests\iec104_auto\test_standard_services.py -v -k "general_interrogation" --slave-host 192.168.159.209 --port 2404 --ca 1
```

只运行自定义 TypeID 用例：

```powershell
py -m pytest tests\iec104_auto -v -m "custom" --slave-host 192.168.159.209 --port 2404 --ca 1
```

只运行异常/负向用例：

```powershell
py -m pytest tests\iec104_auto -v -m "negative" --slave-host 192.168.159.209 --port 2404 --ca 1
```

运行主动上送和诊断注入用例：

```powershell
py -m pytest tests\iec104_auto -v -m "active" --manage-slave --slave-host 192.168.159.209 --ssh-user sps --ssh-pass sps --project-dir /home/sps/shihua/104_slave
```

单独运行主动遥测带时标上送用例并生成报告：

```powershell
py -m pytest tests\iec104_auto\test_active_upload.py -v -k "active_yc_upload_uses_timed_measured_value" --manage-slave --slave-host 192.168.159.209 --ssh-user sps --ssh-pass sps --project-dir /home/sps/shihua/104_slave --report-md tests\iec104_auto\active_yc_report.md
```

如果省略 `--manage-slave`，该用例会跳过，并在报告中显示类似信息：

```text
diag tests require --manage-slave, --build-slave, --restart-slave, or --capture-pcap so SSH is available
```

运行时同步抓包并保存产物：

```powershell
py -m pytest tests\iec104_auto -v --manage-slave --capture-pcap --artifacts artifacts\iec104_auto --slave-host 192.168.159.209
```

查看所有可收集用例：

```powershell
py -m pytest tests\iec104_auto --collect-only -q
```

每次测试结束后会生成中文 Markdown 测试报告，默认路径：

```text
tests/iec104_auto/test_report.md
```

也可以指定报告路径：

```powershell
py -m pytest tests\iec104_auto -v --report-md artifacts\iec104_auto\my_report.md --slave-host 192.168.159.209 --port 2404 --ca 1
```

## 3. 测试框架的通用校验

测试客户端会完成 IEC104 TCP 连接、STARTDT 激活、I 帧发送、响应接收和 APDU/ASDU 解析。

已实现的通用报文解析和校验包括：

- APDU 起始字节 `0x68` 和长度字段校验。
- I/S/U 帧类型识别。
- I 帧发送序号、接收确认序号解析。
- ASDU `TypeID`、`VSQ`、`COT`、`PN/negative`、`OA`、`CA` 解析。
- SQ=0 和 SQ=1 信息对象解析。
- `0x01` 单点遥信解析。
- `0x0B` 标度化遥测解析。
- `0x0F` 累计量/电度解析。
- `0x1E` 带 CP56Time2a 的单点遥信解析。
- `0x23` 带 CP56Time2a 的标度化遥测解析。
- `0x25` 带 CP56Time2a 的短浮点遥测解析。
- 自定义 word 数组载荷解析。
- VSQ 数量与实际载荷对象数量一致性检查。
- I 帧序号是否连续，是否存在跳号或重复。
- IOA 地址范围覆盖和越界检查。
- 关键默认值或诊断注入值检查。

注意：TCP 是字节流，测试客户端按 `0x68 + length` 做完整 APDU 拆包，不把一次 `recv()` 当成一个完整 IEC104 帧。

## 4. 链路层用例

文件：`tests/iec104_auto/test_link.py`

### 4.1 `test_startdt_testfr_stopdt`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_link.py -v -k "startdt_testfr_stopdt" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 建立 TCP 连接。
2. 发送 `STARTDT ACT`。
3. 期望收到 `STARTDT CON`。
4. 发送 `TESTFR ACT`。
5. 期望收到 `TESTFR CON`。
6. 发送 `STOPDT ACT`。
7. 期望收到 `STOPDT CON`。

判定点：

- slave 能接受连接。
- 启停数据传输流程正确。
- 测试帧响应正确。

### 4.2 `test_i_frame_sequence_increments`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_link.py -v -k "i_frame_sequence_increments" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. STARTDT 激活后发送总召唤 `TypeID=0x64, COT=0x06`。
2. 接收直到总召结束 `TypeID=0x64, COT=0x0A`。
3. 提取所有 I 帧发送序号。

判定点：

- 至少收到 2 个 I 帧。
- I 帧发送序号单调递增。
- I 帧发送序号没有重复。

## 5. 标准服务用例

文件：`tests/iec104_auto/test_standard_services.py`

### 5.1 `test_general_interrogation_flow`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_standard_services.py -v -k "test_general_interrogation_flow" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 发送总召唤 `0x64/COT=0x06`。
2. 接收直到总召结束 `0x64/COT=0x0A`。
3. 解析总召响应过程中的所有 APDU/ASDU。

期望响应：

- 总召确认 `0x64/COT=0x07`。
- 遥信总召数据 `0x01/COT=0x14`。
- 遥测总召数据 `0x0B/COT=0x14`。
- 总召结束 `0x64/COT=0x0A`。

判定点：

- 遥信 IOA 不超出 `0x0001..0x1000`。
- 遥测 IOA 不超出 `0x4001..0x5000`。
- I 帧序号没有跳号或重复。
- VSQ 数量与解析出的信息对象数量一致。
- 每个 APDU 原始长度不超过 255 字节。

### 5.2 `test_general_interrogation_returns_all_demo_yx_yc_and_values`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_standard_services.py -v -k "returns_all_demo_yx_yc_and_values" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 发送总召唤。
2. 收到总召结束后，收集所有 `0x01/COT=0x14` 和 `0x0B/COT=0x14` 对象。
3. 构建 IOA 到值的映射。

判定点：

- 遥信完整覆盖 `0x0001..0x1000`。
- 遥测至少包含 `0x4001..0x5000`。
- 关键遥信默认值正确：
  - `0x0001 = False`
  - `0x0002 = True`
  - `0x0003 = False`
  - `0x0004 = True`
- 关键遥测默认值正确：
  - `0x4001 = 10`
  - `0x4002 = 11`
  - `0x4003 = 12`
  - `0x4004 = 13`

说明：当前没有逐点校验 `0x4001..0x5000` 每个遥测值，只校验地址覆盖和关键值。

### 5.3 `test_general_interrogation_does_not_return_custom_or_counter_ranges`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_standard_services.py -v -k "does_not_return_custom_or_counter_ranges" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 发送标准总召唤。
2. 检查响应中是否出现非标准总召数据类型。

判定点：

- 不应出现 `0x0F` 电度。
- 不应出现 `0x89`、`0x8A`、`0x8E` 等扩展/自定义数据。

当前状态：

```text
xfail
```

原因：当前 slave 标准总召实现会追加功图、谐波、电度等扩展数据，需要确认协议口径或修正实现。

### 5.4 `test_general_interrogation_wrong_ca`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_standard_services.py -v -k "wrong_ca" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 使用错误 CA 发送总召唤，默认发送 `ca + 1`。
2. 等待异常响应。

判定点：

- 返回 `TypeID=0x64`。
- COT 为 `0x2E`，即 unknown CA。
- PN/negative 位为 true。

### 5.5 `test_counter_interrogation_flow`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_standard_services.py -v -k "counter_interrogation_flow" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 发送电度召唤 `0x65/COT=0x06`。
2. 接收直到电度召唤结束 `0x65/COT=0x0A`。
3. 解析 `0x0F/COT=0x05` 累计量数据。

判定点：

- 收到电度召唤确认 `0x65/COT=0x07`。
- 收到电度数据 `0x0F/COT=0x05`。
- 收到电度召唤结束 `0x65/COT=0x0A`。
- 电度 IOA 不超出 `0x6401..0x6600`。
- 电度完整覆盖 `0x6401..0x6600`。
- 该标准用例不再断言固定默认值；电度数据正确性由诊断写入回读用例验证。

### 5.6 `test_clock_sync_ack`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_standard_services.py -v -k "clock_sync_ack" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 发送校时命令 `0x67/COT=0x06`，携带 CP56Time2a。
2. 等待校时确认。

判定点：

- 返回 `0x67/COT=0x07`。
- 公共地址 CA 正确。
- 响应载荷长度满足 IOA + CP56Time2a 的基本长度要求。

## 6. 遥控和遥调用例

文件：`tests/iec104_auto/test_control_setpoint.py`

### 6.1 `test_single_command_select_execute`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_control_setpoint.py -v -k "single_command_select_execute" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 对 `IOA=0x6001` 发送单点遥控选择 `0x2D/COT=0x06`，S/E 位为选择。
2. 期望收到遥控选择确认 `0x2D/COT=0x07`。
3. 对同一 IOA 发送执行命令。
4. 期望收到遥控结束 `0x2D/COT=0x0A`。

判定点：

- 选择流程响应正确。
- 执行流程响应正确。

### 6.2 `test_single_command_out_of_range`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_control_setpoint.py -v -k "single_command_out_of_range" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 对越界遥控 IOA `0x6000` 发送选择命令。
2. 等待遥控结束帧。

判定点：

- 返回 `0x2D/COT=0x0A`，表示从站拒绝并结束本次遥控流程。
- PN/negative 位是否置位由 `test_single_command_direct_execute_rejection_sets_negative` 作为已知 xfail 单独跟踪。

### 6.3 `test_setpoint_scaled_select_execute`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_control_setpoint.py -v -k "setpoint_scaled_select_execute" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 对 `IOA=0x6201` 发送标度化遥调选择 `0x31/COT=0x06`，值为 `100`。
2. 期望收到遥调选择确认 `0x31/COT=0x07`。
3. 对同一 IOA 发送执行命令。
4. 期望收到遥调结束 `0x31/COT=0x0A`。

判定点：

- 遥调选择确认正确。
- 遥调执行结束正确。

### 6.4 `test_setpoint_scaled_out_of_range_ioa`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_control_setpoint.py -v -k "setpoint_scaled_out_of_range_ioa" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 对越界遥调 IOA `0x6200` 发送选择命令。
2. 等待遥调结束帧。

判定点：

- 返回 `0x31/COT=0x0A`，表示从站拒绝并结束本次遥调流程。
- PN/negative 位是否置位由拒绝 PN 位相关 xfail 用例单独跟踪。

### 6.5 `test_single_command_direct_execute_rejection_sets_negative`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_control_setpoint.py -v -k "direct_execute_rejection_sets_negative" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 不经过选择，直接对 `IOA=0x6002` 发送遥控执行。
2. 等待遥控结束帧。

判定点：

- 期望返回 `0x2D/COT=0x0A`。
- 期望 PN/negative 位为 true。

当前状态：

```text
xfail
```

原因：当前实现拒绝遥控/遥调时返回结束帧，但 `negative=false`，可能和按 PN 位判断拒绝的主站存在歧义。

## 7. 自定义 TypeID 用例

文件：`tests/iec104_auto/test_custom_services.py`

### 7.1 `test_measure_total_call_0x88`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_custom_services.py -v -k "measure_total_call_0x88" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 发送测量总召 `0x88/COT=0x06`。
2. 接收直到 `0x88/COT=0x0A`。
3. 解析返回的遥信和遥测。

判定点：

- 收到 `0x88/COT=0x07` 确认。
- 收到 `0x01/COT=0x05` 遥信数据。
- 收到 `0x0B/COT=0x05` 遥测数据。
- 收到 `0x88/COT=0x0A` 结束。
- 遥信完整覆盖 `0x0001..0x1000`。
- 遥测至少包含 `0x4001..0x5000`，允许 slave 在 0x88 测量总召中追加扩展遥测段。
- 遥测关键值 `0x4001=10`、`0x4002=11`。

### 7.2 `test_custom_word_range_calls`

单独运行全部参数化子用例：

```powershell
py -m pytest tests\iec104_auto\test_custom_services.py -v -k "custom_word_range_calls" --slave-host 192.168.159.209 --port 2404 --ca 1
```

单独运行某个参数化子用例示例：

```powershell
py -m pytest tests\iec104_auto\test_custom_services.py -v -k "rtu-param" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 按参数发送自定义召唤 TypeID。
2. 等待对应 TypeID 的结束帧 `COT=0x0A`。
3. 检查确认帧、数据帧和结束帧。
4. 按 word 数组解析返回数据。

参数化覆盖：

```text
0x8C rtu-param          IOA 0x1001..0x103E，关键值 0x1001=100, 0x1002=101
0x8D sensor-param       IOA 0x2001..0x4000，关键值 0x2001=200, 0x2002=201
0x8E harmonic           IOA 0x5201..0x5400，关键值 0x5201=1012, 0x5202=1013
0x8F meter-truck        IOA 0x5001..0x5100，关键值 0x5001=500, 0x5002=501
0x90 injection          IOA 0x5101..0x5200，关键值 0x5101=756, 0x5102=757
0x92 active-power       IOA 0x5CB7..0x5FD6，关键值 0x5CB7=3754, 0x5CB8=3755
0x93 wellhead-pressure  IOA 0x5B27..0x5CB6，关键值 0x5B27=3354, 0x5B28=3355
0x95 reserved-sensor    IOA 0x4200..0x42AA
```

判定点：

- 收到对应 TypeID 的确认 `COT=0x07`。
- 收到对应 TypeID 的数据 `COT=0x05`。
- 数据起始 IOA 落在期望范围。
- 返回 IOA 完整覆盖期望范围。
- 有定义关键值的用例检查关键值。
- 收到对应 TypeID 的结束 `COT=0x0A`。

### 7.3 `test_dynagram_0x89_has_cp56_tail`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_custom_services.py -v -k "dynagram_0x89_has_cp56_tail" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 发送示功图召唤 `0x89/COT=0x06`。
2. 接收直到 `0x89/COT=0x0A`。
3. 解析 `0x89/COT=0x05` 数据。

判定点：

- 数据起始 IOA 在 `0x5401..0x5800`。
- 载荷长度至少满足 IOA + word 数据 + 7 字节 CP56Time2a 尾部。
- IOA 完整覆盖 `0x5401..0x5800`。
- 关键值 `0x5401=1524`、`0x5402=1525`。

### 7.4 `test_all_dynagram_0x91_returns_89_8a_93_then_finish`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_custom_services.py -v -k "all_dynagram_0x91" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 发送全部功图召唤 `0x91/COT=0x06`。
2. 接收直到 `0x91/COT=0x0A`。

判定点：

- 收到 `0x91/COT=0x07` 确认。
- 返回 `0x89/COT=0x05` 示功图数据。
- 返回 `0x8A/COT=0x05` 电功图数据。
- 返回 `0x93/COT=0x05` 井口回压数据。
- 收到 `0x91/COT=0x0A` 结束。

### 7.5 `test_history_call_0x8b_finishes`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_custom_services.py -v -k "history_call_0x8b" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 发送历史数据召唤 `0x8B/COT=0x06`，载荷包含起止 CP56Time2a。
2. 接收直到 `0x8B/COT=0x0A`。

判定点：

- 至少收到 `0x8B/COT=0x0A` 结束帧。

说明：该用例当前主要验证流程能结束，未强制要求历史数据非空。

## 8. 主动上送和诊断注入用例

文件：`tests/iec104_auto/test_active_upload.py`

这些用例依赖远程 SSH 和诊断工具 `iec104-diag`，建议使用：

```powershell
py -m pytest tests\iec104_auto\test_active_upload.py -v --manage-slave --slave-host 192.168.159.209 --ssh-user sps --ssh-pass sps --project-dir /home/sps/shihua/104_slave
```

注意：

- `test_active_upload.py` 中的主动上送和诊断注入用例依赖 `diag` fixture。
- `diag` fixture 需要 SSH 远程执行 `iec104_diag`，所以运行时必须带 `--manage-slave`、`--build-slave`、`--restart-slave` 或 `--capture-pcap`。
- 未带远程管理参数时，这些用例会被跳过，不应判定为协议栈失败。
- 如果报告中看到 `SKIPPED`，先检查命令行是否带了 `--manage-slave`，以及是否已安装 `tests\iec104_auto\requirements-remote.txt`。

### 8.1 `test_active_yc_upload_uses_timed_measured_value`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_active_upload.py -v -k "active_yc_upload_uses_timed_measured_value" --manage-slave --slave-host 192.168.159.209
```

测试方法：

1. 通过诊断口执行 `set-yc --ioa 16385 --value 321`。
2. 通过诊断口执行 `active-upload notify`。
3. 等待主动遥测上送。

判定点：

- 收到 `0x23/COT=0x03`。
- 起始 IOA 为 `0x4001`。
- 载荷长度满足带时标遥测结构。
- 解析值 `0x4001=321`。

### 8.2 `test_active_yx_upload_has_soe_with_timestamp`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_active_upload.py -v -k "active_yx_upload_has_soe_with_timestamp" --manage-slave --slave-host 192.168.159.209
```

测试方法：

1. 通过诊断口执行 `set-yx --ioa 5 --value 1`。
2. 执行 `active-upload notify`。
3. 等待 SOE 主动上送。

判定点：

- 收到 `0x1E/COT=0x03`。
- 信息对象包含 IOA `5`。
- 载荷长度满足带 CP56Time2a 的遥信结构。
- 解析值 `5=True`。

### 8.3 `test_active_yx_upload_does_not_send_untimed_yx`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_active_upload.py -v -k "does_not_send_untimed_yx" --manage-slave --slave-host 192.168.159.209
```

测试方法：

1. 通过诊断口设置遥信 `IOA=6`。
2. 触发主动上送。
3. 在 3 秒内收集所有响应。

判定点：

- 不应出现 `0x01/COT=0x03` 无时标主动遥信。

当前状态：

```text
xfail
```

原因：当前实现主动遥信仍会先发无时标 `0x01`，再发带时标 `0x1E` SOE。

### 8.4 `test_history_soe_call_0x94`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_active_upload.py -v -k "history_soe_call_0x94" --manage-slave --slave-host 192.168.159.209
```

测试方法：

1. 通过诊断口执行 `soe add --ioa 7 --value 1`。
2. 发送历史 SOE 召唤 `0x94/COT=0x06`。
3. 接收直到 `0x94/COT=0x0A`。

判定点：

- 收到 `0x94/COT=0x05` 历史 SOE 数据。
- 收到 `0x94/COT=0x0A` 结束。

### 8.5 `test_general_interrogation_reads_diag_yc_value`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_active_upload.py -v -k "general_interrogation_reads_diag_yc_value" --manage-slave --slave-host 192.168.159.209
```

测试方法：

1. 通过诊断口执行 `set-yc --ioa 16386 --value 456`。
2. 发送标准总召唤。
3. 解析总召遥测数据。

判定点：

- `0x4002` 的总召返回值为 `456`。

### 8.6 `test_counter_interrogation_reads_diag_dd_value`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_active_upload.py -v -k "counter_interrogation_reads_diag_dd_value" --manage-slave --slave-host 192.168.159.209
```

测试方法：

1. 通过诊断口写入多个电度点：
   - `set-dd --ioa 25601 --value 123456`
   - `set-dd --ioa 25604 --value 654321`
   - `set-dd --ioa 25856 --value 234567`
   - `set-dd --ioa 26112 --value 345678`
2. 发送电度召唤。
3. 解析 `0x0F/COT=0x05` 数据。

判定点：

- `0x6401` 的电度返回值为 `123456`。
- `0x6404` 的电度返回值为 `654321`。
- `0x6500` 的电度返回值为 `234567`。
- `0x6600` 的电度返回值为 `345678`。

### 8.7 `test_interrogations_return_diag_modified_yx_yc_dd_values`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_active_upload.py -v -k "interrogations_return_diag_modified_yx_yc_dd_values" --manage-slave --slave-host 192.168.159.209
```

指定随机种子、轮数和每轮点数：

```powershell
py -m pytest tests\iec104_auto\test_active_upload.py -v -k "interrogations_return_diag_modified_yx_yc_dd_values" --manage-slave --slave-host 192.168.159.209 --diag-random-seed 20260611 --diag-random-rounds 5 --diag-random-points 4
```

测试方法：

1. 使用固定随机种子生成随机点位，默认种子 `1042026`，默认 10 轮。
2. 每轮从遥信 `0x0001..0x1000` 随机挑选若干点，默认 50 个点。
3. 每轮从遥测 `0x4001..0x5000` 随机挑选若干点，默认 50 个点。
4. 每轮从电度 `0x6401..0x6600` 随机挑选若干点，默认 50 个点。
5. 通过 `iec104_diag set-yx`、`set-yc`、`set-dd` 将这些点写成该轮特定值。
6. 发送标准总召唤 `0x64/COT=0x06`。
7. 在总召应答中解析 `0x01/COT=0x14` 遥信和 `0x0B/COT=0x14` 遥测，并与本轮写入值比较。
8. 发送电度召唤 `0x65/COT=0x06`。
9. 在电度召唤应答中解析 `0x0F/COT=0x05` 电度，并与本轮写入值比较。
10. 所有轮次全部通过，测试才通过。

判定点：

- 总召结束 `0x64/COT=0x0A` 正常返回。
- 每轮随机遥信点在总召应答中均为修改后的值。
- 每轮随机遥测点在总召应答中均为修改后的值。
- 电度召唤结束 `0x65/COT=0x0A` 正常返回。
- 每轮随机电度点在电度召唤应答中均为修改后的值。
- 总召和电度召唤过程中所有已支持类型的 VSQ 数量与实际载荷对象数量一致。
- 失败信息会打印随机种子、轮次和本轮期望值，便于用相同参数复现。

## 9. 负向用例

文件：`tests/iec104_auto/test_negative.py`

### 9.1 `test_unknown_type_id`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_negative.py -v -k "unknown_type_id" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 发送未知 TypeID `0xAA/COT=0x06`。
2. 等待 unknown TypeID 响应。

判定点：

- 返回 `0xAA/COT=0x2C`。
- PN/negative 位为 true。

### 9.2 `test_custom_call_wrong_cot`

单独运行：

```powershell
py -m pytest tests\iec104_auto\test_negative.py -v -k "custom_call_wrong_cot" --slave-host 192.168.159.209 --port 2404 --ca 1
```

测试方法：

1. 对自定义召唤 `0x88` 使用错误 COT `0x05`。
2. 等待 unknown COT 响应。

判定点：

- 返回 `0x88/COT=0x2D`。
- PN/negative 位为 true。

## 10. 已知 xfail 用例说明

当前存在以下预期失败用例。它们用于记录已知协议差异，不会导致测试批次失败：

```text
test_general_interrogation_does_not_return_custom_or_counter_ranges
  标准总召当前会追加扩展/自定义数据。

test_active_yx_upload_does_not_send_untimed_yx
  主动遥信当前会先发无时标 0x01，再发带时标 0x1E。

test_single_command_direct_execute_rejection_sets_negative
  拒绝遥控/遥调时当前 negative=false。
```

## 11. 常见问题

### 11.1 STARTDT 阶段连接被关闭

现象：

```text
slave closed the connection during STARTDT
```

检查：

```bash
ssh sps@192.168.159.209
cd /home/sps/shihua/104_slave
ps -ef | grep iec104_slave
ss -lntp | grep 2404
tail -100 run.log
```

也要确认 ProIEC104Client 没有占用唯一主站连接。如果 `max_open_connections=1`，slave 可能接受第二个连接后立即关闭。

### 11.2 总召只收到 12 个 I 帧后超时

通常是主站没有发送 S 帧确认导致 slave 发满 K 窗口后暂停。当前自动化客户端已经在收到 I 帧后自动发送 S 帧确认。

### 11.3 抓包中看到 `00 3c 0e 00...` 类似“非法帧”

这通常是同一个 `68 fd` 长 APDU 的 TCP 后续分片，不是新的 IEC104 帧。正确解析必须按 `0x68 + length` 从 TCP 字节流中重组完整 APDU。

### 11.4 `68 0e 00` 不一定是新帧头

如果它位于 `0x0B` 标度化遥测的 SQ=1 数据区，`68 0e 00` 表示遥测值 `0x0E68` 和品质字节 `0x00`，不是 APDU 起始。

### 11.5 主动用例报告中出现 `remote did not yield a value`

历史版本测试框架中，`remote` fixture 在不需要 SSH 远程管理的分支曾经直接 `return None`。由于该 fixture 其他分支使用了 `yield`，pytest 会把它识别为生成器 fixture；生成器 fixture 必须始终 `yield` 一个值，否则 setup 阶段会报：

```text
ValueError: remote did not yield a value
```

当前版本已修复为不需要远程管理时 `yield None`，因此不应再出现该 setup 错误。修复后，如果主动上送用例没有带 `--manage-slave`、`--build-slave`、`--restart-slave` 或 `--capture-pcap`，结果应为 `SKIPPED`，并提示：

```text
diag tests require --manage-slave, --build-slave, --restart-slave, or --capture-pcap so SSH is available
```

如果要真正执行主动遥测带时标上送测试，应使用：

```powershell
py -m pytest tests\iec104_auto\test_active_upload.py -v -k "active_yc_upload_uses_timed_measured_value" --manage-slave --slave-host 192.168.159.209 --ssh-user sps --ssh-pass sps --project-dir /home/sps/shihua/104_slave --report-md tests\iec104_auto\active_yc_report.md
```
