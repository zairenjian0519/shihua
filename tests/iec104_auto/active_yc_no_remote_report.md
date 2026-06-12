# IEC104 Slave 自动化测试报告

## 1. 测试概况

| 项目 | 值 |
| --- | --- |
| 开始时间 | 2026-06-12 09:33:31 |
| 结束时间 | 2026-06-12 09:33:31 |
| 退出码 | 0 |
| Slave 地址 | 192.168.159.209:2404 |
| 公共地址 CA | 1 |
| 总用例数 | 1 |
| 通过 | 0 |
| 失败 | 0 |
| 跳过 | 1 |
| 预期失败 xfail | 0 |
| 意外通过 xpass | 0 |
| 用例执行耗时合计 | 0.017 秒 |
| 诊断随机种子 | 1042026 |
| 诊断随机轮数 | 10 |
| 每轮每类随机点数 | 50 |

## 2. 结果汇总

| 用例 | 中文名称 | 标记 | 结果 | 耗时(s) |
| --- | --- | --- | --- | --- |
| `test_active_upload.py::test_active_yc_upload_uses_timed_measured_value` | 主动遥测带时标上送 | active | 跳过 | 0.017 |

## 3. 用例详情

### 3.1 主动遥测带时标上送

- 用例节点：`test_active_upload.py::test_active_yc_upload_uses_timed_measured_value`
- pytest 函数：`test_active_yc_upload_uses_timed_measured_value`
- 标记：active
- 结果：跳过
- 耗时：0.017 秒

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

失败/跳过详情：

```text
('D:\\project\\中石化-四化协议\\proj\\tests\\iec104_auto\\test_active_upload.py', 20, 'Skipped: diag tests require --manage-slave or --capture-pcap so SSH is available')
```

