/******************************************
*  * note: SSC336Q P2P IPC project
*  * version: 1.0 
*  * date: 2023-12-03
******************************************/
#include "CLocaludp.h"
#include "key_server.h"
#include "media_server.h"
#include "soc_server.h"
#include "authent_passwd.h"
#include "wdt.h"
#include "ircut_control.h"
#include "verification.h"
#include "protocol.h"
#include "message_manage.h"
#include "app_run.h"
#include "configfile.h"
#include "sys_conf.h"
#include "soft_ircut.h"
#include "soc_server.h"
#include "onvif_entry.h"
#include "rtsp_enctry.h"
#include "zbar_dec.h"
#include "sk_gpio.h"
#include "play_audio.h"
#include "check_wifi.h"
#include "log_rec.h"
#include "hw_pwm.h"
#include "NK_Tfcard.h"
#include "net_trans.h"

#include "file_manager.h"

#if SUPPORT_WECHAT
	#include "wechat_user.h"
#elif SUPPORT_P2P
	#include "p2p_entry.h"	
#endif

/*  /etc/init.d/rcS
if ! mount -t vfat /dev/mmcblk0p1 /tmp/mmc/ ; then
    mount -t exfat /dev/mmcblk0p1 /tmp/mmc/
fi

if [ -f "/tmp/mmc/test_dev.txt" ] ; then
    touch /var/test_flag
fi
*/

#define DEBUG_P2P_TRASN		1

#define DAY_30				(30 * 24 * 3600)
#define DAY_32				(32 * 24 * 3600)

#define BUF_SIZE 			1400
#define TOTAL_UDP_UNIX_FD 	1

CIRCutThread  g_hardIrcut_task;
CSoftIrcutTask  g_softIrcut_task;

// serial receiver.
CProtocol  g_protocol_server;
CRaderProtocol g_rader_server;

// serial sender
CSerialSend g_serial_sender;
static bool exit_flag = false, gs_test_mode = false;

extern INPUT_INFOR input_infor;

extern int SK_WECHAT_registerDevice(void);
extern int SK_getLightValue(void);
extern float SK_getIrCamTempValue(void);

extern void SK_USBSerial_start(void);
extern void SK_USBSerial_stop(void);

extern void SK_RadarSerial_start(void);
extern void SK_RadarSerial_stop(void);

extern int SK_P2P_startPushAlarm(void);
extern void SK_P2P_stopPushAlarm(void);

extern int SK_NTP_startClient(void);
extern void SK_NTP_stopClient(void);

extern bool SK_COMN_GetResetFlag(void);
extern bool sk_upgrade_getFlag(void);

void sigroutine(int dunno)
{ 
	switch (dunno) 
	{
	case SIGUSR1:
		printf("Get a signal -- SIGUSR1\n");
		exit_flag = true;
		break;
	case SIGHUP :
		printf("Get a signal -- SIGHUP\n");
		exit_flag = true;
		break;
	case SIGINT :
		printf("Get a signal -- SIGINT\n");
		exit_flag = true;
		break;
	case SIGTERM :
		exit_flag = true;
		printf("Get a signal -- SIGTERM\n");
		break;
    }
}

void func_waitpid(int signo) {
    pid_t pid;
    int stat;
    while( (pid = waitpid(-1, &stat, WNOHANG)) > 0 ) 
	{
        printf( "child %d exit\n", pid );
    }
    return;
}

int SK_SYS_isTestMode(void)
{
	return gs_test_mode;
}

int main( int argc, char **argv )
{ 
	static SensorValue_t sensor_value;
	CLocalUDP 	m_localudp_web;
	char buf[BUF_SIZE];
	unsigned int count = 0;
    int ret = 0, wechat_p2p_init = 0, link_flag = 0;
    NETDATA_HEADER * Pack_head;
	long my_pid;
	FILE * p_file;

	int maxfd = 0, udp_Fd[TOTAL_UDP_UNIX_FD], s32Ret;
    struct timeval TimeoutVal;
    fd_set read_fds;

#if DEBUG_P2P_TRASN	
	char test_data[20 * 1000];
#endif

	printf("Project name : SSC336 P2P IPC, build at %s\n", __DATE__);

	signal(SIGUSR1, sigroutine);
    signal(SIGINT, sigroutine);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, sigroutine);
	//signal(SIGCHLD, &func_waitpid);

	// SK_PWM_initChn(HEATER_CHN_INDEX);
	// SK_PWM_setDuty(HEATER_CHN_INDEX, 70);

	// check if goto test mode.
    gs_test_mode = (access(TEST_MODE_FLAG, F_OK) == F_OK) ? true : false;

	SK_INI_loadConf();
	audio_encoder_init();   // must after SK_INI_loadConf()

	// sleep manager.
	SK_SLP_startFileManager();

	udp_Fd[0] = m_localudp_web.InitServerSocket((char *)UNIX_UDPC_PATH, (char *)UNIX_UDPS_PATH);
	maxfd = udp_Fd[0];
	
	/*
	*  * save pid
	*/
	my_pid = getpid();
	p_file = fopen("/var/encoder.pid", "w");
	if(p_file)
	{
		fprintf(p_file, "%d", (int)my_pid);
		fclose(p_file);
	}	

	/*
	*  * check register to wechat server. wait event !!!
	*/
#if SUPPORT_WECHAT
	SK_WECHAT_registerDevice();
#endif

#if 1
	//ret = authen_passwd();
	//if(ret != AUTHEN_OK)
	if(access("/tmp/sk_logo", F_OK) != F_OK)	
	{
		int count = 50;
		while(count-- > 0)      // 500 seconds
		{
			printf("authen fialed. can not run !, reboot after:%d seconds\n", count * 10);
			sleep(10);
		}
 
		printf("\n............reboot now........\n\n");

		system("reboot");
		while(1);
	}

	printf("\n************** 1 - authentication passed ***************\n\n");
#endif	

#if 0
	// for onvif process
	start_message_manage_thread();

	// for send PTZ cmd
	start_notify_thread();
	// serial send
	g_serial_sender.set_comm_fd(g_protocol_server.get_serial_fd());
	g_protocol_server.set_sender(&g_serial_sender);
	g_serial_sender.start_server();

	// serial receiver
	g_protocol_server.start_server();
#else
	// serial receiver for rader.
	g_rader_server.start_server();

	// serial send
	g_serial_sender.set_comm_fd(g_rader_server.get_serial_fd());
	g_rader_server.set_sender(&g_serial_sender);

	g_serial_sender.start_server();
#endif

#if SUPPORT_P2P
	SK_P2P_startPushAlarm();
#endif

	SK_WDT_InitDev();
	SK_GPIO_init();

    // video
    ret = SOC_Video_init();
	if(ret < 0)
	{
		printf("SOC_Video_init faild, will reboot, after 10 minutes\n");

		count = 120;   
		while(1)
		{
			if(count-- < 0) system("/sbin/reboot");	

			SK_WDT_FeedDev();	
			sleep(5);
			
			printf("time left:%d\n", 5 * count);
		}	
	}

	SK_WDT_FeedDev();

    // audio
    SOC_Audio_Init();
	SK_AUDIO_StartPlaybackFileTrd();

    // must after init video !!!, for image width and height
	init_all_server();

	SK_KEY_start();

#if USE_HARDWARE_IR_CUT	
	g_hardIrcut_task.start();
#elif USE_SOFTWARE_IR_CUT
	g_softIrcut_task.start();
#else
	//#error "must define IR cut mode"
#endif

#if !SMALL_MEMORY
	SK_ONVIF_Init();
#endif

	SK_RTSP_Init();	
	SK_NTP_startClient();
	
	SK_USBSerial_start();
	SK_RadarSerial_start();

#if USE_TS_RECORD
	int width = 1920, height = 1080, fps = 20;
	SK_INI_GetMastarVideoInfo(width, height, fps);
    // for record
    NK_TFCard_setVideoParam(fps, width, height);    // same with master channel
    NK_TFCard_setAudioParam(8000, 1);

	NK_TFCARD_Init(TFCARD_MMCBLK0P1_PATH, TFCARD_MOUNT_PATH);
	NK_TFRECORD_Start(0, EN_RECORD_TYPE_TIMER);
	//SK_GetRcordFile_test();

	// if(count == 0)
	// {
	// 	return 0;
	// }	
#endif

	count = 0;
	SK_WDT_FeedDev();
	SK_CONF_startServer();	

	// rewrite app version to firware version file.
	SK_P2P_getOldFirmwareVer();

	if(gs_test_mode)
	{
		printf("------------------------------> int test mode.\n");
		SK_AUDIO_playbackFile(TEST_MODE_FILE);
		SK_SYS_loadWifiMod();
	}
	else
	{
		printf("------------------------------> normal work mode.\n");
	}

#if SUPPORT_PROTOCOL
	if(gs_test_mode)
	{		
		start_SK_tcp_server();
	}
#endif

	while(!exit_flag)
	{	
		if(count == 30)
		{
			init_logLib(RECORD_MOUNT_DIR);
		}	

	#if SUPPORT_WECHAT
		if((count == 0) && !SK_INI_getWifiInfoState())
		{			
			SK_AUDIO_playbackFile(AUDIO_ADD_DEV_G711A);			
		}
		// check if have WIFI information
		if(!wechat_p2p_init && SK_INI_getWifiInfoState())
		{
			if(SK_INI_getWechatRegisterFlag())   // after registar to wecht server
			{				
				int net_type = SK_INI_getNetType();
				ret = SK_WECHAT_Init(net_type);
				if(ret >= 0)
				{
					wechat_p2p_init = 1;
				}
			}
		}
	#elif SUPPORT_P2P
		if(!wechat_p2p_init)
		{
			unsigned char ip[20];
			int net_type = SK_INI_getNetType();

			if((count == 0) && (net_type == NET_TYPE_WIFI))
			{
				if(!SK_INI_getWifiInfoState() && (SK_INI_getRj45State() <= 0))
				{
					SK_AUDIO_playbackFile(AUDIO_ADD_DEV_G711A);					
				}
			}

			if((SK_INI_getRj45State() == 1) || (net_type == NET_TYPE_WIRED))  // wired net
			{				
				ret = SK_P2P_Init(NET_TYPE_WIRED);
				if(ret >= 0)
				{
					wechat_p2p_init = 1;
				}
			}
			else if(net_type == NET_TYPE_WIFI)  // wifi
			{
				ret = SK_P2P_Init(NET_TYPE_WIFI);
				if(ret >= 0)
				{
					wechat_p2p_init = 1;
				}				
			}
			else if(net_type == NET_TYPE_4G) // 4g
			{
				ret = SK_P2P_Init(NET_TYPE_4G);
				if(ret >= 0)
				{
					wechat_p2p_init = 1;
				}				
			}
		}

		if(count % 2 == 0)
		{
			if(wechat_p2p_init && !link_flag)
			{
				ret = SK_P2P_GetRegState();
				if(ret)
				{
					link_flag = 1;
					
					if(!SK_COMN_GetResetFlag())   // not in reset state.
					{
						//SK_AUDIO_playbackFile(AUDIO_CON_SERVER);
					}

					printf("\n######## p2p registar to server\n\n");
				}
			}			
		}		
	#endif	

    	FD_ZERO(&read_fds);
    	for (int j = 0; j < TOTAL_UDP_UNIX_FD; j++)
    	{
        	FD_SET(udp_Fd[j], &read_fds);
    	}

    	TimeoutVal.tv_sec  = 0;
    	TimeoutVal.tv_usec = 500 * 1000;
    	s32Ret = select(maxfd + 1, &read_fds, NULL, NULL, &TimeoutVal);
    	if (s32Ret == -1)
    	{
    		printf("select, get a signal, s32Ret:%d\n", s32Ret);
    	}
    	else if (s32Ret < -1)
    	{
    		printf("select failed! s32Ret:%d\n", s32Ret);
    		exit_flag = true;
    		continue;
    	}
    	else if (s32Ret == 0)
    	{
        	//printf("get timeout\n");
    	}
    	else
    	{
			int i;
    		for (i = 0; i < TOTAL_UDP_UNIX_FD; i++)
        	{
            	if (FD_ISSET(udp_Fd[i], &read_fds))
            	{
            		ret = 0;

            		if(i == 0)
            		{
            			ret = m_localudp_web.RecvNetDataUdp(buf, BUF_SIZE);
            		}

					if(ret)
					{
						ret = deal_cmd_func(buf, BUF_SIZE);
					}

            		if(ret)
    				{
    					Pack_head = (NETDATA_HEADER *)buf;
				    	//printf("flag : %s, operation : 0x%04x\n\n", Pack_head->szPackFlag, Pack_head->operation);

    					switch(Pack_head->operation)
    					{
						case OPER_GUI_STOP_WDT:
						{
							printf("GUI stop WDT. app stop now\n");
							//exit_flag = true;
							SK_WDT_CloseDev();
						}
						break;

    					case OPER_GUI_SetSystem_Master_Bmp_OSD:
    					case OPER_GUI_SetSystem_Sub_Bmp_OSD:
    					case OPER_GUI_SetSystem_Sound:
    						exit_flag = true;
    						printf("set sound param\n");
    						break;

    					case OPER_GUI_SetSystem_Master_Encoder:
    						exit_flag = true;
    						printf("set Main encoder param\n");
    						break;

    					case OPER_GUI_SetSystem_Sub_Encoder:
    						exit_flag = true;
    						printf("set Sub encoder param\n");
    						break;

    					case OPER_GUI_SetSystem_Master_Live:
    						exit_flag = true;
    						printf("set Main live stream param\n");
    						break;

    					case OPER_GUI_SetSystem_Sub_Live:
    						exit_flag = true;
    						printf("set Sub live stream param\n");
    						break;

						case OPER_GUI_SetSystem_Sub_OSD:
    					case OPER_GUI_SetSystem_Master_OSD:
    						SOC_DestoryTrdOSDShow();
    						SOC_CreatTrdOSDShow();
    						printf("set Main stream OSD\n");
    						break;
    			
						case OPER_GUI_SET_WATCHDOG_SYS_RESTART:
							{
								printf("\n\ndo not feed wdt frquencily. system will restart !!!!\n\n");								
								while(1);
							}
							break;

						case OPER_GUI_LOAD_PTZ_PARAM:
						{
							SK_INI_loadConf();  // same with int Bsp_INI_Load(void)
						}
						break;
    					}
    				}
            	}
        	}
    	}

		count++;
		
		if(1)
		{			
			sensor_value.temp = SK_getTempValue();
			sensor_value.humidity = SK_getHumidityValue();
			sensor_value.volume = SK_getAudioDbValue();
			sensor_value.light = SK_getLightValue();
			sensor_value.ircam_temp = SK_getIrCamTempValue();
			SK_P2P_SetSensorValue(sensor_value);			
		}

		// watchdog
		if((count % 10) == 1)    // 5 seconds
		{					
			int run_time = SK_APP_getRunTime();
			// extern int SK_FS_setCrop(int strat_x, int start_y, int img_w, int img_h);
			// SK_FS_setCrop(0, 0, 1920, 1080);
			
			if((run_time < DAY_32) && SK_WDT_getFeedFlag())    // < 32 days, after will restart
			{
				SK_WDT_FeedDev();
				
				time_t current_t = time(NULL);
				struct tm * pTM;
				pTM = localtime(&current_t);					
				if((run_time) > DAY_30) //  30 day
				{
					struct tm * pTM;
					pTM = localtime(&current_t);
					if(pTM->tm_hour == 11 && pTM->tm_min == 25)    // 11:25
					{
						SK_SLP_stopFileManager();
						NK_TFCARD_Exit();
						
						// wait reboot
						while(1)
						{
							sleep(1);
						}
					}
				}				
			}
			else if(sk_upgrade_getFlag())   // upgrade
			{
				SK_WDT_FeedDev();	
			}
			else   // need restart device
			{
				SK_SLP_stopFileManager();
				// stop tf record
				NK_TFCARD_Exit();	
				
				while(1)
				{
					sleep(1);
				}
			}

		#if 0 //DEBUG_P2P_TRASN
			char buf[100] = {0};
			strcpy(buf, "I am p2p user data\n");
			SK_P2P_SendUserData((unsigned char *)buf, strlen(buf), P2P_USER_DATA_STRING, 0);

			//strcpy(buf, "radar test data.\n");
			//SK_P2P_SendUserData((unsigned char *)buf, strlen(buf), P2P_USER_DATA_RADAR, 0);			

			test_data[0] = 1;
			test_data[1] = 2;
			test_data[2] = 3;
			test_data[3] = 4;
			test_data[4] = 1;
			test_data[5] = 2;
			test_data[6] = 3;
			test_data[7] = 4;

			test_data[20 * 1000 - 5] = 'S';
			test_data[20 * 1000 - 4] = 'K';
			test_data[20 * 1000 - 3] = 'D';
			test_data[20 * 1000 - 2] = 'Z';
			test_data[20 * 1000 - 1] = 0;

			SK_P2P_SendUserData((unsigned char *)test_data, 10 * 1000, P2P_USER_DATA_IRCAM, 0);
		#endif
		}

		// media information
		if((count % 20) == 0)  // 10 seconds
		{
			m_localudp_web.NotifyCmdParam(OPER_GUI_SEND_MEDIA_Infor, (char *)&input_infor, sizeof(input_infor));			
		}

		//usleep(1000 * 1000);

		//if(count % 4 == 0)
		if(0)
		{
			static int flag_format = 0;
			int res = SK_TF_GetFormatState();
			printf("--->>>tf state:%d\n",  res);
			//SK_ZBAR_decode(NULL, 320, 576);
			if(res == 0 && flag_format == 0)
			{
				flag_format = 1;	
				SK_GetRcordFile_test();
			}
			else if(res == 3 && flag_format == 1)
			{
				flag_format = 2;
				SK_GetRcordFile_test();
			}			
		}

		// if(count == 10)
		// {
		// 	extern int SK_VENC_getJpegSnap(unsigned char * buf, int in_len);
		// 	SK_VENC_getJpegSnap(NULL, 0);
		// }		
	}

	SK_WDT_CloseDev();
	SK_GPIO_deinit();
	SK_CONF_stopServer();
	SK_SLP_stopFileManager();

	printf("main app stop 0\n");

	//SK_KEY_stop();
	SK_NTP_stopClient();

	printf("main app stop 1\n");

	SK_USBSerial_stop();
	SK_RadarSerial_stop();

	printf("main app stop 2\n");

#if SUPPORT_PROTOCOL	
	if(gs_test_mode)
	{
		stop_SK_tcp_server();
	}
#endif

#if 0	
	// message server
	stop_message_manage_thread();

	// for PTZ command 
	stop_notify_thread();
	
	g_serial_sender.stop_server();
	
	// serial protocol server
	g_protocol_server.stop_server();
#endif

#if USE_HARDWARE_IR_CUT
	g_hardIrcut_task.stop_server();
#elif USE_SOFTWARE_IR_CUT
	g_softIrcut_task.stop_server();
#endif

	printf("main app stop 3\n");

	SK_RTSP_Deinit();
	deinit_all_server();

	printf("main app stop 4\n");

	SOC_Audio_exit();
	SOC_Video_deinit();
	
	printf("main app stop 5\n");

	SOC_Exit_System();

#if USE_HTTP_FLV && !SMALL_MEMORY
	extern void stopLiveStream(void);
	stopLiveStream();
#endif

#if USE_TS_RECORD
	NK_TFRECORD_Stop(0);
#endif

#if SUPPORT_WECHAT	
	stop_wechatHttp_thread();
	SK_WECHAT_Deinit();
#elif SUPPORT_P2P	
	SK_P2P_stopPushAlarm();
	SK_P2P_Deinit();
#endif

	printf("main app end.\n");

	return 0 ;
}
