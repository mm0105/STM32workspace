#ifndef __GAME_H
#define __GAME_H

#include "stm32f10x.h"

/* ==================== 全局变量（定义在 main.c）==================== */
extern volatile uint8_t  g_score_a;
extern volatile uint8_t  g_score_b;
extern volatile uint8_t  g_game_quarter;
extern volatile uint8_t  g_game_min;         /* 倒计时分钟 */
extern volatile uint8_t  g_game_sec;         /* 倒计时秒 */
extern volatile uint8_t  g_game_state;        /* 状态机状态 */
extern volatile uint8_t  g_timer_running;     /* 计时器使能 */
extern volatile uint8_t  g_refresh_flags;     /* OLED 刷新标志 */
extern volatile uint8_t  g_quarter_minutes;  /* 每节分钟数（正常模式） */
extern volatile uint8_t  g_quarter_seconds;  /* 每节秒数（demo 模式） */
extern volatile uint32_t g_sys_tick;         /* 1ms 全局计数器 */

/* ==================== 常量与枚举 ================================= */
#define GAME_STATE_IDLE      0   /* 等待选择时间 */
#define GAME_STATE_PLAYING   1   /* 比赛中 */
#define GAME_STATE_PAUSED    2   /* 暂停（节间休息） */
#define GAME_STATE_GAMEOVER  3   /* 比赛结束 */

#define TOTAL_QUARTERS       4   /* 共 4 节 */

#define TIME_5MIN             5
#define TIME_15MIN           15
#define TIME_30MIN           30

#define TEAM_A  0   /* 甲方 */
#define TEAM_B  1   /* 乙方 */

#define SCORE_1PT  1   /* 罚球 */
#define SCORE_2PT  2   /* 两分 */
#define SCORE_3PT  3   /* 三分 */

/* ==================== 刷新标志位 ================================= */
#define RF_SCORE_A  0x01   /* 甲方分数区 */
#define RF_SCORE_B  0x02   /* 乙方分数区 */
#define RF_TIME     0x04   /* 时间区（节数 + 倒计时） */
#define RF_STATUS   0x08   /* 状态行 */
#define RF_OVER     0x10   /* 比赛结束（全屏清） */
#define RF_TIMESEL  0x20   /* 时间选择界面（全屏清） */
#define RF_ALL      0xFF   /* 重画所有区域 */

/* ==================== 供 ISR 调用的函数 ======================== */
void Display_GameOver(void);

#endif
