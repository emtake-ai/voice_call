/*
 * media_server_lib.h
 *
 *  Created on: Jul 31, 2017
 *      Author: baijb
 */

#ifndef MEDIA_SERVER_LIB_H_
#define MEDIA_SERVER_LIB_H_

typedef struct _COMMON_THREAD_PARAM_{
	bool b_loop;
    pthread_t gs_serverPid;
}COMMON_THREAD_PARAM;

extern int start_rtmp_url(int stream_index, char * stream_url);
extern int start_rtmp(int stream_index);
extern int stop_rtmp(int stream_index);

extern void audio_encoder_init(void);
extern void audio_encoder_deinit(void);
extern int init_all_server(void);
extern void deinit_all_server(void);
extern void check_resolution(int input_width, int input_height, int input_Fps);
extern int put_audio_2_fifo(unsigned char * buffer, int size, int64_t timestamp);


extern int start_getAddr_thread(void);
extern void stop_getAddr_thread(void);
extern int SK_getAudioDbValue(void);

#endif /* MEDIA_SERVER_LIB_H_ */
