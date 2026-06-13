#include "stm32f10x_it.h"
#include "Game.h"

/**
 * TIM2 中断服务程序 (1ms 周期)
 *
 * 功能:
 * 1. g_sys_tick++: 供 main.c 长按检测使用
 * 2. 比赛计时: g_timer_running=1 时倒计时
 *    - 秒递减到 0 时借位
 *    - 分钟/秒都到 0 时触发小节结束
 */
void TIM2_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
	{
		/* 1ms 系统节拍计数 */
		g_sys_tick++;

		/* 比赛计时器运行中 */
		if (g_timer_running)
		{
			/* 秒倒计时 */
			if (g_game_sec > 0)
			{
				g_game_sec--;
				g_refresh_flags |= RF_TIME;
			}
			/* 秒到 0, 借位 */
			else if (g_game_min > 0)
			{
				g_game_min--;
				g_game_sec = 59;
				g_refresh_flags |= RF_TIME;
			}
			/* 比赛时间到: 暂停计时 */
			else
			{
				g_timer_running = 0;
				g_game_state    = GAME_STATE_PAUSED;
				g_refresh_flags |= (RF_TIME | RF_STATUS);
			}
		}

		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	}
}
