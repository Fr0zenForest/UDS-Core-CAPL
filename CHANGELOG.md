# UDS_Transport.cin 底层变更日志

> 本文件记录 `UDS_Transport.cin` 底层传输/服务层的接口变更。
> 其他项目引用了此底层时，需根据本日志同步修改上层调用代码。

---

## [v2.0] - 2026-03-25

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
