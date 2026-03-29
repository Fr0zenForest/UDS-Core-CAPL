# DoIP_CANoe_Transport.cin 开发文档

> 基于 CANoe IP_Endpoint API 的 DoIP 传输层实现，纯 CAPL 零外部依赖。

---

## 1. 概述

`DoIP_CANoe_Transport.cin` 是 UDS-Core-CAPL 框架的第三套传输层，通过 CANoe 内置的 `IP_Endpoint` 风格 TCP/IP API 实现 ISO 13400-2 DoIP 协议，提供与 `CAN_Transport.cin` 和 `DoIP_Transport.cin` 完全一致的上层接口。

### 1.1 适用场景

- 通过以太网对 ECU 进行 UDS 诊断（DoIP）
- CANoe 已配置以太网通道（VN5620/VN5640 或虚拟通道）
- 不希望依赖外部 DLL，希望纯 CAPL 脚本即可运行

### 1.2 三套传输层对比

| 维度 | CAN_Transport | DoIP_Transport (DLL) | DoIP_CANoe_Transport |
|------|--------------|---------------------|----------------------|
| 物理层 | CAN / CAN FD | Ethernet | Ethernet |
| 依赖 | CANoe CAN 通道 | DoIP_Core.dll | CANoe IP_Endpoint API |
| 代码形式 | 纯 CAPL | CAPL + C++ DLL | 纯 CAPL |
| 收发模式 | ISO-TP + 异步事件 | DLL 同步阻塞 | TCP 异步回调 |
| KeepAlive | 0x7DF 功能寻址 CAN 帧 | DLL 后台线程 | CAPL msTimer |
| 编译依赖 | 无 | CMake + MSVC | 无 |
| 适用环境 | CAN 总线 | 任意 Windows 网卡 | CANoe 以太网通道 |

---

## 2. 架构设计

### 2.1 整体分层

```
┌────────────────────────────────────────────────────┐
│  Test Module (.can + .vxt)                         │
│  DoIP_Test.can / SecurityFlash.can / ...           │
├────────────────────────────────────────────────────┤
│  UDS Service Layer                                 │
│  UDS_Services.cin                                  │
│  (10/11/14/22/27/28/2E/31/34/36/37/85)            │
├────────────────────────────────────────────────────┤
│  Transport Layer (三选一)                           │
│  ┌──────────────┬───────────────┬────────────────┐ │
│  │CAN_Transport │DoIP_Transport │DoIP_CANoe_     │ │
│  │   .cin       │   .cin        │Transport.cin   │ │
│  │ (ISO-TP)     │ (DoIP DLL)    │(IP_Endpoint)   │ │
│  └──────┬───────┴───────┬───────┴────────┬───────┘ │
├─────────┼───────────────┼────────────────┼─────────┤
│  底层   │ CANoe CAN     │ WinSock2       │ CANoe   │
│  通信   │ message       │ TCP/UDP        │ TCP API │
│         │               │ (DoIP_Core.dll)│(IP_Ep)  │
└─────────┴───────────────┴────────────────┴─────────┘
```

### 2.2 模块内部结构

```
DoIP_CANoe_Transport.cin
├── variables { ... }                  // 连接状态、协议常量、缓冲区
├── DoIP_BuildHeader()                 // DoIP 帧构建
├── DoIP_ParseHeader()                 // DoIP 帧解析
├── DoIP_BuildRoutingActReq()          // Routing Activation 请求
├── DoIP_BuildDiagMsg()                // Diagnostic Message 封装
├── DoIP_BuildAliveCheckResp()         // Alive Check 响应
├── DoIP_TcpSendBytes()               // byte[]→char[] 发送辅助
├── OnTcpConnect()                     // TCP 连接回调
├── OnTcpReceive()                     // TCP 数据接收回调 + 拼包
├── OnTcpClose()                       // TCP 关闭回调
├── DoIP_HandleFrame()                 // DoIP 帧分发处理
├── UDS_Init()                         // 传输层初始化 (对外接口)
├── UDS_SendRaw()                      // 发送 UDS 并等待响应 (对外接口)
├── on timer gUDS_KeepAliveTimer       // 3E 80 保活定时器
├── UDS_StartKeepAlive()               // 启动保活 (对外接口)
└── UDS_StopKeepAlive()                // 停止保活 (对外接口)
```

---

## 3. CANoe TCP/IP API 说明

### 3.1 为什么选择 IP_Endpoint API

CANoe 提供两套 TCP/IP API：

| API 风格 | 函数签名 | 特点 |
|----------|----------|------|
| **数值 IP** | `TcpOpen(ipNum, port)` | 早期接口，IP 为 `dword` 数值 |
| **IP_Endpoint** | `TcpOpen(IP_Endpoint)` | 新接口，使用结构体封装 IP + Port |

**实测结论**: 在部分 CANoe 以太网通道配置下，数值 IP API 的 `TcpConnect()` 会超时失败（10 秒后返回错误），而 IP_Endpoint API 能正常建立 TCP 连接。原因可能与 CANoe 内部的 IP 路由/绑定机制差异有关。

**建议**: 新项目统一使用 IP_Endpoint API。

### 3.2 IP_Endpoint API 关键特性

#### 3.2.1 TcpOpen — 绑定本地端口

```c
IP_Endpoint localEp;
localEp.ParseEndpointFromString("192.168.69.21");
localEp.PortNumber = 0;  // 0 = 系统自动分配端口
dword socket = TcpOpen(localEp);
```

- `PortNumber = 0` 让系统自动分配临时端口（推荐）
- 不要手动指定端口号，避免端口冲突

#### 3.2.2 TcpConnect — 异步连接

```c
IP_Endpoint remoteEp;
remoteEp.ParseEndpointFromString("192.168.69.1");
remoteEp.PortNumber = 13400;  // DoIP 标准端口
long result = TcpConnect(socket, remoteEp);
```

- 连接是**异步**的，结果通过 `OnTcpConnect()` 回调通知
- `result != 0` 且 `IpGetLastSocketError() != 10035 (WSAEWOULDBLOCK)` 才是真正失败
- `WSAEWOULDBLOCK` 是正常的异步连接进行中状态

#### 3.2.3 TcpReceive — 手动 arm 接收

```c
char charBuf[16400];
TcpReceive(socket, charBuf, elcount(charBuf));
```

**这是 IP_Endpoint API 与数值 IP API 最关键的区别**:
- 数值 IP API: 连接成功后自动接收，`OnTcpReceive` 自动触发
- IP_Endpoint API: **必须手动调用 `TcpReceive()` arm 一次异步接收**

**必须 arm 的时机**:
1. `OnTcpConnect` 连接成功后 — 首次 arm
2. `OnTcpReceive` 回调中处理完数据后 — re-arm 等待下一次数据
3. 帧未收齐时 — re-arm 继续等待

**遗漏 arm 的后果**: TCP 数据到达但回调不触发，表现为"响应超时"。

#### 3.2.4 缓冲区类型

- IP_Endpoint API 的 `TcpSend` 和 `TcpReceive` 使用 **`char[]`** 缓冲区
- DoIP 协议解析需要 **`byte[]`**
- 需要 `memcpy` 或逐字节转换在两种类型之间桥接

---

## 4. DoIP 协议实现 (ISO 13400-2)

### 4.1 DoIP 帧格式

```
Byte:  0       1       2       3       4       5       6       7       8...
     ┌───────┬───────┬───────────────┬───────────────────────────────┬──────────
     │Version│~Ver   │ Payload Type  │       Payload Length          │ Payload
     │ 0x02  │ 0xFD  │   (2 bytes)   │        (4 bytes)             │ (N bytes)
     └───────┴───────┴───────────────┴───────────────────────────────┴──────────
     |<------------- Header (8 bytes) --------------------------->|
```

- `Version`: DoIP 协议版本，ISO 13400-2:2012 = `0x02`
- `~Version`: 版本取反，`0xFD`
- 校验: `buf[0] ^ 0xFF == buf[1]`

### 4.2 Payload Types

| 值 | 常量名 | 方向 | 说明 |
|----|--------|------|------|
| `0x0005` | `cDoIP_RoutingActReq` | Tester→ECU | Routing Activation Request |
| `0x0006` | `cDoIP_RoutingActResp` | ECU→Tester | Routing Activation Response |
| `0x0007` | `cDoIP_AliveCheckReq` | ECU→Tester | Alive Check Request |
| `0x0008` | `cDoIP_AliveCheckResp` | Tester→ECU | Alive Check Response |
| `0x8001` | `cDoIP_DiagMessage` | 双向 | Diagnostic Message (UDS 载荷) |
| `0x8002` | `cDoIP_DiagPosAck` | ECU→Tester | Diagnostic Positive ACK |
| `0x8003` | `cDoIP_DiagNegAck` | ECU→Tester | Diagnostic Negative ACK |

### 4.3 Routing Activation

建立 TCP 连接后，Tester 必须先发送 Routing Activation 请求，ECU 响应 `0x10` (成功) 后才能进行诊断通信。

**请求 (0x0005)**:
```
Payload (7 bytes): SA[2] + ActivationType[1] + Reserved[4]
  SA = Tester Address (如 0x0002)
  ActivationType = 0x00 (Default)
  Reserved = 0x00000000
Total = Header(8) + 7 = 15 bytes
```

**响应 (0x0006)**:
```
Payload: TesterAddr[2] + EntityAddr[2] + ResponseCode[1] + [OEM specific]
  ResponseCode:
    0x10 = Routing activation accepted
    0x00 = Unknown SA
    0x01 = All sockets registered
    0x02 = Different SA already registered
    0x03 = SA not activated
    0x04 = Authentication missing
    0x06 = Rejected, unsupported type
```

### 4.4 Diagnostic Message (0x8001)

```
Payload: SA[2] + TA[2] + UDS_Data[N]
  SA = Source Address (发送方逻辑地址)
  TA = Target Address (接收方逻辑地址)
  UDS_Data = 原始 UDS 字节 (如 10 03 = Extended Session)
```

### 4.5 Alive Check (0x0007/0x0008)

ECU 定期发送 Alive Check Request (0x0007)，Tester 必须回复 Alive Check Response (0x0008)，否则 ECU 将断开连接。

```
Request  (0x0007): 无 payload 或 payload 长度 0
Response (0x0008): SA[2] (Tester Address)
```

---

## 5. TCP 流拼包状态机

TCP 是字节流，一次 `OnTcpReceive` 回调可能收到:
- 不足一个 DoIP 帧（需要等下次回调凑齐）
- 恰好一个 DoIP 帧
- 多个完整 DoIP 帧（需要循环处理）
- 上一帧的尾部 + 下一帧的头部

### 5.1 状态机逻辑

```
OnTcpReceive(char buffer[], dword size)
│
├─ 1. 追加到 gDoIP_RxBuf[gDoIP_RxPos..] (char → byte)
│
├─ 2. while (gDoIP_RxPos >= 8):
│     ├─ DoIP_ParseHeader → payloadType, payloadLen
│     ├─ frameLen = 8 + payloadLen
│     ├─ if (gDoIP_RxPos < frameLen):
│     │     break (帧未收齐, re-arm 等下次)
│     ├─ DoIP_HandleFrame(payloadType, buf, frameLen)
│     └─ 移除已处理帧, 保留剩余字节
│
└─ 3. TcpReceive() re-arm
```

### 5.2 缓冲区布局

```
gDoIP_CharRxBuf[16400]  ← TcpReceive() arm 用 (char[])
         │
         │  OnTcpReceive 回调
         ▼
gDoIP_RxBuf[16400]      ← DoIP 帧拼包/解析用 (byte[])
gDoIP_RxPos              ← 当前已接收字节数
```

---

## 6. 通信流程

### 6.1 连接建立 (UDS_Init)

```
UDS_Init()
│
├─ 1. diagSetTarget(ecuQualifier)    // 设置 Seed&Key DLL 目标
│
├─ 2. TcpOpen(localEp)              // 绑定本地 IP, 端口自动分配
│     ├─ 检查 IpGetLastSocketError
│
├─ 3. TcpConnect(socket, remoteEp)  // 异步连接 ECU:13400
│     ├─ 检查 WSAEWOULDBLOCK (正常)
│
├─ 4. testWaitForTextEvent("DoIP_ConnectResult", 10000)
│     │
│     │  [异步回调链]
│     │  OnTcpConnect()
│     │  ├─ TcpReceive() arm 首次接收
│     │  ├─ DoIP_BuildRoutingActReq()
│     │  └─ DoIP_TcpSendBytes(RA_Request)
│     │
│     │  OnTcpReceive()
│     │  ├─ 拼包 → DoIP_HandleFrame()
│     │  └─ RA Response (0x0006, code=0x10)
│     │     ├─ gDoIP_Connected = 1
│     │     └─ testSupplyTextEvent("DoIP_ConnectResult")
│     │
│     ▼
├─ 5. 检查 gDoIP_Connected == 1
│
└─ Done: 连接就绪, 可以诊断
```

### 6.2 UDS 请求/响应 (UDS_SendRaw)

```
UDS_SendRaw()
│
├─ 1. 检查 gDoIP_Connected == 1
├─ 2. 请求间隔延迟 (cUDS_InterReqDelay ms)
├─ 3. DoIP_BuildDiagMsg(SA, TA, gUDS_TxBuf)
├─ 4. DoIP_TcpSendBytes(doipFrame)
│
├─ 5. testWaitForTextEvent("UDS_RxDone", timeout)
│     │
│     │  [异步回调链]
│     │  OnTcpReceive()
│     │  └─ DoIP_HandleFrame()
│     │     ├─ 0x8002 Positive ACK → 忽略, 等后续 0x8001
│     │     └─ 0x8001 Diagnostic Message
│     │        ├─ 提取 UDS data → gUDS_RxBuf
│     │        └─ testSupplyTextEvent("UDS_RxDone")
│     ▼
├─ 6. 检查 gUDS_RxReady
│
├─ 7. NRC 0x78 Pending 循环:
│     └─ testWaitForTextEvent("UDS_RxDone", P2*_timeout)
│
└─ return 1=成功 / 0=超时 / -1=错误
```

### 6.3 KeepAlive (TesterPresent 3E 80)

```
UDS_StartKeepAlive()
├─ gUDS_KeepAliveActive = 1
└─ setTimer(gUDS_KeepAliveTimer, 2000)

on timer gUDS_KeepAliveTimer
├─ 检查 Active && Connected
├─ DoIP_BuildDiagMsg(SA, TA, {0x3E, 0x80})
├─ DoIP_TcpSendBytes()
└─ setTimer(gUDS_KeepAliveTimer, 2000)  // 2秒周期

UDS_StopKeepAlive()
├─ gUDS_KeepAliveActive = 0
└─ cancelTimer(gUDS_KeepAliveTimer)
```

---

## 7. 配置参数

### 7.1 上层 .can 文件中定义

```c
variables
{
  /* DoIP 连接参数 */
  char   gDoIP_LocalIp[32]  = "192.168.69.21";  // 测试 PC 网卡 IP
  char   gDoIP_EcuIp[32]    = "192.168.69.1";   // ECU DoIP 端口 IP
  dword  gDoIP_TesterAddr   = 0x0002;            // Tester 逻辑地址 (SA)
  dword  gDoIP_EcuAddr      = 0x0001;            // ECU 逻辑地址 (TA)

  /* SecurityAccess 惩罚延迟 (NRC 0x37 等待) */
  const dword cSA_PenaltyDelay = 10000;          // ms
}
```

### 7.2 传输层内部常量

| 常量 | 值 | 说明 |
|------|-----|------|
| `cDoIP_Version` | `0x02` | DoIP 协议版本 |
| `cDoIP_InvVersion` | `0xFD` | 版本取反 |
| `cDoIP_Port` | `13400` | DoIP 标准 TCP 端口 |
| `cDoIP_HeaderLen` | `8` | DoIP 帧头长度 |
| `cUDS_DefaultTimeout` | `5000` | UDS 请求默认超时 (ms) |
| `cUDS_P2StarTimeout` | `10000` | NRC 0x78 P2* 超时 (ms) |
| `cUDS_InterReqDelay` | `100` | UDS 请求间隔 (ms) |

### 7.3 网络配置

1. **Windows 网卡**: 设置静态 IP（如 `192.168.69.21`，子网 `255.255.255.0`）
2. **CANoe 以太网通道**: Simulation Setup 中配置 Ethernet 通道绑定到对应网卡
3. **ECU 端**: 确认 DoIP 端口 `13400` 可达（`ping` 或 Wireshark 抓包验证）

---

## 8. 使用示例

### 8.1 最小测试脚本

```c
/*@!Encoding:65001*/
includes
{
  #include "DoIP_CANoe_Transport.cin"
  #include "UDS_Services.cin"
}

variables
{
  char   gDoIP_LocalIp[32]  = "192.168.69.21";
  char   gDoIP_EcuIp[32]    = "192.168.69.1";
  dword  gDoIP_TesterAddr   = 0x0002;
  dword  gDoIP_EcuAddr      = 0x0001;
  const dword cSA_PenaltyDelay = 10000;
}

export testfunction Setup()
{
  UDS_Init(0, 0, "ECU_NAME");
  if (gDoIP_Connected != 1)
  {
    testStepFail("Setup", "DoIP 连接失败");
    return;
  }
  testStepPass("Setup", "DoIP 连接成功");
}

testcase TC_ReadVIN()
{
  byte vinData[32];
  long vinLen;
  testCaseTitle("TC_ReadVIN", "22 F190 Read VIN");
  if (UDS_ReadDID(0xF190, vinData, vinLen))
    testStepPass("ReadVIN", "VIN 读取成功, %d bytes", vinLen);
  else
    testStepFail("ReadVIN", "VIN 读取失败");
}

export testfunction Cleanup()
{
  if (gDoIP_Connected == 1)
  {
    UDS_StopKeepAlive();
    UDS_DiagSessionControl_Default();
  }
  testStepPass("Cleanup", "清理完成");
}
```

### 8.2 VXT 配置

```xml
<?xml version="1.0" encoding="gb2312"?>
<testmodule title="DoIP Test" version="1.0"
            xmlns="http://www.vector-informatik.de/CANoe/TestModule/1.27">
  <preparation>
    <capltestfunction name="Setup" title="DoIP Connect" />
  </preparation>
  <testgroup title="UDS Tests">
    <capltestcase name="TC_ReadVIN" title="22 F190 Read VIN" />
  </testgroup>
  <completion>
    <capltestfunction name="Cleanup" title="Cleanup" />
  </completion>
</testmodule>
```

---

## 9. 常见问题排查

### 9.1 TCP 连接超时

**症状**: `TcpConnect` 后 10 秒内未收到 `OnTcpConnect` 回调

**排查步骤**:
1. 检查 Windows 网卡 IP 是否与 `gDoIP_LocalIp` 一致
2. `ping` ECU IP 确认网络可达
3. Wireshark 抓包检查 SYN 包是否发出、SYN-ACK 是否返回
4. 确认 CANoe 以太网通道绑定了正确的网卡
5. 确认使用的是 `IP_Endpoint` API（非数值 IP API）

### 9.2 Routing Activation 失败

**症状**: TCP 连接成功但 `gDoIP_Connected = -1`

**排查步骤**:
1. 检查 Write 窗口中的 RA Response Code（`0x10` = 成功）
2. `ResponseCode = 0x00` → Tester Address 不被 ECU 认可
3. `ResponseCode = 0x01` → ECU 已满连接数，需断开其他 Tester
4. 检查 `gDoIP_TesterAddr` 和 `gDoIP_EcuAddr` 是否与 ECU 配置匹配

### 9.3 UDS 响应超时

**症状**: TX 正常发送但 RX 超时

**排查步骤**:
1. Wireshark 检查 ECU 是否回复了 DoIP 帧
2. 检查 `OnTcpReceive` 是否被调用（加 `write` 日志）
3. 确认每次 `OnTcpReceive` 结尾都有 `TcpReceive()` re-arm
4. 检查 DoIP 帧头是否被正确解析（Version 校验）

### 9.4 Alive Check 导致断连

**症状**: 通信一段时间后 ECU 主动断开 TCP

**原因**: ECU 发送了 Alive Check Request (0x0007) 但未收到 Response

**解决**: `DoIP_HandleFrame` 中已自动处理 Alive Check，检查 `case 0x0007` 分支是否正常执行

---

## 10. 与 DoIP_Transport.cin (DLL 版) 的对比

| 维度 | DoIP_CANoe_Transport | DoIP_Transport (DLL) |
|------|---------------------|---------------------|
| 代码量 | ~535 行 CAPL | ~170 行 CAPL + ~1200 行 C++ |
| 外部依赖 | 无 | DoIP_Core.dll (82KB) |
| 编译 | 无需编译 | 需 CMake + MSVC |
| DoIP 帧处理 | CAPL 手动构建/解析 | DLL 内部处理 |
| TCP 管理 | CANoe IP_Endpoint 异步回调 | WinSock2 同步阻塞 |
| KeepAlive | CAPL msTimer (2s 周期) | C++ 后台线程 (2s 周期) |
| 调试 | CANoe Write 窗口可见全部日志 | DLL 内部日志受限 |
| 可移植性 | 仅 CANoe (需以太网通道) | 任意 Windows 环境 |
| 性能 | 适合诊断 (ms 级延迟可忽略) | 同上 |
| Vehicle Discovery | 不支持 (需另行实现 UDP) | `DoIP_VehicleIdentification()` |

**选择建议**:
- 已有 CANoe 以太网通道配置 → 优先使用 `DoIP_CANoe_Transport.cin`
- 需要脱离 CANoe 或在纯 Windows 环境运行 → 使用 `DoIP_Transport.cin` + DLL
- 需要 UDP Vehicle Discovery → 使用 `DoIP_Transport.cin` + DLL

---

## 11. 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-03-29 | 初始版本，从 EthTest 项目迁移 IP_Endpoint API，TBOX 实测验证通过 |
