/******************************************************************************
 * doip_tcp.cpp - TCP/UDP 连接管理 + KeepAlive 线程 (WinSock2 实现)
 ******************************************************************************/
#include "doip_tcp.h"
#include "doip_protocol.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <cstdio>

#pragma comment(lib, "ws2_32.lib")

/* ========== 全局状态 ========== */
static SOCKET           g_tcpSock       = INVALID_SOCKET;
static uint16_t         g_testerAddr    = 0;
static uint16_t         g_ecuAddr       = 0;
static char             g_localIp[64]   = {0};
static char             g_remoteIp[64]  = {0};
static uint32_t         g_controlTimeoutMs = 5000;
static uint32_t         g_diagTimeoutMs    = 5000;
static int              g_connected     = 0;    /* 0=未连接 1=已连接 -1=错误 */
static int              g_lastError     = 0;
static int              g_wsaInited     = 0;

/* 发送互斥锁 (KeepAlive 线程 vs 主线程) */
static CRITICAL_SECTION g_sendLock;
static int              g_sendLockInited = 0;

/* KeepAlive 线程 */
static HANDLE           g_kaThread      = NULL;
static HANDLE           g_kaStopEvent   = NULL;
static uint32_t         g_kaIntervalMs  = 2000;

/* 调试日志 — 写到 DLL 同目录的 DoIP_Core_debug.log (可选) */
#ifdef DOIP_DEBUG
static void dbg(const char* fmt, ...)
{
    FILE* f = fopen("DoIP_Core_debug.log", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}
#else
#define dbg(...) ((void)0)
#endif

/* ========== 内部辅助 ========== */

/**
 * 确保精确接收 n 字节
 * @return 实际收到字节数, 0=连接关闭, -1=错误
 */
static int recv_exact(SOCKET s, uint8_t* buf, int n, uint32_t timeoutMs)
{
    int received = 0;
    DWORD startTick = GetTickCount();

    while (received < n)
    {
        DWORD elapsed = GetTickCount() - startTick;
        if (elapsed >= timeoutMs)
            return received > 0 ? received : 0;

        DWORD remaining = timeoutMs - elapsed;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(s, &fds);

        struct timeval tv;
        tv.tv_sec  = (long)(remaining / 1000);
        tv.tv_usec = (long)((remaining % 1000) * 1000);

        int sel = select(0, &fds, NULL, NULL, &tv);
        if (sel < 0)
            return -1;
        if (sel == 0)
            return received > 0 ? received : 0;  /* 超时 */

        int r = recv(s, (char*)(buf + received), n - received, 0);
        if (r <= 0)
            return r == 0 ? 0 : -1;

        received += r;
    }
    return received;
}

/**
 * 接收一条完整 DoIP 消息 (header + payload)
 * @param headerOut  [out] 8 字节 header
 * @param payloadOut [out] payload 缓冲区
 * @param payloadBufSize payload 缓冲区大小
 * @param payloadType [out] 解析出的 payload type
 * @param payloadLen  [out] 解析出的 payload 长度
 * @return DOIP_OK, DOIP_ERR_TIMEOUT, DOIP_ERR_RECV, DOIP_ERR_HEADER, DOIP_ERR_BUFFER
 */
static int recv_doip_message(uint8_t* payloadOut, int payloadBufSize,
                              uint16_t* payloadType, uint32_t* payloadLen,
                              uint32_t timeoutMs)
{
    uint8_t header[DOIP_HEADER_LEN];
    DWORD startTick = GetTickCount();

    /* 1. 收 8 字节 header */
    int r = recv_exact(g_tcpSock, header, DOIP_HEADER_LEN, timeoutMs);
    if (r == 0) return DOIP_ERR_TIMEOUT;
    if (r < DOIP_HEADER_LEN) return DOIP_ERR_RECV;

    /* 2. 解析 header */
    if (doip_parse_header(header, DOIP_HEADER_LEN, payloadType, payloadLen) != 0)
        return DOIP_ERR_HEADER;

    /* 3. 收 payload */
    if ((int)*payloadLen > payloadBufSize)
        return DOIP_ERR_BUFFER;

    if (*payloadLen > 0)
    {
        DWORD elapsed = GetTickCount() - startTick;
        uint32_t remaining = (elapsed < timeoutMs) ? (timeoutMs - elapsed) : 0;

        r = recv_exact(g_tcpSock, payloadOut, (int)*payloadLen, remaining);
        if (r < (int)*payloadLen)
            return (r == 0) ? DOIP_ERR_TIMEOUT : DOIP_ERR_RECV;
    }

    return DOIP_OK;
}

/* ========== 连接管理 ========== */

int doip_tcp_init(const char* localIp, const char* remoteIp,
                  uint16_t testerAddr, uint16_t ecuAddr)
{
    if (!localIp || !remoteIp)
        return DOIP_ERR_PARAM;

    strncpy(g_localIp, localIp, sizeof(g_localIp) - 1);
    g_localIp[sizeof(g_localIp) - 1] = 0;
    strncpy(g_remoteIp, remoteIp, sizeof(g_remoteIp) - 1);
    g_remoteIp[sizeof(g_remoteIp) - 1] = 0;

    g_testerAddr = testerAddr;
    g_ecuAddr    = ecuAddr;
    g_connected  = 0;
    g_lastError  = 0;

    return DOIP_OK;
}

void doip_tcp_set_timeout(uint32_t controlMs, uint32_t diagMs)
{
    g_controlTimeoutMs = controlMs;
    g_diagTimeoutMs    = diagMs;
}

int doip_tcp_connect(void)
{
    WSADATA wsaData;
    struct sockaddr_in addr;
    int ret;

    /* WSAStartup (幂等) */
    if (!g_wsaInited)
    {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            g_lastError = DOIP_ERR_WSA;
            return DOIP_ERR_WSA;
        }
        g_wsaInited = 1;
    }

    /* 初始化 CRITICAL_SECTION */
    if (!g_sendLockInited)
    {
        InitializeCriticalSection(&g_sendLock);
        g_sendLockInited = 1;
    }

    /* 如果已连接，先断开 */
    if (g_tcpSock != INVALID_SOCKET)
        doip_tcp_disconnect();

    /* 创建 TCP socket */
    g_tcpSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_tcpSock == INVALID_SOCKET)
    {
        g_lastError = DOIP_ERR_SOCKET;
        g_connected = -1;
        return DOIP_ERR_SOCKET;
    }

    /* 可选: 绑定本地 IP */
    if (g_localIp[0] != '\0')
    {
        struct sockaddr_in localAddr;
        memset(&localAddr, 0, sizeof(localAddr));
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = inet_addr(g_localIp);
        localAddr.sin_port = 0;  /* 自动分配端口 */
        bind(g_tcpSock, (struct sockaddr*)&localAddr, sizeof(localAddr));
    }

    /* 连接到 ECU */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(DOIP_PORT);
    addr.sin_addr.s_addr = inet_addr(g_remoteIp);

    dbg("Connecting to %s:%d ...", g_remoteIp, DOIP_PORT);

    if (connect(g_tcpSock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        dbg("connect() failed: %d", WSAGetLastError());
        closesocket(g_tcpSock);
        g_tcpSock = INVALID_SOCKET;
        g_lastError = DOIP_ERR_CONNECT;
        g_connected = -1;
        return DOIP_ERR_CONNECT;
    }

    dbg("TCP connected, sending Routing Activation...");

    /* 发送 Routing Activation Request */
    uint8_t raBuf[32];
    int raLen = doip_build_routing_activation_req(raBuf, sizeof(raBuf),
                                                   g_testerAddr,
                                                   DOIP_ACT_TYPE_DEFAULT);
    if (raLen < 0)
    {
        closesocket(g_tcpSock);
        g_tcpSock = INVALID_SOCKET;
        g_lastError = DOIP_ERR_BUFFER;
        g_connected = -1;
        return DOIP_ERR_BUFFER;
    }

    if (send(g_tcpSock, (char*)raBuf, raLen, 0) != raLen)
    {
        closesocket(g_tcpSock);
        g_tcpSock = INVALID_SOCKET;
        g_lastError = DOIP_ERR_SEND;
        g_connected = -1;
        return DOIP_ERR_SEND;
    }

    /* 接收 Routing Activation Response */
    uint8_t respPayload[64];
    uint16_t respType;
    uint32_t respPayloadLen;

    ret = recv_doip_message(respPayload, sizeof(respPayload),
                             &respType, &respPayloadLen,
                             g_controlTimeoutMs);
    if (ret != DOIP_OK)
    {
        dbg("Routing Activation recv failed: %d", ret);
        closesocket(g_tcpSock);
        g_tcpSock = INVALID_SOCKET;
        g_lastError = ret;
        g_connected = -1;
        return ret;
    }

    if (respType != DOIP_PT_ROUTING_ACT_RESP)
    {
        dbg("Unexpected response type: 0x%04X", respType);
        closesocket(g_tcpSock);
        g_tcpSock = INVALID_SOCKET;
        g_lastError = DOIP_ERR_ROUTING;
        g_connected = -1;
        return DOIP_ERR_ROUTING;
    }

    uint16_t testerAddr, entityAddr;
    uint8_t responseCode;
    if (doip_parse_routing_activation_resp(respPayload, (int)respPayloadLen,
                                            &testerAddr, &entityAddr,
                                            &responseCode) != 0)
    {
        closesocket(g_tcpSock);
        g_tcpSock = INVALID_SOCKET;
        g_lastError = DOIP_ERR_ROUTING;
        g_connected = -1;
        return DOIP_ERR_ROUTING;
    }

    dbg("Routing Activation response: code=0x%02X entity=0x%04X", responseCode, entityAddr);

    if (responseCode != DOIP_RA_SUCCESS && responseCode != DOIP_RA_CONFIRM_REQUIRED)
    {
        closesocket(g_tcpSock);
        g_tcpSock = INVALID_SOCKET;
        g_lastError = DOIP_ERR_ROUTING;
        g_connected = -1;
        return DOIP_ERR_ROUTING;
    }

    g_connected = 1;
    g_lastError = DOIP_OK;

    dbg("DoIP connection established successfully");
    return DOIP_OK;
}

int doip_tcp_send(const uint8_t* data, int len)
{
    if (g_connected != 1)
        return DOIP_ERR_NOT_CONNECTED;

    EnterCriticalSection(&g_sendLock);
    int sent = send(g_tcpSock, (const char*)data, len, 0);
    LeaveCriticalSection(&g_sendLock);

    if (sent != len)
    {
        g_lastError = DOIP_ERR_SEND;
        return DOIP_ERR_SEND;
    }
    return DOIP_OK;
}

int doip_tcp_recv_message(uint8_t* udsBuf, int bufSize, uint32_t timeoutMs)
{
    if (g_connected != 1)
        return DOIP_ERR_NOT_CONNECTED;

    DWORD startTick = GetTickCount();

    for (;;)
    {
        /* 计算剩余超时 */
        DWORD elapsed = GetTickCount() - startTick;
        if (elapsed >= timeoutMs)
            return 0;  /* 超时 → 返回 0 */
        uint32_t remaining = timeoutMs - elapsed;

        uint8_t payload[DOIP_HEADER_LEN + DOIP_MAX_UDS_LEN + 16];
        uint16_t payloadType;
        uint32_t payloadLen;

        int ret = recv_doip_message(payload, sizeof(payload),
                                     &payloadType, &payloadLen, remaining);
        if (ret == DOIP_ERR_TIMEOUT)
            return 0;  /* 超时 → 返回 0 */
        if (ret != DOIP_OK)
        {
            g_lastError = ret;
            g_connected = -1;
            return ret;
        }

        switch (payloadType)
        {
        case DOIP_PT_DIAG_MESSAGE:
        {
            /* 解析 Diagnostic Message → 提取 UDS payload */
            uint16_t srcAddr, tgtAddr;
            const uint8_t* udsData;
            int udsLen;
            if (doip_parse_diagnostic_message(payload, (int)payloadLen,
                                               &srcAddr, &tgtAddr,
                                               &udsData, &udsLen) != 0)
            {
                g_lastError = DOIP_ERR_HEADER;
                return DOIP_ERR_HEADER;
            }
            if (udsLen > bufSize)
            {
                g_lastError = DOIP_ERR_BUFFER;
                return DOIP_ERR_BUFFER;
            }
            memcpy(udsBuf, udsData, udsLen);
            return udsLen;  /* 成功: 返回 UDS 长度 */
        }

        case DOIP_PT_DIAG_POSITIVE_ACK:
            /* Positive ACK: ECU 确认收到请求，继续等待真正的响应 */
            dbg("Received Diagnostic Positive ACK, waiting for response...");
            continue;

        case DOIP_PT_DIAG_NEGATIVE_ACK:
        {
            uint16_t srcAddr, tgtAddr;
            uint8_t ackCode;
            doip_parse_diagnostic_ack(payload, (int)payloadLen,
                                       &srcAddr, &tgtAddr, &ackCode);
            dbg("Received Diagnostic Negative ACK: code=0x%02X", ackCode);
            g_lastError = DOIP_ERR_NACK;
            return DOIP_ERR_NACK;
        }

        case DOIP_PT_ALIVE_CHECK_REQ:
        {
            /* 自动回复 Alive Check Response */
            uint8_t acResp[16];
            int acLen = doip_build_alive_check_resp(acResp, sizeof(acResp), g_testerAddr);
            if (acLen > 0)
                doip_tcp_send(acResp, acLen);
            dbg("Responded to Alive Check Request");
            continue;  /* 继续等待 */
        }

        default:
            /* 未知类型: 忽略并继续 */
            dbg("Received unknown payload type: 0x%04X, ignoring", payloadType);
            continue;
        }
    }
}

void doip_tcp_disconnect(void)
{
    /* 先停止 KeepAlive */
    doip_ka_stop();

    if (g_tcpSock != INVALID_SOCKET)
    {
        shutdown(g_tcpSock, SD_BOTH);
        closesocket(g_tcpSock);
        g_tcpSock = INVALID_SOCKET;
    }

    g_connected = 0;

    dbg("Disconnected");
}

int doip_tcp_get_status(void)
{
    return g_connected;
}

int doip_tcp_get_last_error(void)
{
    return g_lastError;
}

uint16_t doip_tcp_get_tester_addr(void)
{
    return g_testerAddr;
}

uint16_t doip_tcp_get_ecu_addr(void)
{
    return g_ecuAddr;
}

/* ========== KeepAlive 线程 ========== */

static DWORD WINAPI ka_thread_proc(LPVOID param)
{
    (void)param;

    while (WaitForSingleObject(g_kaStopEvent, g_kaIntervalMs) == WAIT_TIMEOUT)
    {
        if (g_connected != 1)
            continue;

        /* 构建 TesterPresent (3E 80) DoIP 报文 */
        uint8_t msg[64];
        uint8_t tpBuf[2] = {0x3E, 0x80};
        int msgLen = doip_build_diagnostic_message(msg, sizeof(msg),
                                                    g_testerAddr, g_ecuAddr,
                                                    tpBuf, 2);
        if (msgLen > 0)
        {
            EnterCriticalSection(&g_sendLock);
            send(g_tcpSock, (char*)msg, msgLen, 0);
            LeaveCriticalSection(&g_sendLock);
        }
    }

    return 0;
}

int doip_ka_start(uint32_t intervalMs)
{
    if (g_connected != 1)
        return DOIP_ERR_NOT_CONNECTED;

    /* 如果已在运行，先停止 */
    doip_ka_stop();

    g_kaIntervalMs = intervalMs;

    g_kaStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_kaStopEvent)
        return DOIP_ERR_PARAM;

    g_kaThread = CreateThread(NULL, 0, ka_thread_proc, NULL, 0, NULL);
    if (!g_kaThread)
    {
        CloseHandle(g_kaStopEvent);
        g_kaStopEvent = NULL;
        return DOIP_ERR_PARAM;
    }

    dbg("KeepAlive started: interval=%dms", intervalMs);
    return DOIP_OK;
}

int doip_ka_stop(void)
{
    if (g_kaThread)
    {
        SetEvent(g_kaStopEvent);
        WaitForSingleObject(g_kaThread, 5000);
        CloseHandle(g_kaThread);
        g_kaThread = NULL;
    }
    if (g_kaStopEvent)
    {
        CloseHandle(g_kaStopEvent);
        g_kaStopEvent = NULL;
    }

    dbg("KeepAlive stopped");
    return DOIP_OK;
}

/* ========== UDP 车辆发现 ========== */

int doip_udp_vehicle_ident(const char* broadcastIp,
                            uint8_t* vinOut, uint16_t* logicalAddr,
                            uint32_t timeoutMs)
{
    WSADATA wsaData;

    if (!g_wsaInited)
    {
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            return DOIP_ERR_WSA;
        g_wsaInited = 1;
    }

    SOCKET udpSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSock == INVALID_SOCKET)
        return DOIP_ERR_SOCKET;

    /* 允许广播 */
    BOOL bcast = TRUE;
    setsockopt(udpSock, SOL_SOCKET, SO_BROADCAST, (char*)&bcast, sizeof(bcast));

    /* 构建并发送 Vehicle Identification Request */
    uint8_t reqBuf[16];
    int reqLen = doip_build_vehicle_ident_req(reqBuf, sizeof(reqBuf));
    if (reqLen < 0)
    {
        closesocket(udpSock);
        return DOIP_ERR_BUFFER;
    }

    struct sockaddr_in destAddr;
    memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_port   = htons(DOIP_PORT);
    destAddr.sin_addr.s_addr = inet_addr(broadcastIp);

    if (sendto(udpSock, (char*)reqBuf, reqLen, 0,
               (struct sockaddr*)&destAddr, sizeof(destAddr)) < 0)
    {
        closesocket(udpSock);
        return DOIP_ERR_SEND;
    }

    /* 等待响应 */
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(udpSock, &fds);

    struct timeval tv;
    tv.tv_sec  = (long)(timeoutMs / 1000);
    tv.tv_usec = (long)((timeoutMs % 1000) * 1000);

    int sel = select(0, &fds, NULL, NULL, &tv);
    if (sel <= 0)
    {
        closesocket(udpSock);
        return (sel == 0) ? DOIP_ERR_TIMEOUT : DOIP_ERR_RECV;
    }

    uint8_t recvBuf[256];
    struct sockaddr_in fromAddr;
    int fromLen = sizeof(fromAddr);
    int recvLen = recvfrom(udpSock, (char*)recvBuf, sizeof(recvBuf), 0,
                            (struct sockaddr*)&fromAddr, &fromLen);
    closesocket(udpSock);

    if (recvLen <= 0)
        return DOIP_ERR_RECV;

    /* 解析 header */
    uint16_t payloadType;
    uint32_t payloadLen;
    if (doip_parse_header(recvBuf, recvLen, &payloadType, &payloadLen) != 0)
        return DOIP_ERR_HEADER;

    if (payloadType != DOIP_PT_VEHICLE_IDENT_RESP)
        return DOIP_ERR_HEADER;

    /* 解析 Vehicle Identification Response */
    char vin[17];
    uint8_t eid[6], gid[6];
    uint8_t furtherAction;
    uint16_t addr;

    if (doip_parse_vehicle_ident_resp(recvBuf + DOIP_HEADER_LEN, (int)payloadLen,
                                       vin, &addr, eid, gid, &furtherAction) != 0)
        return DOIP_ERR_HEADER;

    memcpy(vinOut, vin, 17);
    *logicalAddr = addr;

    return DOIP_OK;
}
