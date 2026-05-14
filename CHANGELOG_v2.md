# UDS-Core-CAPL v2 变更日志

> 本文件记录 `UDS_Transport.cin` + `UDS_Services.cin` 的变更。

---

## [v2.4.0] - 2026-05-14

### 新增: 多路 CAN 通道支持

传输层新增通道常量和活跃通道变量，所有 CAN 发送/接收均通过 `gCAN_ActiveChannel` 显式指定通道，支持多路 CAN 台架测试场景（如诊断转发验证）。

**改动文件**:

| 文件 | 改动 |
|------|------|
| `UDS_Transport.cin` | 新增 `CH_PRIMARY/CH_AUXILIARY/CH_MONITOR` 常量 + `gCAN_ActiveChannel` 变量；`on message` 入口加通道过滤；`IsoTp_SendFC()`、`UDS_SendRaw_CAN()`、KeepAlive 发送均加 `msg.can = gCAN_ActiveChannel` |

**向后兼容**: `gCAN_ActiveChannel` 默认值为 1 (CAN1)，不修改此变量的项目行为完全不变。

**用法**:
```c
/* 切换到 CAN2 发送/接收 */
gCAN_ActiveChannel = CH_AUXILIARY;
UDS_SendRaw();

/* 恢复默认 */
gCAN_ActiveChannel = CH_PRIMARY;
```

---

## [v2.3.1] - 2026-04-28

### 变更: cUDS_InterReqDelay 去除 const 限定

`cUDS_InterReqDelay` 从 `const int` 改为 `int`，允许下游项目在运行时临时覆盖请求间隔。
典型场景：SecurityFlash 的 TransferData (0x36) 循环中临时置零以消除不必要的 100ms 延迟
（ECU 的 0x76 正响应已确认就绪），循环结束后恢复原值。

**改动文件**:

| 文件 | 改动 |
|------|------|
| `UDS_Services.cin` | `const int cUDS_InterReqDelay` → `int cUDS_InterReqDelay`，注释补充"非const, Flash可临时覆盖" |

---

## [v2.3-seedkey-flash] - 2026-04-24

### 新增: 双 Seed&Key DLL 支持 (应用级/刷写级)

不同安全级别可能需要不同的 Seed&Key DLL（如应用级 0x01 和刷写级 0x11 使用不同算法）。
新增 `gUDS_SeedKeyDllPath_Flash` 变量，`UDS_GenerateKeyFromSeed` 根据当前
`gUDS_SecurityLevel` 自动选择对应 DLL。

**改动文件**:

| 文件 | 改动 |
|------|------|
| `UDS_Services.cin` | 新增 `gUDS_SeedKeyDllPath_Flash` 变量；`UDS_GenerateKeyFromSeed` 按安全级别选择 DLL (0x11/0x19 用刷写级，其余用应用级)；错误日志输出实际 DLL 路径 |

**用法**:
```c
gUDS_SeedKeyDllPath       = "C:/project/Modules/SeedKey/app_level.dll";
gUDS_SeedKeyDllPath_Flash = "C:/project/Modules/SeedKey/flash_level.dll";
gUDS_SecurityLevel = 0x01;  // → 使用 gUDS_SeedKeyDllPath
gUDS_SecurityLevel = 0x11;  // → 使用 gUDS_SeedKeyDllPath_Flash
```

**DLL 路径注意事项**:
- `LoadLibrary` 路径相对于 CANoe 进程工作目录（安装目录）解析，不是项目根目录
- `#pragma library` 路径相对于 `.can` 文件解析（CAPL 编译器处理）
- `getProfileString` 路径相对于 `.cfg` 文件解析（CANoe 内部处理）
- 建议传入绝对路径，避免路径解析歧义

### 修复: DoIP KeepAlive 改为请求期间自动停发

原方案（v2.2 commit 219bb00）将 DoIP KeepAlive 改用功能寻址 TA=0xE400，
但实测发现部分 ECU 即使收到功能寻址的 3E 80 也会中断当前请求处理。

新方案：新增 `gUDS_TxPending` 标志，`UDS_SendRaw_DoIP` 执行期间自动置 1，
KeepAlive timer 检测到此标志时跳过发送，请求完成后恢复。
DoIP KeepAlive 恢复使用物理寻址 `gDoIP_EcuAddr`（与 ECU 的正常诊断通道一致）。

**改动文件**:

| 文件 | 改动 |
|------|------|
| `UDS_Transport.cin` | 新增 `gUDS_TxPending` 标志；`UDS_SendRaw_DoIP` 入口置 1、所有返回点清 0；KeepAlive timer DoIP 分支增加 `if (gUDS_TxPending) return;` 检查；移除 0xE400 功能寻址，恢复 `gDoIP_EcuAddr` 物理寻址 |

**CAN 模式不受影响**: CAN KeepAlive 仍使用 0x7DF 功能寻址 + `gIsoTp_TxBusy` 互斥。

---

## [v2.2-doip-native] - 2026-04-13

### 重构: DoIP 传输层从 DLL 迁移到 CANoe IP_Endpoint API

将 DoIP 模式从自定义 `DoIP_Core.dll` (WinSock2) 迁移到 CANoe 原生
IP_Endpoint TCP API，所有 DoIP 报文经过 CANoe 网络栈，Trace 窗口可见。

**动机**: DLL 方式的 TCP 通信绕过 CANoe，Trace 中看不到 DoIP 报文，
调试困难。IP_Endpoint API 方式所有收发都在 CANoe 内部，完整可追溯。

**改动文件**:

| 文件 | 改动 |
|------|------|
| `UDS_Transport.cin` | 完整 DoIP 协议栈 (帧构建/解析、TCP 回调、帧分发)，替换所有 DLL 调用 |
| `UDS_Services.cin` | NRC 0x37 等待中的 `DoIP_Send()` 替换为 `DoIP_BuildDiagMsg` + `DoIP_TcpSendBytes` |

**新增功能**:
- `UDS_Reconnect()` — ECU 复位后重连 (重试循环，自动恢复 KeepAlive)
- `cDoIP_ReconnectMaxWait` / `cDoIP_ReconnectInterval` — 可配置重连参数

**DoIP 协议栈 (CAPL 内实现)**:
- `DoIP_BuildHeader` / `DoIP_ParseHeader` — ISO 13400-2 通用头
- `DoIP_BuildRoutingActReq` — Routing Activation 请求
- `DoIP_BuildDiagMsg` — Diagnostic Message 封装
- `DoIP_BuildAliveCheckResp` — Alive Check 响应
- `DoIP_TcpSendBytes` — byte[] → char[] 转换发送
- `OnTcpConnect` / `OnTcpReceive` / `OnTcpClose` — TCP 回调 (流式拼包)
- `DoIP_HandleFrame` — 帧分发 (RA/DiagMsg/PosAck/NegAck/AliveCheck)

**破坏性变更**:
- `.can` 文件不再需要 `#pragma library("./Modules/DoIP_Core.dll")`
- `DoIP_Core.dll` 不再被引用，可从 Modules/ 移除

**Code Review 修复**:
- P1: 移除 `UDS_Services.cin` 中残留的 `DoIP_Send()` 调用
- P2: `UDS_Reconnect()` 成功后自动恢复 KeepAlive
- P2: `UDS_Init()` 失败路径关闭 socket 防止泄漏

---

## [v2.1-seedkey] - 2026-04-12

### 新增: SecurityKeyBridge.dll — 独立 Seed&Key 桥接

集成 SecurityKeyBridge DLL，替代 CANoe 内置 `diagGenerateKeyFromSeed()`，
实现运行时动态加载任意厂商 Seed&Key DLL，不再依赖 CANoe 诊断配置。

**改动文件**:

| 文件 | 改动 |
|------|------|
| `DLL/SecurityKeyBridge/SecurityKeyBridge.c` | 完整版源码：UTF-8 路径、SEH 保护、extern "C"、诊断函数 |
| `DLL/SecurityKeyBridge/CMakeLists.txt` | CMake 构建配置（/MT + Win32） |
| `DLL/SecurityKeyBridge/cdll.h` | Vector CAPL DLL 接口头文件 |
| `Modules/SecurityKeyBridge.dll` | 编译产物（x86, 仅依赖 KERNEL32.dll） |
| `UDS_Services.cin` | `UDS_GenerateKeyFromSeed` 改用 `SK_GenerateKey`，新增 `gUDS_SeedKeyDllPath` |
| `UDS_Transport.cin` | 头注释更新 pragma 引用说明 |

**DLL 导出函数**:
- `SK_GenerateKey(dllPath, seed, seedLen, secLevel, key, maxKeyLen)` — 核心：加载 DLL 并计算 Key
- `SK_LoadTest(dllPath)` — 诊断：仅测试 LoadLibrary
- `SK_DummyTest(...)` — 诊断：不用 LoadLibrary，验证调用链
- `SK_Ping(value)` — 诊断：返回 input+1

**错误码**:
- `-1` = 路径为空
- `-3` = DLL 中找不到 GenerateKeyEx 函数
- `-4` = GenerateKeyEx 返回非零
- `-10` = LoadLibrary 时 SEH 异常
- `-11` = GenerateKeyEx 调用时 SEH 异常
- `-200-N` = LoadLibrary 失败，N = GetLastError() % 1000（如 -326 = ERROR_MOD_NOT_FOUND）

---

## [v2.0-unified] - 2026-04-12

### 新增: CAN/DoIP 双模统一传输层

将 `CAN_Transport.cin` 和 `DoIP_Transport.cin` 合并为 `UDS_Transport.cin`，
通过 `gUDS_CommMode` 变量在运行时选择底层传输方式，上层代码零修改。

**架构**:
```
gUDS_CommMode = 0 → CAN (ISO-TP, 手动分帧)
gUDS_CommMode = 1 → DoIP (CANoe IP_Endpoint TCP API)
```

**统一接口** (签名不变):
- `UDS_Init(txId, rxId, ecuQualifier)`
- `UDS_SendRaw()`
- `UDS_StartKeepAlive()` / `UDS_StopKeepAlive()`
- `UDS_Deinit()` (新增)

### Code Review 后修复 (2026-04-12)

基于两位独立 reviewer 的并行审查，修复以下问题:

**Critical 修复**:

1. **KeepAlive 恢复功能寻址 0x7DF**
   - 原合并版误改为 `gUDS_TxId` 物理寻址
   - 多帧接收期间会干扰 ISO-TP 状态机
   - 恢复原版设计并补充注释说明原因

2. **补上 `diagStopTesterPresent()` 调用**
   - `UDS_Init()` 中 `diagSetTarget` 成功后调用
   - 防止 CANoe 诊断层自动发 3E 00 干扰手动 ISO-TP

3. **NRC 0x37 等待逻辑 DoIP 兼容**
   - 按 `gUDS_CommMode` 分支: CAN 用 0x7DF 功能寻址, DoIP 用 IP_Endpoint API 发送
   - 修复 DoIP 模式下向 CAN ID 0x000 发垃圾帧的问题

**Important 修复**:

4. **DoIP 分支清零 TxId/RxId**
   - 防止 CAN→DoIP 切换后 `on message` 回调误触发

5. **缓冲区恢复 16384 字节**
   - 从 4096 恢复, 支持 DoIP 大响应和刷写场景

6. **P2* 超时恢复 10000ms**
   - 从 5000 恢复, Flash 编程期间 ECU 可能需要更长处理时间

7. **超时类型 `const int` → `const long`**
   - 防止超过 32767ms 时 16 位 int 溢出

8. **FF 上限改为 `elcount(gUDS_RxBuf)`**
   - 从硬编码 4096 改为动态引用, 与缓冲区大小自动同步

9. **新增 `UDS_Deinit()` 清理接口**
   - 停止 KeepAlive, 断开 DoIP 连接, 清零状态
   - 支持运行时切换 CommMode 和测试结束清理
