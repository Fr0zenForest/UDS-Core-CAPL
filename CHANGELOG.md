# UDS_Transport.cin 底层变更日志

> 本文件记录 `UDS_Transport.cin` 底层传输/服务层的接口变更。
> 其他项目引用了此底层时，需根据本日志同步修改上层调用代码。

---

## [v2.3] - 2026-03-29

### 新增: DoIP_CANoe_Transport.cin — 基于 CANoe IP_Endpoint API 的 DoIP 传输层

**影响范围**: 无（纯新增文件，现有传输层和上层代码无需修改）

**变更来源**: SecurityFlash/doiptest 项目在实际 TBOX 诊断测试中开发并验证通过。

**背景**: 原有两套传输层方案各有局限：
- `CAN_Transport.cin` — 仅限 CAN/CAN FD 总线
- `DoIP_Transport.cin` — 依赖自定义 `DoIP_Core.dll`（需编译/分发 DLL，且 WinSock2 阻塞模型在部分 CANoe 环境下受限）

新增第三选项 `DoIP_CANoe_Transport.cin`，使用 CANoe 内置的 `IP_Endpoint` 风格 TCP API，**零外部依赖**、纯 CAPL 实现。

**新增文件**:
| 文件 | 说明 |
|------|------|
| `DoIP_CANoe_Transport.cin` | DoIP 传输层 — CANoe IP_Endpoint API 实现 (535 行) |
| `experiment/doiptest/DoIP_Test.can` | VXT 测试模块示例 — DoIP UDS 诊断验证 |
| `experiment/doiptest/DoIP_Test_TestModule.vxt` | Vector Test Automation VXT 配置 |

**三套传输层对比**:
| 维度 | CAN_Transport | DoIP_Transport (DLL) | DoIP_CANoe_Transport (新) |
|------|--------------|---------------------|--------------------------|
| 总线 | CAN / CAN FD | Ethernet (DoIP) | Ethernet (DoIP) |
| 依赖 | CANoe CAN 通道 | DoIP_Core.dll (WinSock2) | CANoe IP_Endpoint API (内置) |
| 收发模式 | ISO-TP 分帧 + 事件回调 | DLL 同步阻塞 | TCP 异步回调 + 手动 arm |
| KeepAlive | 功能寻址 0x7DF CAN 帧 | DLL 后台线程 | CAPL msTimer + DoIP 封装 |
| 外部依赖 | 无 | DoIP_Core.dll | 无 |
| 适用场景 | CAN 总线诊断 | 无 CANoe DoIP 建模需求 | 有 CANoe 以太网通道配置 |

**关键实现细节**:

1. **IP_Endpoint API (非数值 IP API)**
   - `TcpOpen(IP_Endpoint)` 而非 `TcpOpen(ipNum, port)`
   - `TcpConnect(socket, IP_Endpoint)` 而非 `TcpConnect(socket, ipNum, port)`
   - 接收回调 `OnTcpReceive(socket, result, IP_Endpoint, char[], size)` 而非 `(socket, result, dword, dword, byte[], size)`
   - **重要**: 数值 IP API 在部分 CANoe 配置下 TCP 连接超时失败，IP_Endpoint API 经实测可靠

2. **手动 TcpReceive arm 机制**
   - IP_Endpoint API 不自动接收，需在连接成功后和每次 `OnTcpReceive` 回调中调用 `TcpReceive(socket, charBuf, size)` 重新 arm
   - 遗漏 re-arm 将导致后续数据丢失

3. **char[] ↔ byte[] 缓冲区转换**
   - IP_Endpoint API 收发使用 `char[]`，DoIP 帧解析使用 `byte[]`
   - 接收: `OnTcpReceive` 中逐字节 `(byte)buffer[i]` 转入拼包缓冲区
   - 发送: `DoIP_TcpSendBytes()` 辅助函数通过 `memcpy` 将 `byte[]` 转为 `char[]` 后调用 `TcpSend`

4. **TCP 流拼包**
   - TCP 是字节流，可能一次收到不足一个 DoIP 帧或包含多个帧
   - 使用 `gDoIP_RxBuf[16400]` + `gDoIP_RxPos` 实现拼包状态机
   - 先收齐 8 字节 DoIP Header，解析 `payloadLen`，再等齐完整帧

5. **DoIP 协议帧手动构建/解析 (ISO 13400-2)**
   - `DoIP_BuildHeader()` / `DoIP_ParseHeader()` — 通用 8 字节 Header
   - `DoIP_BuildRoutingActReq()` — Routing Activation (0x0005)
   - `DoIP_BuildDiagMsg()` — Diagnostic Message (0x8001)
   - `DoIP_BuildAliveCheckResp()` — Alive Check Response (0x0008)
   - `DoIP_HandleFrame()` — 帧分发: RA Response / Diag Msg / Pos/Neg ACK / Alive Check

6. **统一接口**: `UDS_Init()` / `UDS_SendRaw()` / `UDS_StartKeepAlive()` / `UDS_StopKeepAlive()` 签名与其他传输层完全一致，上层代码零修改

**配置方法**:
```c
/* 上层 .can 文件中配置 */
char   gDoIP_LocalIp[32]  = "192.168.69.21";   // 本机 IP
char   gDoIP_EcuIp[32]    = "192.168.69.1";    // ECU IP
dword  gDoIP_TesterAddr   = 0x0002;             // Tester 逻辑地址
dword  gDoIP_EcuAddr      = 0x0001;             // ECU 逻辑地址
```

**使用方法** (在 .can 文件的 includes 区域三选一):
```c
includes
{
  /* A) CANoe IP_Endpoint API (新增) */
  #include "DoIP_CANoe_Transport.cin"

  /* B) 自定义 DoIP DLL */
  //#pragma library("./Modules/DoIP_Core.dll")
  //#include "DoIP_Transport.cin"

  /* C) CAN/CAN FD ISO-TP */
  //#include "CAN_Transport.cin"

  /* UDS 服务层 (必须在传输层之后) */
  #include "UDS_Services.cin"
}
```

**迁移步骤**: 上层测试脚本**无需任何修改**。只需:
1. 将 `DoIP_CANoe_Transport.cin` 放到工程目录
2. 在 `.can` 文件中 `#include "DoIP_CANoe_Transport.cin"` 替换原有传输层
3. 配置 `gDoIP_*` 参数
4. CANoe 中配好以太网通道和 IP 地址

---

## [v2.2] - 2026-03-27

### DoIP_Core.dll: UDS 缓冲区从 4KB 扩容到 16KB

**影响范围**: 使用 `DoIP_Transport.cin` + `DoIP_Core.dll` 的所有项目

**变更原因**: ECU 在 RequestDownload (0x34) 中协商的 `maxBlockLen` 可能远大于 4096（如 TBOX 协商 0x2800 = 10240）。旧 DLL 内部发送/接收缓冲区仅 4096+12 字节，导致 `DoIP_Send()` 返回 `DOIP_ERR_BUFFER (-10)` 发送失败。

**变更内容**:
| 文件 | 变更 |
|------|------|
| `DLL/DoIP_Core/doip_protocol.h` | `DOIP_MAX_UDS_LEN` 从 4096 → **16384**，与 CAPL 端 `gUDS_TxBuf[16384]` / `gUDS_RxBuf[16384]` 对齐 |
| `Modules/DoIP_Core.dll` | 重新编译 |

**影响的 DLL 内部缓冲区**:
- `DoIP_Send()` 中 `msgBuf[DOIP_MAX_MSG_LEN]`（DoIP_Core.cpp:64）— 发送上限从 4108 → 16396 字节
- `doip_tcp_recv_message()` 中 `payload[...]`（doip_tcp.cpp:345）— 接收上限同步扩大

**迁移步骤**: 替换 `Modules/DoIP_Core.dll` 即可，CAPL 端无需改动。

---

## [v2.1] - 2026-03-27

### 从 SecurityFlash 下游回推合并：KeepAlive 功能寻址、缓冲区扩容、API 简化

**影响范围**: 所有使用 CAN_Transport.cin + UDS_Services.cin 的项目

**变更来源**: SecurityFlash 项目在实际刷写测试中发现并修复的问题，回推到上游统一。详见 `SecurityFlash/docs/SYNC_DIFF_UDS-Core-CAPL.md`。

---

#### Breaking Change: `UDS_SecurityAccess_RequestSeed()` 签名变更

```c
// 旧 (v1.2~v2.0) — 3参数
int UDS_SecurityAccess_RequestSeed(byte seedOut[], int seedBufSize, int &seedActualLen);

// 新 (v2.1) — 2参数
int UDS_SecurityAccess_RequestSeed(byte seedOut[], int seedLen);
//   seedLen=0: 自动检测，使用响应中的全部Seed长度
//   seedLen>0: 截取到指定长度（解决部分ECU返回填充字节的问题）
//   实际长度通过全局变量 gUDS_SeedLen 获取
```

**迁移步骤**:
```c
// ---- 旧代码 ----
int seedLen;
seedLen = 0;
if (!UDS_SecurityAccess_RequestSeed(seedArray, elcount(seedArray), seedLen)) { ... }
UDS_FormatHex(seedArray, seedLen, ...);

// ---- 新代码 ----
if (!UDS_SecurityAccess_RequestSeed(seedArray, 0)) { ... }
// 使用 gUDS_SeedLen 获取实际长度
UDS_FormatHex(seedArray, gUDS_SeedLen, ...);
```

- 使用 `UDS_SecurityAccess_Unlock()` 的上层无需修改（内部已适配）

---

#### 新增函数: `UDS_DiagSessionControl(byte sessionType)`

通用诊断会话控制函数，支持任意 sessionType。原有 3 个便捷函数改为委托调用：
```c
int UDS_DiagSessionControl(byte sessionType);  // 通用 (新增)
int UDS_DiagSessionControl_Extended();          // 委托 → UDS_DiagSessionControl(0x03)
int UDS_DiagSessionControl_Programming();       // 委托 → UDS_DiagSessionControl(0x02)
int UDS_DiagSessionControl_Default();           // 委托 → UDS_DiagSessionControl(0x01)
```
- 进入非默认会话自动 `UDS_StartKeepAlive()`，回默认会话自动 `UDS_StopKeepAlive()`
- 支持供应商自定义会话（如 0x40、0x60 等）
- 使用 `_Extended` / `_Programming` / `_Default` 的上层无需修改

---

#### CAN_Transport.cin 变更明细

| # | 变更 | 级别 | 说明 |
|---|------|------|------|
| 1 | **KeepAlive 改为 0x7DF 功能寻址** | Bug fix | 3E 80 从物理寻址 `gUDS_TxId` 改为功能寻址 `0x7DF`，与物理通道 ISO-TP 传输隔离。删除 `gUDS_TxId==0` 和 `gIsoTp_TxBusy` 两个检查，KeepAlive 始终运行无需暂停。 |
| 2 | **UDS_Init: strncpy null terminator** | Bug fix | `strncpy` 后显式写入 `\0`，防止 `gUDS_EcuQualifier` 缓冲区未终止。 |
| 3 | **UDS_Init: diagStopTesterPresent()** | Bug fix | `diagSetTarget()` 后调用 `diagStopTesterPresent()`，防止 CANoe 诊断层自动发送 3E 00 与脚本的 3E 80 冲突。 |
| 4 | **FF 接收上限跟随缓冲区** | 兼容性 | `dlen > 4096` 改为 `dlen > elcount(gUDS_RxBuf)`，与缓冲区扩容联动。 |

**KeepAlive 功能寻址设计原则**:
```
KeepAlive (3E 80) 使用 0x7DF 功能寻址持续发送:
  - ECU 同时监听功能地址, 3E 80 能正常刷新 S3 定时器
  - 物理通道上的 ISO-TP 多帧传输不受影响
  - TransferData (34/36/37) 期间无需暂停 KeepAlive
  - NRC 0x37 惩罚等待期间无需手动发送 3E 80, 定时器已在运行
  - 不需要 gIsoTp_TxBusy 互斥, 不需要 gUDS_TxId == 0 防护
```

---

#### UDS_Services.cin 变更明细

| # | 变更 | 级别 | 说明 |
|---|------|------|------|
| 5 | **缓冲区 4096 → 16384** | 兼容性 | `gUDS_RxBuf` / `gUDS_TxBuf` 扩大到 16KB，支持 DoIP 模式下 10KB+ 的 TransferData 块。 |
| 6 | **P2* 超时 5000 → 10000** | 可靠性 | Flash 编程场景 ECU 擦写可能需要数秒，5000ms 不够。10000ms 经实测验证。 |
| 7 | **新增 `gUDS_UseFD` 声明** | 正确性 | `int gUDS_UseFD = 1`，CAN_Transport.cin 已在引用此变量，声明在服务层更统一。 |
| 8 | **新增 `gUDS_SeedLen` 声明** | 正确性 | `int gUDS_SeedLen = 0`，配合 `RequestSeed` 新签名，通过全局变量输出实际 Seed 长度。 |
| 9 | **DiagSessionControl 通用化** | 代码质量 | 新增 `UDS_DiagSessionControl(byte sessionType)`，3 个便捷函数改为委托，消除代码重复。 |
| 10 | **RequestSeed 改为 2 参数** | Breaking | 签名从 `(seedOut[], seedBufSize, &seedActualLen)` 改为 `(seedOut[], seedLen)`，详见上方。 |
| 11 | **NRC 0x37 删除手动 3E 80** | Bug fix | 删除 NRC 0x37 等待循环中手动构造 CAN 报文发送 3E 80 的代码。KeepAlive 定时器已在 0x7DF 上持续发送，无需手动构造。原代码使用 `message *` 类型在 DoIP 模式下会编译失败。 |
| 12 | **ECUReset FormatRxSummary(3)** | Bug fix | 失败时 `UDS_FormatRxSummary(2)` → `(3)`，多输出一个 NRC 字节，日志更完整。 |
| 13 | **RoutineControl 动态上限** | 兼容性 | `payloadInLen > 4092` → `payloadInLen > elcount(gUDS_TxBuf) - 4`，与缓冲区大小解耦。 |
| 14 | **TransferData 动态上限** | 兼容性 | `dataLen > 4094` → `dataLen > elcount(gUDS_TxBuf) - 2`，同上。 |

---

#### 迁移检查清单

1. **使用 `UDS_SecurityAccess_RequestSeed` 的代码**: 必须适配新的 2 参数签名
2. **使用 `UDS_SecurityAccess_Unlock` 的代码**: 无需修改
3. **使用 `UDS_DiagSessionControl_*` 的代码**: 无需修改
4. **依赖 `gIsoTp_TxBusy` 控制 KeepAlive 的代码**: 不再需要，可删除 Suspend/Resume 逻辑
5. **NRC 0x37 等待期间手动发 3E 80 的代码**: 不再需要，KeepAlive 定时器自动覆盖
6. **上层声明 `gUDS_UseFD` 的代码**: 检查是否与 UDS_Services.cin 中的声明重复

### 新增: 独立 DoIP DLL，脱离 CANoe DoIP Modeling Library 依赖

**影响范围**: 使用 `DoIP_Transport.cin` 的所有项目

**变更原因**: 原 DoIP 传输层完全依赖 CANoe 内置 DoIP.DLL (Modeling Library)，必须在 CANoe Simulation Setup 中配置 DoIP 节点才能使用。本次开发自定义 `DoIP_Core.dll`，通过 WinSock2 原生 TCP/UDP 实现 ISO 13400-2 DoIP 协议，彻底去除对 CANoe DoIP 建模库的依赖。

**新增文件**:
| 文件 | 说明 |
|------|------|
| `DLL/DoIP_Core/DoIP_Core.cpp` | CAPL DLL 导出层 + caplDllTable4 (11个函数) |
| `DLL/DoIP_Core/doip_protocol.h/cpp` | DoIP 协议层: 报文构建/解析 (ISO 13400-2) |
| `DLL/DoIP_Core/doip_tcp.h/cpp` | TCP/UDP 连接管理 + KeepAlive 后台线程 (WinSock2) |
| `DLL/DoIP_Core/CMakeLists.txt` | CMake 构建配置 (Win32 + /MT 静态CRT) |
| `DLL/DoIP_Core/cdll.h` | Vector CAPL DLL 接口头文件 |
| `Modules/DoIP_Core.dll` | 编译产物 (x86, 82KB, 零外部依赖) |

**DoIP_Core.dll 导出函数 (CAPL 可调用)**:
| 函数 | 说明 |
|------|------|
| `DoIP_CreateConnection(localIp, remoteIp, testerAddr, ecuAddr)` | 配置连接参数 |
| `DoIP_SetTimeout(controlMs, diagMs)` | 设置超时 |
| `DoIP_Connect()` | TCP 连接 + Routing Activation (阻塞) |
| `DoIP_Send(targetAddr, data[], len)` | 发送 UDS 数据 (自动 DoIP 封装) |
| `DoIP_Recv(data[], bufSize, timeoutMs)` | 接收 UDS 响应 (同步阻塞) |
| `DoIP_StartKeepAlive(intervalMs)` | 启动 TesterPresent 后台线程 |
| `DoIP_StopKeepAlive()` | 停止 KeepAlive |
| `DoIP_Disconnect()` | 断开连接 |
| `DoIP_GetStatus()` | 查询连接状态 |
| `DoIP_GetLastError()` | 查询最后错误码 |
| `DoIP_VehicleIdentification(broadcastIp, vinOut[], logicalAddr, timeoutMs)` | UDP 车辆发现 |

**DoIP_Transport.cin 改造** (283行 → ~140行):
- 删除: `on preStart` 中的 `DoIP_InitAsTester()` 等 CANoe DLL 初始化
- 删除: 4个 CANoe 回调函数 (`DoIP_DataInd`, `DoIP_DataCon`, `_DoIP_RoutingActivationResponse`, `DoIP_ErrorInd`)
- 删除: `msTimer gUDS_KeepAliveTimer` 及 `on timer` 回调 (KeepAlive 改由 DLL 内部线程管理)
- 新增: `#pragma library("./Modules/DoIP_Core.dll")` 加载自定义 DLL
- 改造: `UDS_SendRaw()` 从事件驱动回调改为同步阻塞 (`DoIP_Send` + `DoIP_Recv`)
- 改造: `UDS_StartKeepAlive()` / `UDS_StopKeepAlive()` 转发 DLL 后台线程

**架构对比**:
```
之前:  CAPL → CANoe DoIP.DLL (Modeling Library) → CANoe 网络栈 → Vector 硬件
现在:  CAPL → DoIP_Core.dll (自定义) → WinSock2 → Windows TCP/IP → 任意网卡
```

**关键设计**:
- **同步阻塞模式**: `DoIP_Recv()` 内部使用 `select()` + `recv()`，阻塞 CAPL 测试线程直到收到响应或超时。与 CAN Transport 的 `testWaitForTextEvent` 行为效果一致。
- **KeepAlive 线程安全**: DLL 内部 `CreateThread` 后台发送 3E 80，与主线程 `send()` 通过 `CRITICAL_SECTION` 互斥。
- **自动处理**: Alive Check Request 自动回复、Diagnostic Positive ACK 自动跳过、NRC 0x78 仍在 CAPL 侧循环。
- **SecurityAccess 不变**: Seed&Key 仍走 CANoe 的 `diagGenerateKeyFromSeed()`，只有传输层独立。
- **协议参考**: libdoip (https://github.com/AVL-DiTEST-DiagDev/libdoip) 的协议层逻辑。

**不再需要的 CANoe 配置**:
- CANoe Simulation Setup 中的 DoIP Modeling Library
- CANoe 网络配置中的 DoIP 节点/通道
- Vector 硬件 (VN5620 等仍可用，但作为普通 Windows 网卡使用，普通网口也能用)

**网络配置**: 直接在 Windows 网卡属性中配置 IP，`gDoIP_LocalIp` 填对应网卡 IP 即可。

**编译方法**:
```bat
cd DLL\DoIP_Core
cmake -B build -A Win32
cmake --build build --config Release
:: 拷贝 build\Release\DoIP_Core.dll → Modules\DoIP_Core.dll
```

**迁移步骤**: 上层测试脚本**无需任何修改**。`UDS_Init()` / `UDS_SendRaw()` / `UDS_StartKeepAlive()` 等接口签名完全不变。只需:
1. 将 `Modules/DoIP_Core.dll` 放到工程目录
2. 使用新的 `DoIP_Transport.cin`
3. Windows 网卡配好 IP

---

## [v1.5] - 2026-03-22

### 修复: ECUReset 未停止 KeepAlive & SecurityAccess 子功能号硬编码

**影响范围**: 所有使用 `UDS_ECUReset_Hard()` 和 `UDS_SecurityAccess_*` 的项目

**修复内容**:

1. **`UDS_ECUReset_Hard()` — 复位前停止 S3 保活**
   - 复位后 ECU 重启，若不停止 KeepAlive，会在 ECU 未就绪时发 3E 导致总线错误
   - 修复: 发送 `11 01` 前调用 `UDS_StopKeepAlive()`

2. **`UDS_SecurityAccess_RequestSeed()` / `SendKey()` — 子功能号改用 `gUDS_SecurityLevel`**
   - 原代码 RequestSeed 硬编码 `27 01`，SendKey 硬编码 `27 02`
   - 但 `UDS_GenerateKeyFromSeed()` 传给 DLL 的是 `gUDS_SecurityLevel`，导致不一致
   - 修复: RequestSeed 改为 `27 [gUDS_SecurityLevel]`，SendKey 改为 `27 [gUDS_SecurityLevel+1]`
   - PKI 项目 (level=0x01) 行为不变；SecurityFlash 项目 (level=0x11) 现在正确发送 `27 11` / `27 12`

**迁移步骤**: 使用非默认 SecurityLevel 的项目需在调用安全访问前设置 `gUDS_SecurityLevel`：
```c
gUDS_SecurityLevel = 0x11;  // Flash-level security
UDS_SecurityAccess_Unlock();
```

**同步变更的文件**:
- `UDS_Services.cin` — 3处修改

---

## [v1.4] - 2026-03-19

### 新增: 7个UDS服务函数 (从SecurityFlash项目同步)

**影响范围**: 无（纯新增函数，上层无需修改现有调用）

**变更原因**: SecurityFlash 项目为刷写流程新增了 $11/$28/$2E/$34/$36/$37/$85 共 7 个 UDS 服务封装，同步到 PKI-CAPL 保持底层一致。

**新增函数**:
| 函数 | SID | 说明 |
|------|-----|------|
| `UDS_ECUReset_Hard()` | $11 01 | ECU硬复位 |
| `UDS_CommunicationControl(subFunc, commType)` | $28 | 通信控制 (enable/disable Rx&Tx) |
| `UDS_ControlDTCSetting(subFunc)` | $85 | 控制DTC设置 (on/off) |
| `UDS_RequestDownload(dataFmtId, addrLenFmtId, addr, size, &maxBlockLen)` | $34 | 请求下载，自动解析MaxBlockLength |
| `UDS_TransferData(blockSeq, data[], dataLen)` | $36 | 传输数据 (最大4094字节/块) |
| `UDS_RequestTransferExit()` | $37 | 请求传输退出 |
| `UDS_WriteDID(did, dataIn[], dataInLen)` | $2E | 写入DID |

**未同步的差异** (PKI-CAPL版本更优，保持不变):
- `UDS_DiagSessionControl` — PKI版有KeepAlive联动，SecurityFlash的通用版无此逻辑
- `UDS_SecurityAccess_RequestSeed` — PKI版有NRC 0x37惩罚等待+保活重试
- `gUDS_SeedLen` 全局变量 — PKI用局部变量传递，设计更干净

**同步变更的文件**:
- `UDS_Services.cin` — 新增7个服务函数，头部注释更新服务列表

**来源项目**: `SecurityFlash`

---

## [v1.3] - 2026-03-18

### 新增: NRC 0x37 自动等待重试

**影响范围**: 无（纯内部变更，上层无需修改）

**变更原因**: 连续测试用例中，前一个用例发送错误 Key 触发 ECU 安全访问惩罚计时器后，后续用例的 `RequestSeed` 会收到 NRC 0x37 (requiredTimeDelayNotExpired) 导致失败。

**变更内容**:
- `UDS_SecurityAccess_RequestSeed()` 内部检测到 NRC 0x37 时，自动等待 `cSA_PenaltyDelay` 毫秒后重试一次
- 等待期间每 1 秒发送 `3E 80`（TesterPresent, suppressPositiveResponse）保持诊断会话不超时
- `PKI_Config.cin` 新增配置参数 `cSA_PenaltyDelay = 10000`（默认 10 秒），可按 ECU 实际惩罚时间调整
- Write 窗口会输出日志: `NRC 0x37 (requiredTimeDelayNotExpired), waiting XXXX ms...`

**同步变更的文件**:
- `UDS_Services.cin` — RequestSeed 加入 0x37 重试
- `UDS_Transport.cin` — RequestSeed 加入 0x37 重试
- `PKI_Config.cin` — 新增 `cSA_PenaltyDelay`

**关联项目**: `SecurityAccess` 项目已同步此变更（`UDS_Transport.cin` + `SA_Config.cin`）

---

## [v1.2] - 2026-03-16

### 🔧 Breaking Change: SecurityAccess Seed长度动态化

**影响范围**: 所有调用 `UDS_SecurityAccess_RequestSeed()` 的代码

**变更原因**: 不同ECU的Seed长度不同（GW=16字节, TBOX=4字节），原代码硬编码16字节导致TBOX解锁失败。

**函数签名变更**:
```c
// 旧 (v1.1)
int UDS_SecurityAccess_RequestSeed(byte seedOut[], int seedLen);

// 新 (v1.2)
int UDS_SecurityAccess_RequestSeed(byte seedOut[], int seedBufSize, int &seedActualLen);
```

**迁移步骤**:
1. 新增局部变量 `int seedLen;`
2. 调用改为 `UDS_SecurityAccess_RequestSeed(seedArray, elcount(seedArray), seedLen);`
3. 后续使用 `seedLen` 替代原来硬编码的 `16`（FormatHex、GenerateKeyFromSeed等）

**示例**:
```c
// ---- 旧代码 ----
byte seedArray[32];
if (!UDS_SecurityAccess_RequestSeed(seedArray, 16)) { ... }
UDS_FormatHex(seedArray, 16, hexStr, elcount(hexStr));
UDS_GenerateKeyFromSeed(seedArray, 16, keyArray, elcount(keyArray), keyLen);

// ---- 新代码 ----
byte seedArray[32];
int  seedLen;
if (!UDS_SecurityAccess_RequestSeed(seedArray, elcount(seedArray), seedLen)) { ... }
UDS_FormatHex(seedArray, seedLen, hexStr, elcount(hexStr));
UDS_GenerateKeyFromSeed(seedArray, (dword)seedLen, keyArray, elcount(keyArray), keyLen);
```

**内部变更** (无需上层处理):
- `UDS_SecurityAccess_Unlock()` 内部已适配新签名
- `Flow_SecurityUnlock()` 调用 `Unlock()`，无需修改

---

## [v1.1] - 2026-03-16

### 新增: CAN FD / 经典CAN切换支持

**影响范围**: 无（纯内部变更，上层无需修改）

**变更内容**:
- `UDS_SendRaw()` 和 `IsoTp_SendFC()` 中的 `msg.FDF` 从硬编码 `1` 改为读取 `gUDS_UseFD` 配置变量
- SF分支判断: `txLen <= 62` → `txLen <= 62 && gUDS_UseFD`（经典CAN时SF上限为7字节）

**配置方法**: 在 `PKI_Config.cin` 的ECU配置段中设置:
```c
byte gUDS_UseFD = 1;  // 1=CAN FD, 0=经典CAN
```

---

## [v1.0] - 2026-03-14

### 初始版本

- ISOTP 传输层: SF/FF/CF/FC 收发, CAN FD扩展SF, NRC 0x78处理
- UDS 服务: 10 03, 14 FF FF FF, 22 DID, 27 01/02, 31 01
- 动态CAN ID: `UDS_Init(txId, rxId, ecuQualifier)`
- CRC32 计算: `UDS_CalcCRC32()`
