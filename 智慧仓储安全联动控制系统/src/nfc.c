/*
 * Copyright (c) 2024 iSoftStone Education Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <string.h>

#include "nfc.h"
#include "NT3H.h"

/* NDEF TLV 协议常量 */
#define NDEF_MSG_TLV_TAG       0x03   /* NDEF Message TLV type */
#define NDEF_TERMINATOR_TLV    0xFE   /* 终止 TLV */
#define NDEF_FLAG_MB           0x80   /* Message Begin */
#define NDEF_FLAG_ME           0x40   /* Message End */
#define NDEF_FLAG_SR           0x10   /* Short Record */
#define NDEF_TNF_WELL_KNOWN    0x01
#define NDEF_TEXT_TYPE         'T'    /* Well-known: Text */
#define NDEF_URI_TYPE          'U'    /* Well-known: URI */

/* NT3H 用户区一帧数据能放下的最大 NDEF 字节数
 * (USER_END_REG - USER_START_REG + 1) * NFC_PAGE_SIZE = 0x77 * 16 */
#define NFC_USER_MAX           ((USER_END_REG - USER_START_REG + 1) * NFC_PAGE_SIZE)

/* 内部:把 buf/len 完整地 page-aligned 写入 NT3H 用户区(从 page USER_START_REG 起) */
static bool nfc_write_user_bytes(const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0 || len > NFC_USER_MAX) {
        errNo = NT3HERROR_WRITE_NDEF_TEXT;
        return false;
    }

    uint8_t  page = USER_START_REG;
    uint16_t off  = 0;
    uint8_t  pageData[NFC_PAGE_SIZE];

    while (off < len) {
        uint16_t remain = len - off;
        uint16_t chunk  = (remain > NFC_PAGE_SIZE) ? NFC_PAGE_SIZE : remain;
        memset(pageData, 0, NFC_PAGE_SIZE);
        memcpy(pageData, &buf[off], chunk);
        if (!NT3HWriteUserData(page, pageData)) {
            return false;
        }
        off  += chunk;
        page++;
    }
    return true;
}

/* 内部:把单条 NDEF 记录包成完整 NDEF 消息 TLV 并写入 NT3H */
static bool nfc_write_ndef_record(const uint8_t *type, uint8_t typeLen,
                                  const uint8_t *payload, uint8_t payloadLen)
{
    if (type == NULL || typeLen == 0 || payload == NULL || payloadLen == 0) {
        errNo = NT3HERROR_WRITE_NDEF_TEXT;
        return false;
    }

    uint8_t  buf[160];
    uint16_t idx = 0;

    /* NDEF Message TLV: 0x03 [LEN] [...] */
    buf[idx++] = NDEF_MSG_TLV_TAG;
    uint16_t msgLenPos = idx++;

    /* NDEF Record header: MB | ME | SR | TNF=Well-known */
    buf[idx++] = NDEF_FLAG_MB | NDEF_FLAG_ME | NDEF_FLAG_SR | NDEF_TNF_WELL_KNOWN;
    buf[idx++] = typeLen;
    buf[idx++] = payloadLen;
    memcpy(&buf[idx], type, typeLen);
    idx += typeLen;
    memcpy(&buf[idx], payload, payloadLen);
    idx += payloadLen;

    /* 回填 NDEF Message TLV 长度 */
    buf[msgLenPos] = (uint8_t)(idx - (msgLenPos + 1));

    /* 末尾追加 NDEF 终止 TLV(0xFE),占 1 字节 */
    if (idx >= sizeof(buf)) {
        errNo = NT3HERROR_WRITE_NDEF_TEXT;
        return false;
    }
    buf[idx++] = NDEF_TERMINATOR_TLV;

    return nfc_write_user_bytes(buf, idx);
}

/***************************************************************
 * 函数名称: nfc_init
 * 说    明: 初始化 NT3H I2C 总线,并复位用户区
 * 参    数: 无
 * 返 回 值: 0 成功, 其他失败
 ***************************************************************/
unsigned int nfc_init(void)
{
    unsigned int ret = NT3HI2cInit();
    if (ret != 0) {
        printf("[nfc] I2C init failed: 0x%x\n", ret);
        return ret;
    }
    if (!NT3HEraseAllTag()) {
        printf("[nfc] erase all tag failed\n");
        return 1;
    }
    printf("[nfc] init ok\n");
    return 0;
}

/***************************************************************
 * 函数名称: nfc_deinit
 * 说    明: 反初始化 NT3H I2C
 * 参    数: 无
 * 返 回 值: 0
 ***************************************************************/
unsigned int nfc_deinit(void)
{
    return NT3HI2cDeInit();
}

/***************************************************************
 * 函数名称: nfc_store_text
 * 说    明: 向 NFC 写入 UTF-8 文本 NDEF 记录
 * 参    数: position - 标识(当前实现忽略,均视为整条消息)
 *           text     - UTF-8 字符串
 * 返 回 值: true/false
 ***************************************************************/
bool nfc_store_text(RecordPosEnu position, uint8_t *text)
{
    if (text == NULL) {
        errNo = NT3HERROR_WRITE_NDEF_TEXT;
        return false;
    }
    size_t textLen = strlen((char *)text);
    if (textLen == 0 || textLen > 120) {
        errNo = NT3HERROR_WRITE_NDEF_TEXT;
        return false;
    }

    /* NDEF Text payload: status(1) + lang "en"(2) + text */
    uint8_t payload[160];
    uint8_t payLen = 0;
    payload[payLen++] = 0x02;          /* UTF-8, lang length = 2 */
    payload[payLen++] = 'e';
    payload[payLen++] = 'n';
    memcpy(&payload[payLen], text, textLen);
    payLen += (uint8_t)textLen;

    uint8_t type = (uint8_t)NDEF_TEXT_TYPE;
    (void)position;
    return nfc_write_ndef_record(&type, 1, payload, payLen);
}

/***************************************************************
 * 函数名称: nfc_store_uri_http
 * 说    明: 向 NFC 写入 URI NDEF 记录(无前缀,直接写完整 URL)
 * 参    数: position - 标识
 *           http     - URL 字符串
 * 返 回 值: true/false
 ***************************************************************/
bool nfc_store_uri_http(RecordPosEnu position, uint8_t *http)
{
    if (http == NULL) {
        errNo = NT3HERROR_WRITE_NDEF_TEXT;
        return false;
    }
    size_t uriLen = strlen((char *)http);
    if (uriLen == 0 || uriLen > 120) {
        errNo = NT3HERROR_WRITE_NDEF_TEXT;
        return false;
    }

    /* URI identifier code: 0x00 = 无前缀,直接写完整 URL */
    uint8_t payload[160];
    payload[0] = 0x00;
    memcpy(&payload[1], http, uriLen);

    uint8_t type = (uint8_t)NDEF_URI_TYPE;
    (void)position;
    return nfc_write_ndef_record(&type, 1, payload, (uint8_t)(uriLen + 1));
}
