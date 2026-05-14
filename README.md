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
├─────────────────────────────────┤
│     UDS_Transport.cin 传输层     │  ← CAN/DoIP 双模，gUDS_CommMode 切换
├──────────────┬──────────────────┤
│  CAN ISO-TP  │ DoIP (CANoe      │
│  (手动分帧)  │  IP_Endpoint API)│
├──────────────┴──────────────────┤
│  SecurityKeyBridge.dll          │  ← CAPL DLL: 动态加载 Seed&Key
└─────────────────────────────────┘
```

- 服务层（`UDS_Services.cin`）：收发缓冲区、UDS 服务函数、NRC 处理、CRC32、日志格式化
- 传输层（`UDS_Transport.cin`）：CAN/DoIP 双模统一传输，通过 `gUDS_CommMode` 切换
- DoIP 协议栈：内置于传输层，使用 CANoe IP_Endpoint TCP API，Trace 窗口可见
- SeedKey DLL（`Modules/SecurityKeyBridge.dll`）：动态加载厂商 Seed&Key DLL，替代 `diagGenerateKeyFromSeed()`

## 使用方式

在 CANoe CAPL 测试节点中按顺序 include：

```c
includes
{
  #pragma library("./Modules/SecurityKeyBridge.dll")
  #include "UDS_Transport.cin"
  #include "UDS_Services.cin"
}
```

初始化后即可调用服务函数：

```c
variables
{
  /* 传输模式: 0=CAN, 1=DoIP */
  int gUDS_CommMode = 0;
}

on start
{
  UDS_Init(0x741, 0x641, "ECU_Name");
}

testcase TC_ReadDID()
{
  byte data[256];
  int  len;
  UDS_DiagSessionControl_Extended();
  UDS_ReadDID(0xF190, data, len);
}
```

### 安全访问 (0x27)

`UDS_SecurityAccess_Unlock()` 通过 SecurityKeyBridge.dll 动态加载厂商 Seed&Key DLL 计算 Key，调用前需设置 DLL 路径和安全等级：

```c
variables
{
  /* Seed&Key DLL 完整路径（支持中文路径）
   * 应用级和刷写级可使用不同 DLL，按 gUDS_SecurityLevel 自动选择:
   *   0x11/0x19 → gUDS_SeedKeyDllPath_Flash (刷写级)
   *   其余      → gUDS_SeedKeyDllPath       (应用级)
   * 如果 gUDS_SeedKeyDllPath_Flash 为空，统一使用应用级 DLL */
  char gUDS_SeedKeyDllPath[512]       = "C:\\project\\Modules\\SeedKey\\app_level.dll";
  char gUDS_SeedKeyDllPath_Flash[512] = "C:\\project\\Modules\\SeedKey\\flash_level.dll";
}

testcase TC_SecurityAccess()
{
  UDS_DiagSessionControl_Extended();
  gUDS_SecurityLevel = 0x01;           // 应用级 → 使用 gUDS_SeedKeyDllPath
  UDS_SecurityAccess_Unlock();

  gUDS_SecurityLevel = 0x11;           // 刷写级 → 使用 gUDS_SeedKeyDllPath_Flash
  UDS_SecurityAccess_Unlock();
}
```

**DLL 路径注意事项**：

| 路径类型 | 解析基准 | 示例 |
|----------|----------|------|
| `#pragma library("./Modules/xxx.dll")` | 相对于 `.can` 文件 | CAPL 编译器处理 |
| `getProfileString(..., iniPath)` | 相对于 `.cfg` 文件 | CANoe 内部处理 |
| `LoadLibrary("xxx.dll")` (DLL内部) | 相对于 CANoe 进程 CWD (安装目录) | Windows API |

三者的路径解析基准不同，是常见的混淆点。**建议 `gUDS_SeedKeyDllPath` 使用绝对路径**，避免 `LoadLibrary` 在 CANoe 安装目录下找不到 DLL（返回 -326 = ERROR_MOD_NOT_FOUND）。

**注意**：厂商 Seed&Key DLL 通常依赖 `vcruntime140.dll` 等 CRT，需将这些依赖复制到 Seed&Key DLL 同目录，详见 `DLL/SecurityKeyBridge/SecurityKeyBridge.c` 头注释。

### 多路 CAN 通道

传输层内置多通道支持，通过 `gCAN_ActiveChannel` 控制发送和接收的 CAN 通道：

```c
variables
{
  /* 通道常量 (UDS_Transport.cin 已定义) */
  /* CH_PRIMARY   = 1   CAN1 — 默认通道 */
  /* CH_AUXILIARY = 2   CAN2 — 辅助通道 */
  /* CH_MONITOR   = 3   CAN3 — 监听通道 */

  /* 切换活跃通道 (默认 CAN1, 无需修改即兼容单通道) */
  /* gCAN_ActiveChannel = 2; */
}
```

典型场景：台架测试中 CAN1 接诊断，网关转发到其他总线，其他总线并联接入 CANoe CAN2，通过切换 `gCAN_ActiveChannel` 或在不同通道上分别监听来验证转发行为。