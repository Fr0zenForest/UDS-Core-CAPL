/******************************************************************************
 * doip_protocol.cpp - DoIP 协议层实现 (ISO 13400-2)
 *
 * 纯字节操作，无平台依赖。
 * 协议逻辑参考 libdoip (https://github.com/AVL-DiTEST-DiagDev/libdoip)。
 ******************************************************************************/
#include "doip_protocol.h"
#include <cstring>

/* ========== Generic Header ========== */

int doip_build_header(uint8_t* buf, uint16_t type, uint32_t payloadLen)
{
    buf[0] = DOIP_PROTOCOL_VERSION;
    buf[1] = DOIP_INVERSE_VERSION;
    buf[2] = (uint8_t)((type >> 8) & 0xFF);
    buf[3] = (uint8_t)(type & 0xFF);
    buf[4] = (uint8_t)((payloadLen >> 24) & 0xFF);
    buf[5] = (uint8_t)((payloadLen >> 16) & 0xFF);
    buf[6] = (uint8_t)((payloadLen >> 8) & 0xFF);
    buf[7] = (uint8_t)(payloadLen & 0xFF);
    return DOIP_HEADER_LEN;
}

int doip_parse_header(const uint8_t* buf, int bufLen,
                      uint16_t* type, uint32_t* payloadLen)
{
    if (bufLen < DOIP_HEADER_LEN)
        return -1;

    /* 校验同步模式: buf[1] == ~buf[0] */
    if ((uint8_t)(buf[0] ^ 0xFF) != buf[1])
        return -2;

    *type = ((uint16_t)buf[2] << 8) | (uint16_t)buf[3];

    *payloadLen = ((uint32_t)buf[4] << 24)
               | ((uint32_t)buf[5] << 16)
               | ((uint32_t)buf[6] << 8)
               | ((uint32_t)buf[7]);

    return 0;
}

/* ========== Routing Activation ========== */

int doip_build_routing_activation_req(uint8_t* buf, int bufSize,
                                       uint16_t sourceAddr,
                                       uint8_t activationType)
{
    /* Payload: SA(2) + ActivationType(1) + Reserved(4) = 7 bytes */
    const int payloadLen = 7;
    const int totalLen = DOIP_HEADER_LEN + payloadLen;
    if (bufSize < totalLen)
        return -1;

    doip_build_header(buf, DOIP_PT_ROUTING_ACT_REQ, payloadLen);

    buf[8]  = (uint8_t)((sourceAddr >> 8) & 0xFF);
    buf[9]  = (uint8_t)(sourceAddr & 0xFF);
    buf[10] = activationType;
    buf[11] = 0x00;  /* Reserved */
    buf[12] = 0x00;
    buf[13] = 0x00;
    buf[14] = 0x00;

    return totalLen;
}

int doip_parse_routing_activation_resp(const uint8_t* payload, int len,
                                        uint16_t* testerAddr,
                                        uint16_t* entityAddr,
                                        uint8_t* responseCode)
{
    /* Payload: TesterAddr(2) + EntityAddr(2) + ResponseCode(1) + Reserved(4) = 9 bytes min */
    if (len < 5)
        return -1;

    *testerAddr = ((uint16_t)payload[0] << 8) | (uint16_t)payload[1];
    *entityAddr = ((uint16_t)payload[2] << 8) | (uint16_t)payload[3];
    *responseCode = payload[4];

    return 0;
}

/* ========== Diagnostic Message ========== */

int doip_build_diagnostic_message(uint8_t* buf, int bufSize,
                                   uint16_t sourceAddr, uint16_t targetAddr,
                                   const uint8_t* udsData, int udsLen)
{
    /* Payload: SA(2) + TA(2) + UDS data */
    const int payloadLen = 4 + udsLen;
    const int totalLen = DOIP_HEADER_LEN + payloadLen;
    if (bufSize < totalLen)
        return -1;

    doip_build_header(buf, DOIP_PT_DIAG_MESSAGE, payloadLen);

    buf[8]  = (uint8_t)((sourceAddr >> 8) & 0xFF);
    buf[9]  = (uint8_t)(sourceAddr & 0xFF);
    buf[10] = (uint8_t)((targetAddr >> 8) & 0xFF);
    buf[11] = (uint8_t)(targetAddr & 0xFF);

    memcpy(&buf[12], udsData, udsLen);

    return totalLen;
}

int doip_parse_diagnostic_message(const uint8_t* payload, int len,
                                   uint16_t* sourceAddr, uint16_t* targetAddr,
                                   const uint8_t** udsData, int* udsLen)
{
    /* Minimum: SA(2) + TA(2) + at least 1 byte UDS */
    if (len < 5)
        return -1;

    *sourceAddr = ((uint16_t)payload[0] << 8) | (uint16_t)payload[1];
    *targetAddr = ((uint16_t)payload[2] << 8) | (uint16_t)payload[3];
    *udsData = &payload[4];
    *udsLen = len - 4;

    return 0;
}

int doip_parse_diagnostic_ack(const uint8_t* payload, int len,
                               uint16_t* sourceAddr, uint16_t* targetAddr,
                               uint8_t* ackCode)
{
    /* Payload: SA(2) + TA(2) + AckCode(1) = 5 bytes min */
    if (len < 5)
        return -1;

    *sourceAddr = ((uint16_t)payload[0] << 8) | (uint16_t)payload[1];
    *targetAddr = ((uint16_t)payload[2] << 8) | (uint16_t)payload[3];
    *ackCode = payload[4];

    return 0;
}

/* ========== Alive Check ========== */

int doip_build_alive_check_resp(uint8_t* buf, int bufSize, uint16_t sourceAddr)
{
    /* Payload: SA(2) */
    const int payloadLen = 2;
    const int totalLen = DOIP_HEADER_LEN + payloadLen;
    if (bufSize < totalLen)
        return -1;

    doip_build_header(buf, DOIP_PT_ALIVE_CHECK_RESP, payloadLen);

    buf[8] = (uint8_t)((sourceAddr >> 8) & 0xFF);
    buf[9] = (uint8_t)(sourceAddr & 0xFF);

    return totalLen;
}

/* ========== Vehicle Identification ========== */

int doip_build_vehicle_ident_req(uint8_t* buf, int bufSize)
{
    /* No payload */
    const int totalLen = DOIP_HEADER_LEN;
    if (bufSize < totalLen)
        return -1;

    doip_build_header(buf, DOIP_PT_VEHICLE_IDENT_REQ, 0);

    return totalLen;
}

int doip_parse_vehicle_ident_resp(const uint8_t* payload, int len,
                                   char vinOut[17], uint16_t* logicalAddr,
                                   uint8_t eid[6], uint8_t gid[6],
                                   uint8_t* furtherAction)
{
    /* VIN(17) + LogAddr(2) + EID(6) + GID(6) + FAR(1) = 32 bytes min */
    if (len < 32)
        return -1;

    memcpy(vinOut, &payload[0], 17);

    *logicalAddr = ((uint16_t)payload[17] << 8) | (uint16_t)payload[18];

    memcpy(eid, &payload[19], 6);
    memcpy(gid, &payload[25], 6);

    *furtherAction = payload[31];

    return 0;
}
