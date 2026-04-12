# experiment/ 双模传输层变更日志

> 本文件记录 `experiment/UDS_Transport.cin` + `UDS_Services.cin` 的变更。

---

## [v2.0-unified] - 2026-04-12

### 新增: CAN/DoIP 双模统一传输层

将 `CAN_Transport.cin` 和 `DoIP_Transport.cin` 合并为 `UDS_Transport.cin`，
通过 `gUDS_CommMode` 变量在运行时选择底层传输方式，上层代码零修改。

**架构**:
```
gUDS_CommMode = 0 → CAN (ISO-TP, 手动分帧)
gUDS_CommMode = 1 → DoIP (TCP/IP, DoIP_Core.dll)
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
   - 按 `gUDS_CommMode` 分支: CAN 用 0x7DF 功能寻址, DoIP 用 `DoIP_Send`
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
