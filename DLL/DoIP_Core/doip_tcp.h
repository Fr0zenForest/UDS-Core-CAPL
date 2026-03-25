/******************************************************************************
 * doip_tcp.h - TCP/UDP 连接管理 + KeepAlive 线程
 *
 * WinSock2 实现，提供 DoIP 传输层的连接管理、同步收发和后台 KeepAlive。
 ******************************************************************************/
#ifndef DOIP_TCP_H
#define DOIP_TCP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 错误码 ========== */
#define DOIP_OK                  0
#define DOIP_ERR_WSA            -1   /* WSAStartup 失败 */
#define DOIP_ERR_SOCKET         -2   /* socket() 失败 */
#define DOIP_ERR_CONNECT        -3   /* connect() 失败 */
#define DOIP_ERR_SEND           -4   /* send() 失败 */
#define DOIP_ERR_RECV           -5   /* recv() 失败 / 连接断开 */
#define DOIP_ERR_TIMEOUT        -6   /* select() 超时 */
#define DOIP_ERR_ROUTING        -7   /* Routing Activation 失败 */
#define DOIP_ERR_HEADER         -8   /* DoIP Header 解析失败 */
#define DOIP_ERR_NACK           -9   /* 收到 Diagnostic Negative ACK */
#define DOIP_ERR_BUFFER         -10  /* 缓冲区不足 */
#define DOIP_ERR_NOT_CONNECTED  -11  /* 未连接 */
#define DOIP_ERR_PARAM          -12  /* 参数错误 */

/* ========== 连接管理 ========== */

/**
 * 初始化连接参数（不建立连接）
 * @return DOIP_OK 或错误码
 */
int doip_tcp_init(const char* localIp, const char* remoteIp,
                  uint16_t testerAddr, uint16_t ecuAddr);

/**
 * 设置超时时间
 */
void doip_tcp_set_timeout(uint32_t controlMs, uint32_t diagMs);

/**
 * 建立 TCP 连接并完成 Routing Activation
 * @return DOIP_OK 或错误码
 */
int doip_tcp_connect(void);

/**
 * 发送原始数据（加锁，线程安全）
 * @return DOIP_OK 或错误码
 */
int doip_tcp_send(const uint8_t* data, int len);

/**
 * 接收一条 UDS 诊断响应（同步阻塞）
 *
 * 内部处理:
 *   - 0x8002 Positive ACK: 忽略，继续等待 Diagnostic Message
 *   - 0x0007 Alive Check Request: 自动回复，继续等待
 *   - 0x8001 Diagnostic Message: 剥离地址头，返回 UDS payload
 *   - 0x8003 Negative ACK: 返回错误
 *
 * @param udsBuf    [out] UDS 响应数据
 * @param bufSize   缓冲区大小
 * @param timeoutMs 超时毫秒数
 * @return          >0: UDS payload 字节数, 0: 超时, <0: 错误码
 */
int doip_tcp_recv_message(uint8_t* udsBuf, int bufSize, uint32_t timeoutMs);

/**
 * 断开连接并清理资源
 */
void doip_tcp_disconnect(void);

/**
 * 获取连接状态
 * @return 0=未连接, 1=已连接, -1=错误
 */
int doip_tcp_get_status(void);

/**
 * 获取最近一次错误码
 */
int doip_tcp_get_last_error(void);

/**
 * 获取配置的 testerAddr
 */
uint16_t doip_tcp_get_tester_addr(void);

/**
 * 获取配置的 ecuAddr
 */
uint16_t doip_tcp_get_ecu_addr(void);

/* ========== KeepAlive 线程 ========== */

/**
 * 启动 KeepAlive 后台线程 (发送 3E 80)
 * @param intervalMs 发送间隔 (ms)
 * @return DOIP_OK 或错误码
 */
int doip_ka_start(uint32_t intervalMs);

/**
 * 停止 KeepAlive 后台线程
 * @return DOIP_OK
 */
int doip_ka_stop(void);

/* ========== UDP 车辆发现 ========== */

/**
 * 发送 Vehicle Identification Request 并等待响应
 * @param broadcastIp   广播/目标 IP
 * @param vinOut        [out] 17 字节 VIN
 * @param logicalAddr   [out] 逻辑地址
 * @param timeoutMs     超时毫秒数
 * @return              DOIP_OK 或错误码
 */
int doip_udp_vehicle_ident(const char* broadcastIp,
                            uint8_t* vinOut, uint16_t* logicalAddr,
                            uint32_t timeoutMs);

#ifdef __cplusplus
}
#endif

#endif /* DOIP_TCP_H */
