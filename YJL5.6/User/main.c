/**
 * 篮球比赛记分牌 - STM32F103C8T6
 *
 * 硬件连接:
 * - OLED (PB6 SCL / PB7 SDA): 软件 I2C, SSD1306 128x64
 * - 蜂鸣器 (PB0 TIM3_CH3 PWM): 计分时鸣叫 1KHz 提示音
 * - 按键 (PA12~PA15): GPIO 上拉输入, 关闭 JTAG/SWD 复用
 * - TIM2: 1ms 中断, 用于比赛计时
 * - PC13: 输出高电平, 关闭板载七段数码管
 *
 * 按键映射:
 * - IDLE 状态: KEY1=5秒演示, KEY2=5分钟, KEY3=15分钟, KEY4=30分钟
 * - PLAYING 状态: KEY1/3=+1分, KEY2/4 短按=+2分, 长按(>=600ms)=+3分
 * - PAUSED 状态: 任意键恢复计时
 * - GAMEOVER 状态: 任意键回到选时间界面
 *
 * 状态机: IDLE -> PLAYING -> (节时间到) -> PAUSED -> PLAYING -> ... -> GAMEOVER -> IDLE
 */

#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "Key.h"
#include "Buzzer.h"
#include "Game.h"

/* ================================================================ */
/* 全局变量                                                       */
/* ================================================================ */
volatile uint8_t  g_score_a         = 0;
volatile uint8_t  g_score_b         = 0;
volatile uint8_t  g_game_quarter    = 1;
volatile uint8_t  g_game_min        = 0;   /* 倒计时: 分钟 */
volatile uint8_t  g_game_sec        = 0;   /* 倒计时: 秒 */
volatile uint8_t  g_game_state      = GAME_STATE_IDLE;
volatile uint8_t  g_timer_running   = 0;   /* 计时器使能标志 */
volatile uint8_t  g_refresh_flags   = 0;   /* OLED 刷新标志位 */
volatile uint8_t  g_quarter_minutes = 5;   /* 每节分钟数(正常模式) */
volatile uint8_t  g_quarter_seconds = 0;   /* 每节秒数(demo 模式, >0 时生效) */
volatile uint32_t g_sys_tick        = 0;   /* 1ms 全局计数器(供长按检测) */

/* ================================================================ */
/* 函数声明                                                       */
/* ================================================================ */
void GameTimer_Init(void);
void Display_Welcome(void);
void Display_TimeSelect(void);
void Display_ScoreA(void);
void Display_ScoreB(void);
void Display_Time(void);
void Display_Status(void);
static void Game_SetQuarterTime(uint8_t minutes, uint8_t seconds);
static void Game_AddScore(uint8_t team, uint8_t points);
static void Game_StartQuarter(void);
static void Game_Reset(void);

/* ================================================================ */
/* TIM2 游戏定时器初始化 (1ms 中断)                               */
/* 72MHz / 72 / 1000 = 1KHz, 即每 1ms 触发一次                   */
/* ================================================================ */
void GameTimer_Init(void)
{
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	TIM_TimeBaseStructure.TIM_Period        = 1000 - 1;
	TIM_TimeBaseStructure.TIM_Prescaler     = 72 - 1;
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel                   = TIM2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	TIM_Cmd(TIM2, ENABLE);
}

/* ================================================================ */
/* OLED 显示函数 (区域刷新, 只更新变化区域)                        */
/* ================================================================ */

/* 开机欢迎画面 */
void Display_Welcome(void)
{
	OLED_Clear();
	OLED_ShowString(1, 1, "Basketball Score");
	OLED_ShowString(2, 2, "STM32F103C8T6");
	OLED_ShowString(3, 3, "Scoreboard V1.0");
	OLED_ShowString(4, 2, "Press Any Key  ");
	Delay_ms(1500);
}

/* 时间选择界面(IDLE 状态) */
void Display_TimeSelect(void)
{
	OLED_Clear();
	OLED_ShowString(1, 1, "Select Time:    ");
	OLED_ShowString(2, 1, "K1:5s   K2:5m   ");
	OLED_ShowString(3, 1, "K3:15m  K4:30m  ");
	OLED_ShowString(4, 1, "Press K1-K4     ");
	g_refresh_flags = 0;   /* 清零残留标志, 避免 welcome 画面被覆盖 */
}

/* 甲方分数区域: 第 1 行, 格式 "A: XXX            " */
void Display_ScoreA(void)
{
	char buf[17];
	buf[0]  = 'A';
	buf[1]  = ':';
	buf[2]  = ' ';
	buf[3]  = (g_score_a / 100) % 10 + '0';
	buf[4]  = (g_score_a / 10)  % 10 + '0';
	buf[5]  = (g_score_a % 10)       + '0';
	buf[6]  = ' ';
	buf[7]  = ' ';
	buf[8]  = ' ';
	buf[9]  = ' ';
	buf[10] = ' ';
	buf[11] = ' ';
	buf[12] = ' ';
	buf[13] = ' ';
	buf[14] = ' ';
	buf[15] = ' ';
	buf[16] = '\0';
	OLED_ShowString(1, 1, buf);
}

/* 乙方分数区域: 第 2 行, 格式 "B: XXX            " */
void Display_ScoreB(void)
{
	char buf[17];
	buf[0]  = 'B';
	buf[1]  = ':';
	buf[2]  = ' ';
	buf[3]  = (g_score_b / 100) % 10 + '0';
	buf[4]  = (g_score_b / 10)  % 10 + '0';
	buf[5]  = (g_score_b % 10)       + '0';
	buf[6]  = ' ';
	buf[7]  = ' ';
	buf[8]  = ' ';
	buf[9]  = ' ';
	buf[10] = ' ';
	buf[11] = ' ';
	buf[12] = ' ';
	buf[13] = ' ';
	buf[14] = ' ';
	buf[15] = ' ';
	buf[16] = '\0';
	OLED_ShowString(2, 1, buf);
}

/* 时间+小节区域: 第 3 行, 格式 "Q1 MM:SS        " */
void Display_Time(void)
{
	char buf[17];
	buf[0]  = 'Q';
	buf[1]  = g_game_quarter + '0';
	buf[2]  = ' ';
	buf[3]  = (g_game_min / 10) % 10 + '0';
	buf[4]  = (g_game_min % 10)      + '0';
	buf[5]  = ':';
	buf[6]  = (g_game_sec / 10) % 10 + '0';
	buf[7]  = (g_game_sec % 10)      + '0';
	buf[8]  = ' ';
	buf[9]  = ' ';
	buf[10] = ' ';
	buf[11] = ' ';
	buf[12] = ' ';
	buf[13] = ' ';
	buf[14] = ' ';
	buf[15] = ' ';
	buf[16] = '\0';
	OLED_ShowString(3, 1, buf);
}

/* 状态/提示行: 第 4 行
 * 比赛中: "1/3:+1 2/4:+2 L3" -> KEY1/3 加 1, KEY2/4 短按 +2 长按 +3
 * 暂停中: "PAUSE-ANY KEY  " -> 任意键继续
 */
void Display_Status(void)
{
	if (g_timer_running)
		OLED_ShowString(4, 1, "1/3:+1 2/4:+2 L3");
	else
		OLED_ShowString(4, 1, "PAUSE-ANY KEY  ");
}

/* 比赛结束画面 - 清屏后显示结果 */
void Display_GameOver(void)
{
	OLED_Clear();
	OLED_ShowString(1, 1, "===GAME OVER===");

	/* 胜者行 */
	if (g_score_a > g_score_b)
		OLED_ShowString(2, 1, "Winner: Team A  ");
	else if (g_score_b > g_score_a)
		OLED_ShowString(2, 1, "Winner: Team B  ");
	else
		OLED_ShowString(2, 1, "Draw!           ");

	/* 比分行 */
	OLED_ShowString(3, 1, "A:");
	OLED_ShowNum(3, 3, g_score_a, 3);
	OLED_ShowString(3, 7, "B:");
	OLED_ShowNum(3, 11, g_score_b, 3);

	/* 重开提示 */
	OLED_ShowString(4, 1, "ANY KEY=RESTART ");
}

/* ================================================================ */
/* 比赛逻辑函数                                                   */
/* ================================================================ */

/* 设置每节时间(minutes>0: 分钟模式; seconds>0: 5秒演示模式) */
static void Game_SetQuarterTime(uint8_t minutes, uint8_t seconds)
{
	if (seconds > 0)
	{
		/* 5秒演示模式: g_quarter_seconds 保存秒数, g_game_sec 初始化 */
		g_quarter_seconds = seconds;
		g_quarter_minutes = 0;
		g_game_min        = 0;
		g_game_sec        = seconds;
	}
	else
	{
		/* 正常分钟模式 */
		g_quarter_minutes = minutes;
		g_quarter_seconds = 0;
		g_game_min        = minutes;
		g_game_sec        = 0;
	}
	g_score_a         = 0;
	g_score_b         = 0;
	g_game_quarter    = 1;
	g_game_state      = GAME_STATE_IDLE;
	g_timer_running   = 0;
	g_refresh_flags   = RF_ALL;
}

/* 给指定队伍加分 */
static void Game_AddScore(uint8_t team, uint8_t points)
{
	if (g_game_state != GAME_STATE_PLAYING)
		return;

	if (team == TEAM_A)
	{
		g_score_a += points;
		if (g_score_a > 199) g_score_a = 199;
		g_refresh_flags |= RF_SCORE_A;
	}
	else
	{
		g_score_b += points;
		if (g_score_b > 199) g_score_b = 199;
		g_refresh_flags |= RF_SCORE_B;
	}

	Buzzer_Beep(50);
}

/* 启动新小节(根据 demo/正常模式设置初始时间) */
static void Game_StartQuarter(void)
{
	if (g_quarter_seconds > 0)
	{
		g_game_min = 0;
		g_game_sec = g_quarter_seconds;
	}
	else
	{
		g_game_min = g_quarter_minutes;
		g_game_sec = 0;
	}
	g_game_state    = GAME_STATE_PLAYING;
	g_timer_running = 1;
	/* 立即清屏, 让时间选择界面消失后再响蜂鸣器; 主循环下一帧重画 */
	OLED_Clear();
	g_refresh_flags = RF_ALL;
}

/* 重置比赛(回到 IDLE, 显示时间选择界面) */
static void Game_Reset(void)
{
	g_score_a       = 0;
	g_score_b       = 0;
	g_game_quarter  = 1;
	if (g_quarter_seconds > 0)
	{
		g_game_min = 0;
		g_game_sec = g_quarter_seconds;
	}
	else
	{
		g_game_min = g_quarter_minutes;
		g_game_sec = 0;
	}
	g_game_state    = GAME_STATE_IDLE;
	g_timer_running = 0;
	g_refresh_flags = RF_TIMESEL;  /* 触发时间选择界面 */
}

/* ================================================================ */
/* 主函数                                                         */
/* ================================================================ */
int main(void)
{
	/* 1. PC13 拉高 -> 关闭板载七段数码管 */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_13;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOC, &GPIO_InitStructure);
	GPIO_SetBits(GPIOC, GPIO_Pin_13);

	/* 2. 外设初始化 */
	OLED_Init();
	Key_Init();
	Buzzer_Init();
	GameTimer_Init();

	/* 3. 开机欢迎 */
	Display_Welcome();

	/* 4. 首次上电显示时间选择界面 */
	Display_TimeSelect();

	/* 5. 主循环(状态机) */
	while (1)
	{
		/* ---- OLED 区域刷新(只更新变化区域) ---- */
		if (g_refresh_flags)
		{
			/* 时间选择界面(需全屏清) */
			if (g_refresh_flags & RF_TIMESEL)
			{
				Display_TimeSelect();
			}
			/* 比赛结束界面(需全屏清) */
			else if (g_refresh_flags & RF_OVER)
			{
				Display_GameOver();
			}
			/* RF_ALL: 重画全部比赛区域(屏幕已在状态切换时清过) */
			else if (g_refresh_flags & RF_ALL)
			{
				Display_ScoreA();
				Display_ScoreB();
				Display_Time();
				Display_Status();
			}
			else
			{
				/* 按区域刷新 */
				if (g_refresh_flags & RF_SCORE_A) Display_ScoreA();
				if (g_refresh_flags & RF_SCORE_B) Display_ScoreB();
				if (g_refresh_flags & RF_TIME)    Display_Time();
				if (g_refresh_flags & RF_STATUS)  Display_Status();
			}
			g_refresh_flags = 0;
		}

		/* ---- 扫描按键 ---- */
		uint8_t key = Key_Scan();

		/* ---- 状态: IDLE -> 等待选择时间 ---- */
		if (g_game_state == GAME_STATE_IDLE)
		{
			uint8_t minutes = 0;
			uint8_t seconds = 0;
			switch (key)
			{
				case KEY1_PRESS: seconds = 5;          break;  /* 5秒演示 */
				case KEY2_PRESS: minutes = TIME_5MIN;  break;
				case KEY3_PRESS: minutes = TIME_15MIN; break;
				case KEY4_PRESS: minutes = TIME_30MIN; break;
				default: break;
			}
			if (minutes != 0 || seconds != 0)
			{
				Game_SetQuarterTime(minutes, seconds);
				Game_StartQuarter();    /* 清屏 + 设置 RF_ALL */
				Buzzer_Beep(100);
			}
			continue;
		}

		/* ---- 状态: GAMEOVER -> 任意键回到时间选择 ---- */
		if (g_game_state == GAME_STATE_GAMEOVER)
		{
			if (key != KEY_NONE)
			{
				Game_Reset();
			}
			continue;
		}

		/* ---- 状态: PAUSED -> 任意键恢复计时 ---- */
		if (g_game_state == GAME_STATE_PAUSED)
		{
			if (key != KEY_NONE)
			{
				g_timer_running = 1;
				g_game_state    = GAME_STATE_PLAYING;
				g_refresh_flags |= RF_STATUS;
			}
			continue;
		}

		/* ---- 状态: PLAYING ---- */
		switch (key)
		{
			case KEY1_PRESS:  /* 甲方 +1 分 */
				Game_AddScore(TEAM_A, SCORE_1PT);
				break;

			case KEY2_PRESS:  /* 甲方 +2 分(短按) 或 +3 分(长按 >=600ms) */
			{
				uint32_t start = g_sys_tick;
				while (Key_IsPressed(2) && (g_sys_tick - start) < 600);
				if ((g_sys_tick - start) >= 600)
				{
					Game_AddScore(TEAM_A, SCORE_3PT);
					while (Key_IsPressed(2));
				}
				else
				{
					Game_AddScore(TEAM_A, SCORE_2PT);
				}
			}
			break;

			case KEY3_PRESS:  /* 乙方 +1 分 */
				Game_AddScore(TEAM_B, SCORE_1PT);
				break;

			case KEY4_PRESS:  /* 乙方 +2 分(短按) 或 +3 分(长按 >=600ms) */
			{
				uint32_t start = g_sys_tick;
				while (Key_IsPressed(4) && (g_sys_tick - start) < 600);
				if ((g_sys_tick - start) >= 600)
				{
					Game_AddScore(TEAM_B, SCORE_3PT);
					while (Key_IsPressed(4));
				}
				else
				{
					Game_AddScore(TEAM_B, SCORE_2PT);
				}
			}
			break;

			default:
				break;
		}
	}
}
