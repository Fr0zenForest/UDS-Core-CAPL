# UDS-Core-CAPL

CANoe CAPL 编写的 UDS 诊断底层库，为上层测试用例提供统一的 UDS 服务接口。

## 用途

封装 ISO 14229 (UDS) 诊断协议的常用服务，上层测试脚本只需调用 `UDS_ReadDID()`、`UDS_SecurityAccess_Unlock()` 等高层函数，无需关心底层 ISO-TP 分帧、流控、超时重试等细节。

支持的 UDS 服务：

| SID | 服务 | 函数 |
|-----|------|------|
| 0x10 | DiagSessionControl | `UDS_DiagSessionControl_Extended()` / `_Programming()` / `_Default()` |
| 0x11 | ECUReset | `UDS_ECUReset_Hard()` |
| 0x14 | ClearDTC | `UDS_ClearDTC_All()` |
| 0x22 | ReadDID | `UDS_ReadDID(did, dataOut, &dataLen)` |
| 0x27 | SecurityAccess | `UDS_SecurityAccess_Unlock()` |
| 0x28 | CommunicationControl | `UDS_CommunicationControl(subFunc, commType)` |
| 0x2E | WriteDID | `UDS_WriteDID(did, dataIn, dataInLen)` |
| 0x31 | RoutineControl | `UDS_RoutineControl_Start()` / `_StartWithData()` |
| 0x34 | RequestDownload | `UDS_RequestDownload(...)` |
| 0x36 | TransferData | `UDS_TransferData(blockSeq, data, dataLen)` |
| 0x37 | RequestTransferExit | `UDS_RequestTransferExit()` |
| 0x85 | ControlDTCSetting | `UDS_ControlDTCSetting(subFunc)` |

## 架构

```
┌─────────────────────────────────┐
│         上层测试用例 (.can)       │
├─────────────────────────────────┤
│     UDS_Services.cin  服务层     │  ← 传输无关，所有 UDS 服务封装
├──────────────┬──────────────────┤
│ CAN_Transport│  DoIP_Transport  │  ← 二选一，提供 UDS_SendRaw()
│    .cin      │      .cin        │
└──────────────┴──────────────────┘
```

- 服务层（`UDS_Services.cin`）：收发缓冲区、UDS 服务函数、NRC 处理、CRC32、日志格式化
- CAN 传输层（`CAN_Transport.cin`）：ISO-TP 分帧/重组（SF/FF/CF/FC）、CAN FD 支持、TesterPresent 保活
- DoIP 传输层（`DoIP_Transport.cin`）：基于 CANoe DoIP.DLL，TCP 连接管理、Routing Activation

切换传输方式只需替换 `#include` 的传输层文件，服务层代码无需修改。

## 使用方式

在 CANoe CAPL 测试节点中按顺序 include：

```c
includes
{
  #include "CAN_Transport.cin"    // 或 DoIP_Transport.cin
  #include "UDS_Services.cin"
}
```

初始化后即可调用服务函数：

```c
on start
{
  UDS_Init(0x741, 0x641, "ECU_Name");  // 设置 CAN ID 和 ECU 名称
}

testcase TC_ReadDID()
{
  byte data[256];
  int  len;
  UDS_DiagSessionControl_Extended();
  UDS_ReadDID(0xF190, data, len);
}
```