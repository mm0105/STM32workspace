#include "smart_home_event.h"
#include <stdio.h>
#include "ohos_init.h"
#include "los_task.h"
#include "los_queue.h"

static unsigned int event_queue_id = (unsigned int)-1;

#define EVENT_QUEUE_LENGTH 16 //number of events
/* event_info_t 增加了 nfc_id[8] 等字段,放大缓冲以容纳完整结构 */
#define BUFFER_LEN         64 //bytes of event

void smart_home_event_init(void)
{
    /* 防止多次初始化(多个 APP_FEATURE_INIT 模块都依赖事件队列) */
    if (event_queue_id != (unsigned int)-1) {
        return;
    }
    unsigned int ret = LOS_OK;

    ret = LOS_QueueCreate("eventQ", EVENT_QUEUE_LENGTH, &event_queue_id, 0, BUFFER_LEN);
    if (ret != LOS_OK)
    {
        printf("Falied to create Message Queue ret:0x%x\n", ret);
        return;
    }
}
int smart_home_event_send(event_info_t *event)
{
     /* 非阻塞写入: 队列满则丢弃, 避免 LOS_WAIT_FOREVER 导致死锁.
      * 所有调用方 (PIR thread / ADC key thread / main loop) 都能容忍
      * 偶发的丢帧, 因为下一个 1s 周期会重新采样. */
     return LOS_QueueWriteCopy(event_queue_id, event, sizeof(event_info_t), 0);
}
int smart_home_event_wait(event_info_t *event,int timeoutMs){

    return LOS_QueueReadCopy(event_queue_id, event, sizeof(event_info_t),
        LOS_MS2Tick(timeoutMs));

}