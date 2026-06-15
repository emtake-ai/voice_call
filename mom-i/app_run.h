#ifndef __APP_RUN_H__
#define __APP_RUN_H__

extern int SK_MSG_SetCommand(char * buffer, int size);

extern int start_notify_thread(void);
extern void stop_notify_thread(void);
extern int deal_cmd_func(char * buf, int len);
extern int SK_APP_getRunTime(void);

#endif