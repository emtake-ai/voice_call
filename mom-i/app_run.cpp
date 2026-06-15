/******************************************
*  * note: 
*  * version: 1.0 
*  * date: 2021-01-23
******************************************/

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/sysinfo.h>

#include "CLocaludp.h"
#include "msg_queue.h"
#include "rs485_2_net.h"
#include "message_manage.h"
#include "protocol.h"
#include "user_api_comn.h"
#include "protocol.h"
#include "ircut_control.h"
#include "soft_ircut.h"
#include "sys_conf.h"
#include "ringfifo.h"
#include "NK_Tfcard.h"

static THREAD_PARAM g_notfify_param;
static int gCommandMsgId;
static CRingBuff gs_cmd_fifo;

extern CProtocol  g_protocol_server;
extern CIRCutThread  g_hardIrcut_task;
extern CSoftIrcutTask  g_softIrcut_task;

extern int SOC_VENC_getFps(void);
extern "C" int SK_MSG_GetCommand(char * buffer, int size);
/////////////////////////////////////////////////// for message queue
static int ProcSysMsg(SYS_MSG_BUF* pMsg)
{
    switch(pMsg->cmd)
    {
    	case BELL_MSG_REQ_SCHEDULE:
    	{
    	}
    	break;

    	case BELL_MSG_REQ_SCHEDULE_END:
		{

		}
		break;

    	case BELL_MSG_HAVE_ALARM:
    	{
    	}
    	break;

    	case BELL_MSG_REQ_SOFTRESET:
    	{
    		printf("BELL_MSG_REQ_SOFTRESET\n");
    	}
    	break;
    }

    return 0;
}

static void * Notify_thread(void * param)
{
	/*   // for send
	static int bell_qid;
    // message queue init.
    bell_qid = Msg_Init(MSG_BELL_KEY);
    fprintf(stderr, "bell thread MSGQID:%d\n", bell_qid);

	SYS_MSG_BUF msgbuf;
    memset(&msgbuf, 0, sizeof(msgbuf));
    msgbuf.Des = MSG_TYPE_MSG1;
    msgbuf.src = 0;

    msgbuf.cmd = BELL_MSG_HAVE_VISITOR;    // message type
    msgbuf.arg = 0;
    Msg_Send( bell_qid , &msgbuf , sizeof(msgbuf) );
	*/

	THREAD_PARAM * p_thread_param = (THREAD_PARAM *)param;

	SYS_MSG_BUF msgbuf;
    int msg_size;

    p_thread_param->msg_qid = gCommandMsgId;

	fprintf(stderr, "command MSGQID:%d\n", gCommandMsgId);

	while(p_thread_param->b_loop)
    {
        msg_size = msgrcv( gCommandMsgId, &msgbuf, sizeof(msgbuf) - sizeof(long),
			MSG_TYPE_MSG1,  MSG_NOERROR);				// wait until message come

        if( msg_size < 0 )
        {
            //fprintf(stderr, "System server receive msg fail \n");
			//sleep(5);
        }
        else if(msgbuf.src == MSG_TYPE_MSG1 || msgbuf.src < 0)
        {
            fprintf(stderr,"Got Error message\n");
			exit(1);
            break;
        }
        else
        {
            ProcSysMsg(&msgbuf);
        }
    }

	return NULL;
}

int start_notify_thread(void)
{
    gCommandMsgId = Msg_Init(MSG_ONVIF_KEY);
    if(gCommandMsgId < 0)
    {
        fprintf(stderr,"can not create message queue!\n");
    }

    g_notfify_param.msg_qid = gCommandMsgId;
	//g_notfify_param.b_loop = true;
	//return pthread_create(&g_notfify_param.gs_serverPid, 0, Notify_thread, &g_notfify_param);
}

void stop_notify_thread(void)
{
	Msg_Kill(g_notfify_param.msg_qid);

	//g_notfify_param.b_loop = false;
	//pthread_join(g_notfify_param.gs_serverPid, 0);
}

void send_IIRFIR_2_serial(int type, unsigned int value)
{
    if(type == 0)
    {
        g_protocol_server.set_IIRFIR_param(ONVIF_DATA_TYPE_IIR, value);
    }
    else if(type == 1)
    {
        g_protocol_server.set_IIRFIR_param(ONVIF_DATA_TYPE_FIR, value);
    }
}

int deal_cmd_func(char * buf, int len)
{
    int speed_ver = 1;
    int speed_hor = 1;
    int speed_zoom = 1;
    int speed_focus = 1;
    PtzParam ptz_param;
    OnvifData onvif_client;
    NETDATA_HEADER * Pack_head = (NETDATA_HEADER *)buf;
    Pack_head = (NETDATA_HEADER *)buf;
    //printf("flag : %s, operation : 0x%04x\n\n", Pack_head->szPackFlag, Pack_head->operation);

    if(Pack_head->operation >= PTZ_ACTION_CALL_PRESET)
    {
        printf("ptz cmd:0x%x\n", Pack_head->operation);
    }

    bool ret = get_ptz_param(ptz_param);
    if(ret)
    {
        speed_hor = ptz_param.horSpeed;
        speed_ver = ptz_param.verSpeed;
        speed_zoom = ptz_param.verSpeed;
        speed_focus = ptz_param.focusSpeed;
    }

    switch(Pack_head->operation)  
    {
    case PTZ_UPDATE_SPEED:
    {
        PtzParam ptz_param;
        memcpy(&ptz_param, buf + sizeof(NETDATA_HEADER), sizeof(ptz_param));        
        set_ptz_param(ptz_param);
        return 0;
    }
    break;

    case OPER_GUI_SetSystem_Chroma:
    {
        PtzParam ptz_param;
        ImageParam image_param;
        memcpy(&image_param, buf + sizeof(NETDATA_HEADER), sizeof(image_param));

        printf("bright:%d, contrast:%d, saturation:%d, hue:%d, mirror:%d, flip:%d\n", image_param.brightness, image_param.contrast, image_param.saturation, image_param.hue, 
                    image_param.mirror, image_param.flip);
        
        SK_Sensor_SetImageParam(image_param.brightness, image_param.contrast, image_param.saturation, image_param.hue);

        SK_Sensor_SetMirrorFlip(image_param.mirror, image_param.flip);

        printf("image_param.ir_mode_str:%s\n", image_param.ir_mode_str);
        // IR-CUT
    #if USE_HARDWARE_IR_CUT
        g_hardIrcut_task.set_work_mode(image_param.ir_mode_str, image_param.ir_level);
    #elif USE_SOFTWARE_IR_CUT
        g_softIrcut_task.set_work_mode(image_param.ir_mode_str, image_param.ir_level);
    #endif

        // IR-LED
        get_ptz_param(ptz_param);
        g_protocol_server.set_MCU_param(ptz_param, image_param.ir_mode_str);
    }
    break;

    case PTZ_ACTION_SET_PRESET:
    {
        int preset;
        memcpy(&preset, buf + sizeof(NETDATA_HEADER), sizeof(preset));
		onvif_client.param_1 = preset;
		onvif_client.param_2 = preset;  
        onvif_client.cmd = ONVIF_PRESET_ADD;   
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));          
    }    
	break;

	case PTZ_ACTION_CALL_PRESET:
    {
        int preset;
        memcpy(&preset, buf + sizeof(NETDATA_HEADER), sizeof(preset));
		onvif_client.param_1 = preset;
		onvif_client.param_2 = preset;  
        onvif_client.cmd = ONVIF_PRESET_CALL;    
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));         
    }      
    break;

	case PTZ_ACTION_REMOVE_PRESET:
    {
        int preset;
        memcpy(&preset, buf + sizeof(NETDATA_HEADER), sizeof(preset));
		onvif_client.param_1 = preset;
		onvif_client.param_2 = preset;  
        onvif_client.cmd = ONVIF_PRESET_REMOVE;     
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));        
    }      
    break;

	case PTZ_ACTION_GOTO_HOME:
    {
        onvif_client.param_1 = 0;
        onvif_client.param_2 = 0;  
        onvif_client.cmd = ONVIF_PRESET_HOME;
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client)); 
    }
    break;

	case PTZ_ACTION_HOR_MOVE:
    {
		onvif_client.param_1 = speed_hor;
		onvif_client.param_2 = speed_hor;  
        onvif_client.cmd = ONVIF_PTZ_HOR_MOVE;   
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));                
    }
    break;

	case PTZ_ACTION_VER_MOVE:
    {
		onvif_client.param_1 = speed_ver;
		onvif_client.param_2 = speed_ver;  
        onvif_client.cmd = ONVIF_PTZ_VER_MOVE;     
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));     
    }    
    break;

	case PTZ_ACTION_FOCUS_ADD:
    {
        onvif_client.param_1 = speed_focus;
        onvif_client.param_2 = 0;        
        onvif_client.cmd = ONVIF_FOCUS_FAR;

        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client)); 

        usleep(100 * 1000);

        onvif_client.cmd = ONVIF_STOP;
        onvif_client.param_1 = 0;
        onvif_client.param_2 = 0;
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));    
    }
    break;

	case PTZ_ACTION_SINGLE_FOCUS_ADD:
    {
        onvif_client.param_1 = speed_focus;
        onvif_client.param_2 = 0;        
        onvif_client.cmd = ONVIF_FOCUS_FAR;

        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));   
    }
    break;

	case PTZ_ACTION_FOCUS_SUB:
    {
        onvif_client.param_1 = speed_focus;
        onvif_client.param_2 = 0;  
        onvif_client.cmd = ONVIF_FOCUS_NEAR;

        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client)); 

        usleep(100 * 1000);

        onvif_client.cmd = ONVIF_STOP;
        onvif_client.param_1 = 0;
        onvif_client.param_2 = 0;
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));      
    }
    break;

	case PTZ_ACTION_SINGLE_FOCUS_SUB:
    {
        onvif_client.param_1 = speed_focus;
        onvif_client.param_2 = 0;  
        onvif_client.cmd = ONVIF_FOCUS_NEAR;

        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));      
    }
    break;

	case PTZ_ACTION_ZOOM_IN:
    {
		onvif_client.param_1 = speed_zoom;
		onvif_client.param_2 = 0;   
        onvif_client.cmd = ONVIF_ZOOM_IN;    

        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client)); 

        // usleep(100 * 1000);

        // onvif_client.cmd = ONVIF_STOP;
        // onvif_client.param_1 = 0;
        // onvif_client.param_2 = 0;
        // gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));       
    }
    break;

	case PTZ_ACTION_ZOOM_OUT:
    {
		onvif_client.param_1 = speed_zoom;
		onvif_client.param_2 = 0;   
        onvif_client.cmd = ONVIF_ZOOM_OUT;  

        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client)); 

        // usleep(100 * 1000);

        // onvif_client.cmd = ONVIF_STOP;
        // onvif_client.param_1 = 0;
        // onvif_client.param_2 = 0;
        // gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));       
    }    
    break;
    
	case PTZ_DIRECTION_LEFT:
    {
		onvif_client.param_1 = speed_hor;
		onvif_client.param_2 = speed_hor;  
        onvif_client.cmd = ONVIF_PTZ_LEFT;   
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));    
    }
    break;

	case PTZ_DIRECTION_RIGHT:
    {
		onvif_client.param_1 = speed_hor;
		onvif_client.param_2 = speed_hor;  
        onvif_client.cmd = ONVIF_PTZ_RIGHT;   
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));    
    }    
    break;

	case PTZ_DIRECTION_UP:
    {
		onvif_client.param_1 = speed_ver;
		onvif_client.param_2 = speed_ver;  
        onvif_client.cmd = ONVIF_PTZ_UP;   
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));    
    }    
    break;

	case PTZ_DIRECTION_DOWN:
    {
		onvif_client.param_1 = speed_ver;
		onvif_client.param_2 = speed_ver;  
        onvif_client.cmd = ONVIF_PTZ_DONW;   
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));    
    }    
    break;

	case PTZ_DIRECTION_LEFTTOP:
    {
		onvif_client.param_1 = speed_hor;
		onvif_client.param_2 = speed_ver;  
        onvif_client.cmd = ONVIF_PTZ_LEFTUP;    
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));   
    }    
    break;

	case PTZ_DIRECTION_LEFTBOT:
    {
		onvif_client.param_1 = speed_hor;
		onvif_client.param_2 = speed_ver;   
        onvif_client.cmd = ONVIF_PTZ_LEFTDOWN;    
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));   
    }    
    break;

	case PTZ_DIRECTION_RIGHTTOP:
    {
		onvif_client.param_1 = speed_hor;
		onvif_client.param_2 = speed_ver;  
        onvif_client.cmd = ONVIF_PTZ_RIGHTUP;     
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));  
    }    
    break;

	case PTZ_DIRECTION_RIGHTBOT:
    {
		onvif_client.param_1 = speed_hor;
		onvif_client.param_2 = speed_ver;  
        onvif_client.cmd = ONVIF_PTZ_RIGHTDOWN;     
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));  
    }    
    break;

    case PTZ_DIRECTION_STOP:
    {
		onvif_client.param_1 = 0;
		onvif_client.param_2 = 0;  
        onvif_client.cmd = ONVIF_STOP;  
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));   
    }
    break;

    case PTZ_ACTION_CLEAN_START:
    {
		onvif_client.param_1 = 0;
		onvif_client.param_2 = 0;  
        onvif_client.cmd = ONVIF_PTZ_CLEAN_START;  
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));   
    }
    break;

    case PTZ_ACTION_CLEAN_STOP:
    {
		onvif_client.param_1 = 0;
		onvif_client.param_2 = 0;  
        onvif_client.cmd = ONVIF_PTZ_CLEAN_STOP;  
        gui_send_msg_to_server_with_param(SK_MSG_ONVIF_REQ, (uint8_t *)&onvif_client, sizeof(onvif_client));   
    }
    break;

    default:
    return -1;
    }  

    return 0;
}

int SK_MSG_SetCommand(char * buffer, int size)
{
    return gs_cmd_fifo.ringput((unsigned char *)buffer, size, 0, 0);
}

int SK_MSG_GetCommand(char * buffer, int size)
{
    struct ringbuf media_buf;

    if(!buffer) 
    {
        printf("Null buffer.\n");
        return -1;
    }

    if(gs_cmd_fifo.ringget(&media_buf) <= 0) return -1;

    //strncpy(buffer, (char *)media_buf.buffer, size);
    if(media_buf.size < size)
    {
        memset(buffer, 0, size);
        memcpy(buffer, (char *)media_buf.buffer, media_buf.size);

        return media_buf.size;
    }

    return -1;
}

int SK_getTotalSpace(void)
{
    return SK_TF_getTotalSpace();
}

int SK_getFreeSpace(void)
{
    return SK_TF_getFreeSpace();
}

int SK_formatTfCard(void)
{
	int tmp = SK_TF_GetFormatState();   // 0 : not start, 1 : formating, 2: format finish.
	if(tmp == 0 || tmp == 2)
	{
		// start format SD
		int ret = SK_TF_FormatStorage();
		if(ret)
		{
			tmp = 1;   // start format.
		}
	}    
}

long SK_APP_getRunTime(void)
{
	struct sysinfo sys_info;
	sysinfo(&sys_info);	

	return sys_info.uptime;
}