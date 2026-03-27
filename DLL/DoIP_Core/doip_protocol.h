/******************************************************************************
 * doip_protocol.h - DoIP 协议层 (ISO 13400-2)
 *
 * 纯字节操作的报文构建/解析函数，无平台依赖。
 * 协议逻辑参考 libdoip (https://github.com/AVL-DiTEST-DiagDev/libdoip)。
 ******************************************************************************/
#ifndef DOIP_PROTOCOL_H
#define DOIP_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== DoIP Generic Header ========== */
#define DOIP_HEADER_LEN             8
#define DOIP_PROTOCOL_VERSION       0x02
#define DOIP_INVERSE_VERSION        0xFD

/* ========== Payload Types (ISO 13400-2 Table 17) ========== */
#define DOIP_PT_NEGATIVE_ACK            0x0000
#define DOIP_PT_VEHICLE_IDENT_REQ       0x0001
#define DOIP_PT_VEHICLE_IDENT_EID_REQ   0x0002
#define DOIP_PT_VEHICLE_IDENT_VIN_REQ   0x0003
#define DOIP_PT_VEHICLE_IDENT_RESP      0x0004
#define DOIP_PT_ROUTING_ACT_REQ         0x0005
#define DOIP_PT_ROUTING_ACT_RESP        0x0006
#define DOIP_PT_ALIVE_CHECK_REQ         0x0007
#define DOIP_PT_ALIVE_CHECK_RESP        0x0008
#define DOIP_PT_ENTITY_STATUS_REQ       0x4001
#define DOIP_PT_ENTITY_STATUS_RESP      0x4002
#define DOIP_PT_DIAG_POWER_MODE_REQ     0x4003
#define DOIP_PT_DIAG_POWER_MODE_RESP    0x4004
#define DOIP_PT_DIAG_MESSAGE            0x8001
#define DOIP_PT_DIAG_POSITIVE_ACK       0x8002
#define DOIP_PT_DIAG_NEGATIVE_ACK       0x8003

/* ========== Routing Activation Response Codes ========== */
#define DOIP_RA_UNKNOWN_SA              0x00
#define DOIP_RA_NO_FREE_SOCKET          0x01
#define DOIP_RA_SA_MISMATCH             0x02
#define DOIP_RA_SA_ALREADY_ACTIVE       0x03
#define DOIP_RA_MISSING_AUTH            0x04
#define DOIP_RA_REJECTED_CONFIRM        0x05
#define DOIP_RA_UNSUPPORTED_TYPE        0x06
#define DOIP_RA_TLS_REQUIRED            0x07
#define DOIP_RA_SUCCESS                 0x10
#define DOIP_RA_CONFIRM_REQUIRED        0x11

/* ========== Routing Activation Types ========== */
#define DOIP_ACT_TYPE_DEFAULT           0x00
#define DOIP_ACT_TYPE_WWH_OBD           0x01

/* ========== NACK Codes ========== */
#define DOIP_NACK_INCORRECT_PATTERN     0x00
#define DOIP_NACK_UNKNOWN_PAYLOAD       0x01
#define DOIP_NACK_MSG_TOO_LARGE         0x02
#define DOIP_NACK_OUT_OF_MEMORY         0x03
#define DOIP_NACK_INVALID_LENGTH        0x04

/* ========== Diagnostic Message ACK Codes ========== */
#define DOIP_DIAG_ACK_OK                0x00
#define DOIP_DIAG_NACK_INVALID_SA       0x02
#define DOIP_DIAG_NACK_UNKNOWN_TA       0x03
#define DOIP_DIAG_NACK_MSG_TOO_LARGE    0x04
#define DOIP_DIAG_NACK_OUT_OF_MEMORY    0x05
#define DOIP_DIAG_NACK_TARGET_UNREACH   0x06
#define DOIP_DIAG_NACK_UNKNOWN_NET      0x07
#define DOIP_DIAG_NACK_TP_ERROR         0x08

/* ========== Default Port ========== */
#define DOIP_PORT                       13400

/* ========== 报文最大长度 ========== */
#define DOIP_MAX_UDS_LEN                16384
#define DOIP_MAX_MSG_LEN                (DOIP_HEADER_LEN + 4 + DOIP_MAX_UDS_LEN)

/* ========== Generic Header 构建/解析 ========== */

/**
 * 构建 DoIP Generic Header (8 字节)
 * @param buf       目标缓冲区，至少 8 字节
 * @param type      Payload Type
 * @param payloadLen Payload 数据长度 (不含 header)
 * @return          固定返回 DOIP_HEADER_LEN (8)
 */
int doip_build_header(uint8_t* buf, uint16_t type, uint32_t payloadLen);

/**
 * 解析 DoIP Generic Header
 * @param buf       数据缓冲区
 * @param bufLen    缓冲区有效长度
 * @param type      [out] Payload Type
 * @param payloadLen [out] Payload 长度
 * @return          0=成功, -1=数据不足, -2=版本校验失败
 */
int doip_parse_header(const uint8_t* buf, int bufLen,
                      uint16_t* type, uint32_t* payloadLen);

/* ========== Routing Activation ========== */

/**
 * 构建 Routing Activation Request
 * @return 总消息长度 (header + payload)，失败返回 -1
 */
int doip_build_routing_activation_req(uint8_t* buf, int bufSize,
                                       uint16_t sourceAddr,
                                       uint8_t activationType);

/**
 * 解析 Routing Activation Response (仅 payload 部分，不含 header)
 * @return 0=成功, -1=长度不足
 */
int doip_parse_routing_activation_resp(const uint8_t* payload, int len,
                                        uint16_t* testerAddr,
                                        uint16_t* entityAddr,
                                        uint8_t* responseCode);

/* ========== Diagnostic Message ========== */

/**
 * 构建 Diagnostic Message (header + SA/TA + UDS payload)
 * @return 总消息长度，失败返回 -1
 */
int doip_build_diagnostic_message(uint8_t* buf, int bufSize,
                                   uint16_t sourceAddr, uint16_t targetAddr,
                                   const uint8_t* udsData, int udsLen);

/**
 * 解析 Diagnostic Message payload (不含 header)
 * @param udsData   [out] 指向 payload 内 UDS 数据起始位置的指针
 * @param udsLen    [out] UDS 数据长度
 * @return          0=成功, -1=长度不足
 */
int doip_parse_diagnostic_message(const uint8_t* payload, int len,
                                   uint16_t* sourceAddr, uint16_t* targetAddr,
                                   const uint8_t** udsData, int* udsLen);

/**
 * 解析 Diagnostic Positive/Negative ACK payload (不含 header)
 * @return 0=成功, -1=长度不足
 */
int doip_parse_diagnostic_ack(const uint8_t* payload, int len,
                               uint16_t* sourceAddr, uint16_t* targetAddr,
                               uint8_t* ackCode);

/* ========== Alive Check ========== */

/**
 * 构建 Alive Check Response
 * @return 总消息长度，失败返回 -1
 */
int doip_build_alive_check_resp(uint8_t* buf, int bufSize, uint16_t sourceAddr);

/* ========== Vehicle Identification ========== */

/**
 * 构建 Vehicle Identification Request (无 payload)
 * @return 总消息长度 (8)，失败返回 -1
 */
int doip_build_vehicle_ident_req(uint8_t* buf, int bufSize);

/**
 * 解析 Vehicle Identification Response payload (不含 header)
 * @param vinOut        [out] 17 字节 VIN
 * @param logicalAddr   [out] 逻辑地址
 * @param eid           [out] 6 字节 Entity ID
 * @param gid           [out] 6 字节 Group ID
 * @param furtherAction [out] Further Action Required
 * @return              0=成功, -1=长度不足
 */
int doip_parse_vehicle_ident_resp(const uint8_t* payload, int len,
                                   char vinOut[17], uint16_t* logicalAddr,
                                   uint8_t eid[6], uint8_t gid[6],
                                   uint8_t* furtherAction);

#ifdef __cplusplus
}
#endif

#endif /* DOIP_PROTOCOL_H */
