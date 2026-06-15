/*
 * sys_conf.h
 *
 *  Created on: Apr 14, 2017
 *      Author: baijb
 */

#ifndef SYS_CONF_H_
#define SYS_CONF_H_

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

//////////////////////////////////////////
// 3 segment, only number, such as : 2.7.1   ver_sys=2.7.7
#define CURRENT_FIRMWARE_VER        "2.7.9"
#define VERSION_FILE_PATH           "/etc/fs-version"

#define UPGRADE_FIRMWARE_SH         "/etc/upgrade.sh"
/////////////////////////////////////////
#define USE_HARDWARE_VERIFICATION   1    // 1: use 0: do not use
#define DX_DEV_FLAG				    "/tmp/dx_flag"
#define TEST_MODE_FLAG              "/var/test_flag"

/*
*  * SIM card flag
*/
#define ACTIVE_ESIM_FLAG    	    "/mnt/config/active_sim"

/*
*  * domain socket 
*/
#define UNIX_UDPS_PATH          "/tmp/server.socket"
#define UNIX_UDPS_CP_PATH       "/tmp/server_cp.socket"
#define UNIX_UDPC_PATH          "/tmp/client.socket"
#define UNIX_UDPC_CP_PATH       "/tmp/client_cp.socket"

/*
*  *
*/
#define SENSOR_FPS				30

/*
*  *
*/
#define MAX_STREAM_NUMBER  	2
#define MASTER_INDEX 		0
#define SUB_INDEX 			1
#define JPG_CHN_INDEX       2

/*
*  *
*/
#define AUDIO_STEREO_MODE		0   	// 1 : stereo mode, 0 : mon mode

#if AUDIO_STEREO_MODE
	#define NEED_CHNNEL_NUMBER  2   // stereo mode
#else
	#define NEED_CHNNEL_NUMBER	1	// mono mode
#endif

/* 
*  * IRCUT control mode, move to CMAKEFILE
*/
//#define USE_HARDWARE_IR_CUT     1   // hardware such as AD or GPIO
//#define USE_SOFTWARE_IR_CUT     0   // only software, use ISP parameter

/*
*  * http-flv
*/
#define USE_HTTP_FLV       1    // FLV preview.

/*
*  *use RTSP stream
*/
#define USE_RTSP_STREAM	    0 //  use happytime rtsp

/*
*  * TS stream
*/
#define USE_TS_STREAM		0

/*
*  * RTMP stream
*/
#define USE_LIB_RTMP   		0   // 1 : use librtmp for RTMP
/////////////////////////////////////////////////////////////

#define WEB_CONFIG_FILE 	"/mnt/config/encoder.ini"
#define P2P_APP_FILE 	    "/customer/p2p_app.ini"

#define REAL_CONFIG_FILE    "/customer/encoder.ini"
//////////////////////////////////////////RS485 configure
#define RS485_GPIO_BASE  	9
#define RS485_GPIO_INDEX	4
/////////////////////////////////////////  record option
#define USE_RECORD_TS			0		// 1 : use record, 0 : do not use record
#define RECORD_MOUNT_DIR		"/tmp/mmc" //"/tmp/debug" //"/tmp/mmc"
#define RECORD_DIR				"/tmp/mmc/record" //"/tmp/debug/record" //"/tmp/mmc/record"

typedef struct _VIDEO_PARAM{
    int VideoEncType;	// 0:264   1:265
    int VideoFrameRate; //1~30
    int VideoBitRate;
    int InputMode;
	int VideoInputFps;   // input frame rate
    int AudioReSampleRate;	// set for audio encode
	
    int audio_codec;
	int AudioProfile;
	int	b_StreamEanable[MAX_STREAM_NUMBER];   // 1 : enable, 0 : disable
} VIDEO_PARAM;

#endif /* SYS_CONF_H_ */
