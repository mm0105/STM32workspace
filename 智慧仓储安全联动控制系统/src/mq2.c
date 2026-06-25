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

#include "mq2.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "iot_errno.h"
#include "iot_adc.h"

#define CAL_PPM 20 // 校准环境中PPM值
#define RL 1       // RL阻值

static float m_r0 = 0.0f; // 元件在干净空气中的阻值
float g_mq2_ppm = 0.0f;
static int mq2_calibrated = 0;

#define MQ2_ADC_CHANNEL 4

/***************************************************************
* 函数名称: mq2_dev_init
* 说    明: 初始化ADC
* 参    数: 无
* 返 回 值: 0为成功，反之为失败
***************************************************************/
unsigned int mq2_dev_init(void)
{
    unsigned int ret = 0;

    ret = IoTAdcInit(MQ2_ADC_CHANNEL);

    if(ret != IOT_SUCCESS)
    {
        printf("[MQ2] ADC Init fail, ret=0x%x\n", ret);
    }
    else
    {
        printf("[MQ2] ADC channel %d init ok\n", MQ2_ADC_CHANNEL);
    }

    return 0;
}

/***************************************************************
* 函数名称: adc_get_voltage
* 说    明: 获取ADC电压值
* 参    数: 无
* 返 回 值: 电压值
***************************************************************/
static float adc_get_voltage(void)
{
    unsigned int ret = IOT_SUCCESS;
    unsigned int data = 0;

    ret = IoTAdcGetVal(MQ2_ADC_CHANNEL, &data);

    if (ret != IOT_SUCCESS)
    {
        printf("[MQ2] ADC Read Fail, ret=0x%x\n", ret);
        return -1.0;
    }

    /* 返回0~3.3V电压 */
    return (float)(data * 3.3 / 1024.0);
}

/***************************************************************
 * 函数名称: mq2_ppm_calibration
 * 说    明: 传感器校准函数(必须在预热完成后调用)
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void mq2_ppm_calibration(void)
{
  float voltage = adc_get_voltage();
  if(voltage <= 0.05)
  {
    printf("[MQ2] cali fail: voltage=%.3fV (sensor not ready?)\n", voltage);
    m_r0 = 0.0f;
    mq2_calibrated = 0;
    return;
  }

  float rs = (5.0f - voltage) / voltage * RL;
  m_r0 = rs / powf(CAL_PPM / 613.9f, 1.0f / -2.074f);
  mq2_calibrated = 1;

  printf("[MQ2] cali ok: V=%.3fV, Rs=%.3f, R0=%.3f\n", voltage, rs, m_r0);
}

/***************************************************************
 * 函数名称: get_mq2_ppm
 * 说    明: 获取PPM函数
 * 参    数: 无
 * 返 回 值: ppm
 ***************************************************************/
float get_mq2_ppm(void)
{
  float voltage, rs, ppm;

  voltage = adc_get_voltage();

  /* 传感器未接/未预热完成 */
  if(voltage <= 0.05)
  {
    g_mq2_ppm = 0.0f;
    return 0.0f;
  }

  /* 首次/未校准：使用默认R0重新校准 */
  if(!mq2_calibrated || m_r0 <= 0.001f)
  {
    mq2_ppm_calibration();
    if(m_r0 <= 0.001f)
    {
      g_mq2_ppm = 0.0f;
      return 0.0f;
    }
  }

  rs = (5.0f - voltage) / voltage * RL;        // 计算rs
  ppm = 613.9f * powf(rs / m_r0, -2.074f);    // 计算ppm

  if(isnan(ppm) || isinf(ppm) || ppm < 0)
  {
    ppm = 0.0f;
  }

  g_mq2_ppm = ppm;
  return ppm;
}
