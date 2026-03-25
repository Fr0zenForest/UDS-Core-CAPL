/******************************************************************************
 * DoIP_Core.cpp - CAPL DLL 导出层
 *
 * 通过 #pragma library("./Modules/DoIP_Core.dll") 加载。
 * 提供独立于 CANoe DoIP Modeling Library 的 DoIP 传输功能。
 *
 * 编译: cmake -B build -A Win32 && cmake --build build --config Release
 * 部署: 拷贝 build/Release/DoIP_Core.dll → 项目/Modules/DoIP_Core.dll
 ******************************************************************************/

/* CAPL DLL 宏定义 (必须在 cdll.h 之前) */
#define USECDLL_FEATURE
#define _BUILDNODELAYERDLL
#include "cdll.h"

#include "doip_protocol.h"
#include "doip_tcp.h"

#include <cstdint>
#include <cstring>

/* ========== CAPL 导出函数 ========== */

/**
 * 初始化连接参数 (不建立连接)
 * @return 0=成功, <0=错误
 */
int32_t CAPLCDECL DoIP_CreateConnection(const char* localIp,
                                         const char* remoteIp,
                                         uint32_t testerAddr,
                                         uint32_t ecuAddr)
{
    return doip_tcp_init(localIp, remoteIp,
                          (uint16_t)testerAddr, (uint16_t)ecuAddr);
}

/**
 * 设置超时时间 (ms)
 * @return 0
 */
int32_t CAPLCDECL DoIP_SetTimeout(uint32_t controlMs, uint32_t diagMs)
{
    doip_tcp_set_timeout(controlMs, diagMs);
    return 0;
}

/**
 * 建立 TCP 连接 + Routing Activation
 * @return 0=成功, <0=错误码
 */
int32_t CAPLCDECL DoIP_Connect(void)
{
    return doip_tcp_connect();
}

/**
 * 发送 UDS 诊断数据 (自动 DoIP 封装)
 * @return 0=成功, <0=错误
 */
int32_t CAPLCDECL DoIP_Send(uint32_t targetAddr,
                             const unsigned char* data,
                             uint32_t len)
{
    uint8_t msgBuf[DOIP_MAX_MSG_LEN];
    int msgLen = doip_build_diagnostic_message(msgBuf, sizeof(msgBuf),
                                                doip_tcp_get_tester_addr(),
                                                (uint16_t)targetAddr,
                                                data, (int)len);
    if (msgLen < 0)
        return DOIP_ERR_BUFFER;

    return doip_tcp_send(msgBuf, msgLen);
}

/**
 * 接收 UDS 诊断响应 (同步阻塞)
 * @return >0=UDS字节数, 0=超时, <0=错误
 */
int32_t CAPLCDECL DoIP_Recv(unsigned char* data,
                             uint32_t bufSize,
                             uint32_t timeoutMs)
{
    return doip_tcp_recv_message(data, (int)bufSize, timeoutMs);
}

/**
 * 启动 KeepAlive 后台线程 (TesterPresent 3E 80)
 * @return 0=成功, <0=错误
 */
int32_t CAPLCDECL DoIP_StartKeepAlive(uint32_t intervalMs)
{
    return doip_ka_start(intervalMs);
}

/**
 * 停止 KeepAlive 后台线程
 * @return 0=成功
 */
int32_t CAPLCDECL DoIP_StopKeepAlive(void)
{
    return doip_ka_stop();
}

/**
 * 断开 TCP 连接
 * @return 0
 */
int32_t CAPLCDECL DoIP_Disconnect(void)
{
    doip_tcp_disconnect();
    return 0;
}

/**
 * 获取连接状态
 * @return 0=未连接, 1=已连接, -1=错误
 */
int32_t CAPLCDECL DoIP_GetStatus(void)
{
    return doip_tcp_get_status();
}

/**
 * 获取最近一次错误码
 */
int32_t CAPLCDECL DoIP_GetLastError(void)
{
    return doip_tcp_get_last_error();
}

/**
 * UDP 车辆发现
 * @return 0=成功, <0=错误
 */
int32_t CAPLCDECL DoIP_VehicleIdentification(const char* broadcastIp,
                                               unsigned char* vinOut,
                                               uint32_t* logicalAddr,
                                               uint32_t timeoutMs)
{
    uint16_t addr = 0;
    int ret = doip_udp_vehicle_ident(broadcastIp, vinOut, &addr, timeoutMs);
    if (ret == DOIP_OK)
        *logicalAddr = (uint32_t)addr;
    return ret;
}

/* ========== CAPL DLL 导出表 ========== */

CAPL_DLL_INFO4 table[] = {
    /* 版本标记 (固定写法，必须第一项) */
    {CDLL_VERSION_NAME, (CAPL_FARCALL)CDLL_VERSION, "", "",
     CAPL_DLL_CDECL, 0xabcd, CDLL_EXPORT},

    /* DoIP_CreateConnection(localIp, remoteIp, testerAddr, ecuAddr) → long */
    {"DoIP_CreateConnection", (CAPL_FARCALL)DoIP_CreateConnection,
     "DoIP", "Init DoIP connection parameters (no connect yet)",
     'L', 4, "CCDD", "\001\001\000\000",
     {"localIp", "remoteIp", "testerAddr", "ecuAddr"}},

    /* DoIP_SetTimeout(controlMs, diagMs) → long */
    {"DoIP_SetTimeout", (CAPL_FARCALL)DoIP_SetTimeout,
     "DoIP", "Set control and diagnostic timeout in ms",
     'L', 2, "DD", "\000\000",
     {"controlMs", "diagMs"}},

    /* DoIP_Connect() → long */
    {"DoIP_Connect", (CAPL_FARCALL)DoIP_Connect,
     "DoIP", "TCP connect + Routing Activation (blocking)",
     'L', 0, "", "", {0}},

    /* DoIP_Send(targetAddr, data[], len) → long */
    {"DoIP_Send", (CAPL_FARCALL)DoIP_Send,
     "DoIP", "Send UDS diagnostic data via DoIP",
     'L', 3, "DBD", "\000\001\000",
     {"targetAddr", "data", "len"}},

    /* DoIP_Recv(data[], bufSize, timeoutMs) → long (UDS len or 0=timeout or <0=error) */
    {"DoIP_Recv", (CAPL_FARCALL)DoIP_Recv,
     "DoIP", "Receive UDS response (blocking). Returns UDS length, 0=timeout, <0=error",
     'L', 3, "BDD", "\001\000\000",
     {"data", "bufSize", "timeoutMs"}},

    /* DoIP_StartKeepAlive(intervalMs) → long */
    {"DoIP_StartKeepAlive", (CAPL_FARCALL)DoIP_StartKeepAlive,
     "DoIP", "Start TesterPresent (3E 80) background thread",
     'L', 1, "D", "\000",
     {"intervalMs"}},

    /* DoIP_StopKeepAlive() → long */
    {"DoIP_StopKeepAlive", (CAPL_FARCALL)DoIP_StopKeepAlive,
     "DoIP", "Stop TesterPresent background thread",
     'L', 0, "", "", {0}},

    /* DoIP_Disconnect() → long */
    {"DoIP_Disconnect", (CAPL_FARCALL)DoIP_Disconnect,
     "DoIP", "Close TCP connection and cleanup",
     'L', 0, "", "", {0}},

    /* DoIP_GetStatus() → long (0=disconnected, 1=connected, -1=error) */
    {"DoIP_GetStatus", (CAPL_FARCALL)DoIP_GetStatus,
     "DoIP", "Get connection status: 0=disconnected, 1=connected, -1=error",
     'L', 0, "", "", {0}},

    /* DoIP_GetLastError() → long */
    {"DoIP_GetLastError", (CAPL_FARCALL)DoIP_GetLastError,
     "DoIP", "Get last error code",
     'L', 0, "", "", {0}},

    /* DoIP_VehicleIdentification(broadcastIp, vinOut[], logicalAddr, timeoutMs) → long */
    {"DoIP_VehicleIdentification", (CAPL_FARCALL)DoIP_VehicleIdentification,
     "DoIP", "UDP Vehicle Identification discovery",
     'L', 4, "CBDD", "\001\001\001\000",
     {"broadcastIp", "vinOut", "logicalAddr", "timeoutMs"}},

    /* 终止符 */
    {0, 0}
};

CAPLEXPORT CAPL_DLL_INFO4 far * caplDllTable4 = table;
