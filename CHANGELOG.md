# UDS_Transport.cin 底层变更日志

> 本文件记录 `UDS_Transport.cin` 底层传输/服务层的接口变更。
> 其他项目引用了此底层时，需根据本日志同步修改上层调用代码。

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
