/*
*  * Created on: Jul 31, 2017
*  * Author: baijb
*/

#include <string>
#include <math.h>

#include "TsCaster.h"
#include "RtspCaster.h"
#include "g711.h"
#include "CAudioAac.h"
#include "CSendStream.h"

#include "EncoderConfigFile.h"
#include "logfile.h"
#include "CLocaludp.h"
#include "LibrtmpStreamInterface.h"
#include "CRtmpStream.h"
#include "media_server.h"
#include "public.h"
#include "AppContext.h"
#include "RingCacheBuffer.h"
#include "DesktopLiveStreaming.h"
#include "sys_conf.h"
#include "net_trans.h"
#include "wechat_user.h"
#include "p2p_entry.h"
#include "push_alarm.h"
#include "configfile.h"

#include "platform_comm.h"
#include "NK_Tfcard.h"
#include "sdk_enc.h"
#include "CAudioAacDec.h"

using namespace enc;
using namespace std;

// for configure file operation
EncoderConfigFile	m_configFile;

// format
static MFormat g_format[MAX_STREAM_NUMBER];
static MFormat g_ts_format[MAX_STREAM_NUMBER];

HANDLE g_rtsp_handle[MAX_STREAM_NUMBER];
tscaster_t g_ts_group_handle[MAX_STREAM_NUMBER];
tscaster_t g_ts_handle[MAX_STREAM_NUMBER];
tscaster_t g_ts_http_handle[MAX_STREAM_NUMBER];
static CAudioAacDecode gs_aac_decoder;

VIDEO_PARAM videoParam;

INPUT_INFOR input_infor;
int g_stream_encType[MAX_STREAM_NUMBER];

extern void set_aac_cfg(char * cfg, int len);
// auto resolution for encoder, output resolution > input resolution
int master_OSD_max_w, master_OSD_max_h, slave_OSD_max_w, slave_OSD_max_h;

CRtmpStream m_rtmp_main_thread;
CRtmpStream m_rtmp_sub_thread;

// for aac audio encoder
static CAudioAac	gs_aac_enc;
static unsigned char  aac_config[10];

static COMMON_THREAD_PARAM get_rtmpAddr_thread_param;
static int gs_audio_db = 30;

static CRingBuff gs_ringbuf_video_main, gs_ringbuf_video_sub, gs_ringbuf_audio;

extern "C" int SK_AUDIO_AAC_2_g711a(unsigned char * data, int size, unsigned char * data_out);

void SK_FIFO_ResetStream(int index)
{
	if(index == MASTER_INDEX)
	{
		gs_ringbuf_video_main.ringreset();
	}
	else if(index == SUB_INDEX)
	{
		gs_ringbuf_video_sub.ringreset();
	}
	else
	{
		gs_ringbuf_audio.ringreset();	
	}
}

void SK_FIFO_SendVideoStream(int index, unsigned char * buf, int size, int64_t pts, int is_key)
{
	if(index == MASTER_INDEX)
	{
		gs_ringbuf_video_main.PutDataToBufferVideo(buf, size, pts, is_key);	
	}
	else if(index == SUB_INDEX)
	{
		gs_ringbuf_video_sub.PutDataToBufferVideo(buf, size, pts, is_key);	
	}
}

void SK_RTSP_SendAudioStream(unsigned char * buf, int size, int64_t pts)
{
	gs_ringbuf_audio.PutDataToBuffer_CodecAudio(buf, size, pts);	
}

int SK_RTSP_GetVideoStream(int index, unsigned char * buf, int buf_size, int64_t * pts, int * is_key)
{
	int ret = 0;
	struct ringbuf media_buf;

	if(index == MASTER_INDEX)
	{
		ret = gs_ringbuf_video_main.ringget(&media_buf);
	}
	else if(index == SUB_INDEX)
	{
		ret = gs_ringbuf_video_sub.ringget(&media_buf);
	}
	else
	{
		printf("wrong stream index. %d\n", index);
	}

	if(ret > 0)
	{
		if(buf_size < media_buf.size) 
		{
			printf("too big frame, size: %d\n", media_buf.size);
			return -1;
		}
		else
		{
			memcpy(buf, media_buf.buffer, media_buf.size);
			*pts = media_buf.u64PTS;
			*is_key = media_buf.frame_key;			
			return media_buf.size;
		}
	}

	return 0;	
}

int SK_RTSP_GetAudioStream(unsigned char * buf, int buf_size, int64_t * pts)
{
	struct ringbuf media_buf;
	if(gs_ringbuf_audio.ringget(&media_buf) != 0)
	{
		if(buf_size < media_buf.size) 
		{
			printf("too big frame, size: %d\n", media_buf.size);
			return -1;
		}
		else
		{
			memcpy(buf, media_buf.buffer, media_buf.size);
			*pts = media_buf.u64PTS;
			return media_buf.size;
		}
	}

	return 0;
}

int SK_RTSP_GetAAcConf(unsigned char * buf, int buf_size)
{
	memcpy(buf, &gs_aac_enc.aac_config, 2);
	return 2;
}

void audio_encoder_init(void)
{
	videoParam.audio_codec = AUDIO_ENCODE_G711;
	videoParam.AudioProfile = 0;
	memset(&input_infor, 0, sizeof(input_infor));

	m_configFile.m_audio.resample = 8000;   // do not change it !!!

	if(m_configFile.m_audio.codec.compare("G.711") == 0)
		videoParam.audio_codec = AUDIO_ENCODE_G711;
	else if(m_configFile.m_audio.codec.compare("AAC") == 0)
		videoParam.audio_codec = AUDIO_ENCODE_AAC;

	if(((m_configFile.m_videos[MASTER_INDEX].codec.compare("H.264") == 0) && (m_configFile.m_protocols[MASTER_INDEX].rtmpEnabled))
			|| ((m_configFile.m_videos[SUB_INDEX].codec.compare("H.264") == 0) && (m_configFile.m_protocols[SUB_INDEX].rtmpEnabled)))
	{
		DBG("RTMP enabled, force use AAC\n");
		videoParam.audio_codec = AUDIO_ENCODE_AAC;
	}

	g711_init();

	// init audio encoder
	if(videoParam.audio_codec == AUDIO_ENCODE_G711)
	{		
		printf("G.711 encoder init.\n");
		videoParam.AudioProfile = 0;
	}
	else if(videoParam.audio_codec == AUDIO_ENCODE_AAC)
	{
		printf("aac encoder init.\n");
		//gs_aac_enc.init(m_configFile.m_audio.resample, NEED_CHNNEL_NUMBER, 16, m_configFile.m_audio.sampleRate);
		videoParam.AudioProfile = 1; // must be "1", !!!	
	}

	gs_aac_enc.init(m_configFile.m_audio.resample, NEED_CHNNEL_NUMBER, 16, m_configFile.m_audio.sampleRate);
	set_aac_cfg((char *)&gs_aac_enc.aac_config, 2);	

	gs_aac_decoder.fdk_initDecoder(1, m_configFile.m_audio.resample, NEED_CHNNEL_NUMBER);	
}

void audio_encoder_deinit(void)
{
	gs_aac_decoder.fdk_deInitDecoder();

	if(gs_aac_enc.b_init_flag)
	{
		printf("aac encoder finish.\n");
		gs_aac_enc.Finish();
	}
}

int init_all_server(void)
{
	int ret = -1, s32Ret = -1;
	int rtsp_port;
	Audio_Infor_t audio_info;

	PAYLOAD_TYPE_E_SK enPayLoad[3]= {PT_H264_SK, PT_H265_SK,PT_H264_SK};
	char * p_stream = NULL;

	rtsp_port = m_configFile.m_protocols[MASTER_INDEX].rtspPort;
	if(rtsp_port <= 0 || rtsp_port > 65535) rtsp_port = 554;

#if USE_RTSP_STREAM
	if(m_configFile.m_protocols[MASTER_INDEX].rtspEnabled || m_configFile.m_protocols[SUB_INDEX].rtspEnabled)
	{
		ret = caster_init(rtsp_port);
		printf("RTSP server init, ret : %d, port : %d\n", ret, rtsp_port);
	}
#endif

	videoParam.b_StreamEanable[MASTER_INDEX] = true;
	videoParam.b_StreamEanable[SUB_INDEX] = true;

	// media stream structure
	memset(&g_format[MASTER_INDEX], 0, sizeof(MFormat));
	memset(&g_ts_format[MASTER_INDEX], 0, sizeof(MFormat));
	memset(&g_format[SUB_INDEX], 0, sizeof(MFormat));
	memset(&g_ts_format[SUB_INDEX], 0, sizeof(MFormat));

	// set playback fps
	SK_TFPLAY_SetVideoFps(m_configFile.m_videos[MASTER_INDEX].framerate);

	if(m_configFile.m_videos[MASTER_INDEX].codec.compare("H.265") == 0)
	{
		DBG("master stream encoder type : h.265\n");
		enPayLoad[MASTER_INDEX] = PT_H265_SK;
		m_configFile.m_protocols[MASTER_INDEX].rtmpEnabled = 0;  // RTMP do not use H265 encoder !
	}
	else if(m_configFile.m_videos[MASTER_INDEX].codec.compare("H.264") == 0)
	{
		DBG("master stream encoder type : h.264\n");
		enPayLoad[MASTER_INDEX] = PT_H264_SK;
	}

	if(m_configFile.m_videos[SUB_INDEX].codec.compare("H.265") == 0)
	{
		DBG("sub stream encoder type : h.265\n");
		enPayLoad[SUB_INDEX] = PT_H265_SK;
		m_configFile.m_protocols[SUB_INDEX].rtmpEnabled = 0;   // RTMP do not use H265 encoder !
	}
	else if(m_configFile.m_videos[SUB_INDEX].codec.compare("H.264") == 0)
	{
		DBG("sub stream encoder type : h.264\n");
		enPayLoad[SUB_INDEX] = PT_H264_SK;
	}

	if(enPayLoad[MASTER_INDEX] == PT_H264_SK)
	{
		g_format[MASTER_INDEX].codec = MCODEC_H264;
		g_ts_format[MASTER_INDEX].codec = MCODEC_H264;
		SK_P2P_SetVideoEncType(P2P_H264_ENC);
	}
	else if(enPayLoad[MASTER_INDEX] == PT_H265_SK)
	{
		g_format[MASTER_INDEX].codec = MCODEC_H265;
		g_ts_format[MASTER_INDEX].codec = MCODEC_H265;
		SK_P2P_SetVideoEncType(P2P_H265_ENC);
	}

	g_format[MASTER_INDEX].width = m_configFile.m_videos[MASTER_INDEX].pic_width; //stVpssChnMode.u32Width;
	g_format[MASTER_INDEX].height = m_configFile.m_videos[MASTER_INDEX].pic_height; //stVpssChnMode.u32Height;
	g_format[MASTER_INDEX].framerate = m_configFile.m_videos[MASTER_INDEX].framerate; //25;
	g_format[MASTER_INDEX].bitrate = m_configFile.m_videos[MASTER_INDEX].bitrate * 1000;

	// ts stream
	g_ts_format[MASTER_INDEX].width = m_configFile.m_videos[MASTER_INDEX].pic_width;
	g_ts_format[MASTER_INDEX].height = m_configFile.m_videos[MASTER_INDEX].pic_height;
	g_ts_format[MASTER_INDEX].framerate = m_configFile.m_videos[MASTER_INDEX].framerate;
	g_ts_format[MASTER_INDEX].bitrate = g_format[MASTER_INDEX].bitrate * 1000;

	/*
		if(m_videos[i].profile.compare(0, 4, "main"))
			m_videos[i].use_profile = 1;
		else if(m_videos[i].profile.compare(0, 4, "high"))
			m_videos[i].use_profile = 2;
		else if(m_videos[i].profile.compare(0, 4, "base"))
			m_videos[i].use_profile = 0;
	*/
	if(m_configFile.m_videos[MASTER_INDEX].use_profile == 0)
	{
		g_format[MASTER_INDEX].profile = 66; // 66= baseline, 77=main, 100=high
		g_ts_format[MASTER_INDEX].profile = 66;
	}
	else if(m_configFile.m_videos[MASTER_INDEX].use_profile == 1)
	{
		g_format[MASTER_INDEX].profile = 77; // 66= baseline, 77=main, 100=high
		g_ts_format[MASTER_INDEX].profile = 77;
	}
	else if(m_configFile.m_videos[MASTER_INDEX].use_profile == 2)
	{
		g_format[MASTER_INDEX].profile = 100; // 66= baseline, 77=main, 100=high
		g_ts_format[MASTER_INDEX].profile = 100;
	}

	DBG("stream audio coded : %s\n", m_configFile.m_audio.codec.c_str());

	if(videoParam.audio_codec == AUDIO_ENCODE_G711)
	{
		audio_info.format = AUDIO_FORAT_G711_A;

		g_format[MASTER_INDEX].audioCodec = MCODEC_G711A;
		g_ts_format[MASTER_INDEX].audioCodec = MCODEC_G711A; //MCODEC_NONE;//MCODEC_G711A;
	}
	else if(videoParam.audio_codec == AUDIO_ENCODE_AAC)
	{
		audio_info.format = AUDIO_FORAT_AAC;

		g_format[MASTER_INDEX].audioCodec = MCODEC_AAC;
		g_ts_format[MASTER_INDEX].audioCodec = MCODEC_AAC; //MCODEC_NONE;//MCODEC_G711A;

		g_ts_format[SUB_INDEX].configSize = g_ts_format[MASTER_INDEX].configSize = 2;
		g_ts_format[SUB_INDEX].config = g_ts_format[MASTER_INDEX].config = &aac_config[0];   // important !!!!
		memcpy(&aac_config[0], &(gs_aac_enc.aac_config), g_ts_format[MASTER_INDEX].configSize);

		printf("MFormat: aac config[0]:0x%02x, aac config[1]:0x%02x\n", aac_config[0], aac_config[1]);
	}
	else if(videoParam.audio_codec == AUDIO_ENCODE_MP3)
	{
		g_format[MASTER_INDEX].audioCodec = MCODEC_MP3;
		g_ts_format[MASTER_INDEX].audioCodec = MCODEC_MP3; //MCODEC_NONE;//MCODEC_G711A;
	}

	audio_info.freq = m_configFile.m_audio.resample;
	audio_info.channel = NEED_CHNNEL_NUMBER;
	audio_info.bits = 16;
	
	SK_set_audioFormat(audio_info);

	g_format[MASTER_INDEX].channels = NEED_CHNNEL_NUMBER;
	g_format[MASTER_INDEX].sampleRate = m_configFile.m_audio.resample;
	g_format[MASTER_INDEX].audioProfile = videoParam.AudioProfile;
	g_format[MASTER_INDEX].clockRate = 1000000;
	g_format[MASTER_INDEX].audioRate = 1000000; 
	g_format[MASTER_INDEX].audioBitrate = m_configFile.m_audio.sampleRate;

	// ts stream
	g_ts_format[MASTER_INDEX].channels = NEED_CHNNEL_NUMBER;
	g_ts_format[MASTER_INDEX].sampleRate = m_configFile.m_audio.resample;
	g_ts_format[MASTER_INDEX].audioProfile = videoParam.AudioProfile;
	g_ts_format[MASTER_INDEX].clockRate = 1000000;
	g_ts_format[MASTER_INDEX].audioRate = 1000000; 
	g_ts_format[MASTER_INDEX].audioBitrate = m_configFile.m_audio.sampleRate;

	p_stream = (char *)m_configFile.m_protocols[MASTER_INDEX].rtspName.c_str();
	DBG("rtsp main stream name : %s\n", p_stream);
	DBG("rtsp audio freq:%d\n", g_format[MASTER_INDEX].sampleRate);
	if(p_stream[0] == '/') p_stream++;

#if USE_RTSP_STREAM
	if(m_configFile.m_protocols[MASTER_INDEX].rtspEnabled)
	{
		s32Ret = caster_chl_open(&g_rtsp_handle[MASTER_INDEX], p_stream, &g_format[MASTER_INDEX]);
		if(s32Ret == 0)
		{
			printf("rtsp, caster_chl_open[0],success.%d\n", (int)g_rtsp_handle[MASTER_INDEX]);		
		}
		else
		{
			printf("rtsp, caster_chl_open[0], failed. %d!\n", (int)g_rtsp_handle[MASTER_INDEX]);
		}
	}
#endif
	////////////////////////////////////////////////////////////////////////  for ts stream

	////////////////////////////////////////////////////////////////////////
    printf("fmt, width:%d, height:%d, codec:%d, profile:%d, clockRate:%d\n", g_ts_format[MASTER_INDEX].width, g_ts_format[MASTER_INDEX].height, g_ts_format[MASTER_INDEX].codec, g_ts_format[MASTER_INDEX].profile, g_ts_format[MASTER_INDEX].clockRate);

	if(m_configFile.m_protocols[MASTER_INDEX].rtmpEnabled && (enPayLoad[MASTER_INDEX] == PT_H264_SK))
	{
		char str_buf[512] = {0};

		sprintf(str_buf, "rtmp://%s:%d/%s/%s", m_configFile.m_protocols[MASTER_INDEX].rtmpAddr.c_str(), \
				m_configFile.m_protocols[MASTER_INDEX].rtmpPort, m_configFile.m_protocols[MASTER_INDEX].rtmpDir.c_str(), \
				m_configFile.m_protocols[MASTER_INDEX].rtmpNode.c_str());

		printf("master stream RTMP :%s\n", str_buf);

	#if USE_LIB_RTMP
		RTMPMetadata rtmp_data;
		rtmp_data.bHasAudio = true;
		rtmp_data.nAudioChannels = g_ts_format[MASTER_INDEX].channels;
		rtmp_data.nAudioDatarate = 48000;
		rtmp_data.nAudioFmt = g_ts_format[MASTER_INDEX].audioCodec;
		rtmp_data.nAudioSampleRate = g_ts_format[MASTER_INDEX].sampleRate;
		rtmp_data.nAudioSampleSize = 2048;
		rtmp_data.nAudioSpecCfgLen = 2;
		rtmp_data.pAudioSpecCfg = 1;

		rtmp_data.bHasVideo = true;
		rtmp_data.nFrameRate = g_ts_format[MASTER_INDEX].framerate;
		rtmp_data.nHeight = g_ts_format[MASTER_INDEX].height;
		rtmp_data.nWidth = g_ts_format[MASTER_INDEX].width;

		rtmp_data.aac_spec_size = g_ts_format[MASTER_INDEX].configSize;
		memcpy(rtmp_data.aac_spec, &(gs_aac_enc.aac_config), rtmp_data.aac_spec_size);

		m_rtmp_main_thread.setRtmpProperty(&rtmp_data);

		// start rtmp when get push address from server
		start_rtmp_url(MASTER_INDEX, str_buf);
	#endif	
	}

	if(enPayLoad[SUB_INDEX] == PT_H264_SK)
	{
		g_format[SUB_INDEX].codec = MCODEC_H264;
		g_ts_format[SUB_INDEX].codec = MCODEC_H264;
	}
	else if(enPayLoad[SUB_INDEX] == PT_H265_SK)
	{
		g_format[SUB_INDEX].codec = MCODEC_H265;
		g_ts_format[SUB_INDEX].codec = MCODEC_H265;
	}

	g_format[SUB_INDEX].width = m_configFile.m_videos[SUB_INDEX].pic_width; //stVpssChnMode.u32Width;
	g_format[SUB_INDEX].height = m_configFile.m_videos[SUB_INDEX].pic_height; //stVpssChnMode.u32Height;
	g_format[SUB_INDEX].framerate = m_configFile.m_videos[SUB_INDEX].framerate; //25;
	g_format[SUB_INDEX].bitrate = m_configFile.m_videos[SUB_INDEX].bitrate * 1000;

	g_ts_format[SUB_INDEX].width = m_configFile.m_videos[SUB_INDEX].pic_width; //stVpssChnMode.u32Width;
	g_ts_format[SUB_INDEX].height = m_configFile.m_videos[SUB_INDEX].pic_height; //stVpssChnMode.u32Height;
	g_ts_format[SUB_INDEX].framerate = m_configFile.m_videos[SUB_INDEX].framerate; //25;
	g_ts_format[SUB_INDEX].bitrate = m_configFile.m_videos[SUB_INDEX].bitrate * 1000;

	if(m_configFile.m_videos[SUB_INDEX].use_profile == 0)
	{
		g_format[SUB_INDEX].profile = 66; // 66= baseline, 77=main, 100=high
		g_ts_format[SUB_INDEX].profile = 66; // 66= baseline, 77=main, 100=high
	}
	else if(m_configFile.m_videos[SUB_INDEX].use_profile == 1)
	{
		g_format[SUB_INDEX].profile = 77; // 66= baseline, 77=main, 100=high
		g_ts_format[SUB_INDEX].profile = 77; // 66= baseline, 77=main, 100=high
	}
	else if(m_configFile.m_videos[SUB_INDEX].use_profile == 2)
	{
		g_format[SUB_INDEX].profile = 100; // 66= baseline, 77=main, 100=high
		g_ts_format[SUB_INDEX].profile = 100; // 66= baseline, 77=main, 100=high
	}

	//g_format[1].profile = 0;
	g_format[SUB_INDEX].audioCodec = g_format[MASTER_INDEX].audioCodec; //MCODEC_G711A;
	g_format[SUB_INDEX].channels = g_format[MASTER_INDEX].channels; //1;
	g_format[SUB_INDEX].sampleRate = g_format[MASTER_INDEX].sampleRate; //SampleRate[SampleRateMode];
	g_format[SUB_INDEX].audioProfile = g_format[MASTER_INDEX].audioProfile;
	g_format[SUB_INDEX].clockRate = g_format[MASTER_INDEX].clockRate;
	g_format[SUB_INDEX].audioRate = g_format[MASTER_INDEX].audioRate;  // baijb 2016-10-17
	g_format[SUB_INDEX].audioBitrate = m_configFile.m_audio.sampleRate;

	g_ts_format[SUB_INDEX].audioCodec = g_ts_format[MASTER_INDEX].audioCodec; //MCODEC_G711A;
	g_ts_format[SUB_INDEX].channels = g_ts_format[MASTER_INDEX].channels; //1;
	g_ts_format[SUB_INDEX].sampleRate = g_ts_format[MASTER_INDEX].sampleRate; //SampleRate[SampleRateMode];
	g_ts_format[SUB_INDEX].audioProfile = g_ts_format[MASTER_INDEX].audioProfile;
	g_ts_format[SUB_INDEX].clockRate = g_ts_format[MASTER_INDEX].clockRate;
	g_ts_format[SUB_INDEX].audioRate = g_ts_format[MASTER_INDEX].audioRate;
	g_ts_format[SUB_INDEX].audioBitrate = m_configFile.m_audio.sampleRate;

	p_stream = (char *)m_configFile.m_protocols[SUB_INDEX].rtspName.c_str();
	DBG("rtsp sub stream name : %s\n", p_stream);
	if(p_stream[0] == '/') p_stream++;

	if(m_configFile.m_videos[MASTER_INDEX].framerate < 50)   // if master >= 50 slave forbid !!!
	{
	#if USE_RTSP_STREAM
		if(m_configFile.m_protocols[SUB_INDEX].rtspEnabled)
		{
			s32Ret = caster_chl_open(&g_rtsp_handle[SUB_INDEX], p_stream, &g_format[SUB_INDEX]);
			if(s32Ret == 0)
			{
				printf("rtsp, caster_chl_open[1],success.%d\n", (int)g_rtsp_handle[SUB_INDEX]);
				//slave_stream.SetStreamStateRtsp(true, SUB_INDEX, g_rtsp_handle[SUB_INDEX]);
				//audio_stream.SetStreamStateRtsp(true, SUB_INDEX, g_rtsp_handle[SUB_INDEX]);			
			}
			else
			{
				printf("rtsp, caster_chl_open[1], failed. %d!\n", (int)g_rtsp_handle[SUB_INDEX]);
			}
		}
	#endif
	////////////////////////////////////////////////////////////////////////
		if(m_configFile.m_protocols[SUB_INDEX].rtmpEnabled && (enPayLoad[SUB_INDEX] == PT_H264_SK))
		{
			char str_buf[512] = {0};

			sprintf(str_buf, "rtmp://%s:%d/%s/%s", m_configFile.m_protocols[SUB_INDEX].rtmpAddr.c_str(), \
					m_configFile.m_protocols[SUB_INDEX].rtmpPort, m_configFile.m_protocols[SUB_INDEX].rtmpDir.c_str(), \
					m_configFile.m_protocols[SUB_INDEX].rtmpNode.c_str());

			printf("sub stream rtmp server address: %s\n", str_buf);

		#if USE_LIB_RTMP
			RTMPMetadata rtmp_data;
			rtmp_data.bHasAudio = true;
			rtmp_data.nAudioChannels = g_ts_format[SUB_INDEX].channels;
			rtmp_data.nAudioDatarate = 48000;
			rtmp_data.nAudioFmt = g_ts_format[SUB_INDEX].audioCodec;
			rtmp_data.nAudioSampleRate = g_ts_format[SUB_INDEX].sampleRate;
			rtmp_data.nAudioSampleSize = 2048;
			rtmp_data.nAudioSpecCfgLen = 2;
			rtmp_data.pAudioSpecCfg = 1;

			rtmp_data.bHasVideo = true;
			rtmp_data.nFrameRate = g_ts_format[SUB_INDEX].framerate;
			rtmp_data.nHeight = g_ts_format[SUB_INDEX].height;
			rtmp_data.nWidth = g_ts_format[SUB_INDEX].width;

			//memcpy(&aac_config[0], &(gs_aac_enc.aac_config), g_ts_format[MASTER_INDEX].configSize);
			rtmp_data.aac_spec_size = g_ts_format[MASTER_INDEX].configSize;
			memcpy(rtmp_data.aac_spec, &(gs_aac_enc.aac_config), rtmp_data.aac_spec_size);

			m_rtmp_sub_thread.setRtmpProperty(&rtmp_data);

			start_rtmp_url(SUB_INDEX, str_buf);
		#endif	
		}
	}

	return ret;
}

void deinit_all_server(void)
{
	int i;
	
#if USE_RTSP_STREAM
	for(i = 0; i<MAX_STREAM_NUMBER; i++)
	{
		if(g_rtsp_handle[i]) caster_chl_close(g_rtsp_handle[i]);
		g_rtsp_handle[i] = NULL;
	}

	caster_quit();
#endif

	//stop_rtmp(0);
}

int start_rtmp(int stream_index)
{
	int s32Ret = -1;

	if(stream_index < 0 || stream_index >= MAX_STREAM_NUMBER)
		return s32Ret;

	if(m_configFile.m_protocols[stream_index].rtmpEnabled)
	{
		char str_buf[512] = {0};

		if(m_configFile.m_protocols[MASTER_INDEX].rtmpAddrMode.compare("manual") == 0)
		{
			sprintf(str_buf, "rtmp://%s:%d/%s/%s", m_configFile.m_protocols[stream_index].rtmpAddr.c_str(), \
					m_configFile.m_protocols[stream_index].rtmpPort, m_configFile.m_protocols[stream_index].rtmpDir.c_str(), \
					m_configFile.m_protocols[stream_index].rtmpNode.c_str());

			printf("stream :%d, RTMP URL : %s\n", stream_index, str_buf);

			//strcpy(str_buf, "rtmp://pub.mudu.tv/watch/45p2et");
		}
		else
		{
			strcpy(str_buf, m_configFile.m_protocols[MASTER_INDEX].rtmpAddr.c_str());
		}

		m_rtmp_main_thread.m_bitrate = m_configFile.m_videos[stream_index].bitrate;
		m_rtmp_main_thread.setRtmpUrl(str_buf);
		m_rtmp_main_thread.start();
	}

	return 0;
}

int start_rtmp_url(int stream_index, char * stream_url)
{
	int s32Ret = -1;

	if(stream_index < 0 || stream_index >= MAX_STREAM_NUMBER)
		return s32Ret;

	//if(m_configFile.m_protocols[stream_index].rtmpEnabled)
	if(1)
	{
		char str_buf[256] = {0};

		strcpy(str_buf, stream_url);

		printf("\nstream :%d, RTMP URL : %s\n\n", stream_index, str_buf);

		//strcpy(str_buf, "rtmp://pub.mudu.tv/watch/45p2et");

		if(stream_index == MASTER_INDEX)
		{
			m_rtmp_main_thread.m_bitrate = m_configFile.m_videos[stream_index].bitrate;
			m_rtmp_main_thread.setRtmpUrl(str_buf);
			usleep(100);
			m_rtmp_main_thread.start();
		}
		else if(stream_index == SUB_INDEX)
		{
			m_rtmp_sub_thread.m_bitrate = m_configFile.m_videos[stream_index].bitrate;
			m_rtmp_sub_thread.setRtmpUrl(str_buf);
			usleep(100);
			m_rtmp_sub_thread.start();			
		}
	}

	return 0;
}

int stop_rtmp(int stream_index)
{
	int s32Ret = -1;

	if(stream_index < 0 || stream_index >= MAX_STREAM_NUMBER)
		return s32Ret;

	if(m_rtmp_main_thread.getThreadID())
	{
		m_rtmp_main_thread.StopConnect();
		//m_rtmp_main_thread.stop();
	}

	if(m_rtmp_sub_thread.getThreadID())
	{
		m_rtmp_sub_thread.StopConnect();
		//m_rtmp_sub_thread.stop();
	}

	return 0;
}
/*
 * for auto resolution mode
 */
void check_resolution(int input_width, int input_height, int input_Fps)
{
	printf("Video input resolution : %d*%d\n", input_width, input_height);

	// default enable stream
	videoParam.b_StreamEanable[MASTER_INDEX] = true;
	videoParam.b_StreamEanable[SUB_INDEX] = true;

	// res_mode == 1 , auto resolution mode
	if(m_configFile.m_videos[MASTER_INDEX].res_mode || (m_configFile.m_videos[MASTER_INDEX].pic_width > input_width))
	{
		master_OSD_max_w = m_configFile.m_videos[MASTER_INDEX].pic_width = input_width;
		master_OSD_max_h = m_configFile.m_videos[MASTER_INDEX].pic_height = input_height;

		printf("check resolution : master change to %d*%d\n", input_width, input_height);
	}

	if(m_configFile.m_videos[SUB_INDEX].res_mode || (m_configFile.m_videos[SUB_INDEX].pic_width > input_width))
	{
		slave_OSD_max_w = m_configFile.m_videos[SUB_INDEX].pic_width = input_width;
		slave_OSD_max_h = m_configFile.m_videos[SUB_INDEX].pic_height = input_height;

		printf("check resolution : slave change to %d*%d\n", input_width, input_height);
	}

	master_OSD_max_w = m_configFile.m_videos[MASTER_INDEX].pic_width;
	master_OSD_max_h = m_configFile.m_videos[MASTER_INDEX].pic_height;

	slave_OSD_max_w = m_configFile.m_videos[SUB_INDEX].pic_width;
	slave_OSD_max_h = m_configFile.m_videos[SUB_INDEX].pic_height;		

#if 1
	// limit the processor, only main stream can be set > 30 fps
	if((m_configFile.m_videos[MASTER_INDEX].pic_width == 1920) && (m_configFile.m_videos[MASTER_INDEX].framerate > 30))
	{
		// if(Vi_1080I == CheckMode)
		// {
		// 	videoParam.b_StreamEanable[SUB_INDEX] = true;
		// 	m_configFile.m_videos[MASTER_INDEX].framerate = CheckFps;
		// 	m_configFile.m_videos[SUB_INDEX].framerate = CheckFps;
		// }
		// else
		{
			// m_configFile.m_videos[SUB_INDEX].pic_width = 640;
			// m_configFile.m_videos[SUB_INDEX].pic_height = 480;
			// m_configFile.m_videos[SUB_INDEX].framerate = 30;
			videoParam.b_StreamEanable[SUB_INDEX] = false;
		}
	}
#endif

	// output stream frame rate can not larger than input
	if(m_configFile.m_videos[0].framerate > input_Fps)
	{
		printf("frame rate change to %d\n", input_Fps);
		m_configFile.m_videos[0].framerate = input_Fps;
		m_configFile.m_videos[0].gop = input_Fps;
	}

	if(m_configFile.m_videos[1].framerate > input_Fps)
	{
		m_configFile.m_videos[1].framerate = input_Fps;
		m_configFile.m_videos[1].gop = input_Fps;
	}
}

/*
note: get audio from HISI and codec it to special format, then put it to FIFO
parameter: buffer: audio data
           size: data size
		   timestamp: get from HISI
*/
int put_audio_2_fifo(unsigned char * buffer, int size, int64_t timestamp)
{
	#define MAX_DB	120
	#define MIN_DB  20

	const float kMaxSquaredLevel = 32768 * 32768;

	static unsigned char aac_frame[1200];
	static unsigned char audio_tmp[4096];
	static int count = 0;
	static time_t cur_time, old_time = 0;
	short * p_data = (short * )audio_tmp;
	bool frame_OK_flag = false;
	int data_len;

	if(size > sizeof(audio_tmp))
	{
		printf("too long audio data.\n");
		return -1;
	}

	//printf("PCM audio len:%d\n", size);

	if(count++ % 1 == 0)
	{
		long long pcmAllLenght = 0;
		int len = size/2;
		int tmp;

		memset(audio_tmp, 0, sizeof(audio_tmp));
		memcpy(audio_tmp, buffer, size);

		// 将 buffer 内容取出，进行平方和运算
		for (int i = 0; i < len; i++)
		{
			pcmAllLenght += (p_data[i] * p_data[i]);
		}
		// 平方和除以数据总长度，得到音量大小。
		double mean = 1.0 * pcmAllLenght/len;
		double volume_db = 10 * log10(mean); //volume为分贝数大小
		tmp = (int)volume_db;

		if(tmp < MIN_DB) tmp = MIN_DB;
		else if(tmp > MAX_DB) tmp = MAX_DB;

		gs_audio_db = tmp;
		//printf("vol: %d  dB, gs_audio_db:%d\n", (int)volume_db, gs_audio_db);
	}
	

	data_len = g711_encode(buffer, size, audio_tmp);  //G711A

	SK_RTSP_SendAudioStream(audio_tmp, data_len, timestamp);  // for RTSP
	SK_WECHAT_SendG711Audio(audio_tmp, data_len, timestamp/1000);  // for wechat

#if SUPPORT_PROTOCOL	
	SK_udp_trans_video(audio_tmp, data_len, 1, timestamp, SK_AV_AUDIO);   // g.711a
#endif

	if(videoParam.audio_codec == AUDIO_ENCODE_G711)
	{
		frame_OK_flag = true;
	}
	else if(videoParam.audio_codec == AUDIO_ENCODE_AAC)
	{
		if(gs_aac_enc.G7112Aac(buffer, size) > 0)
		{
			int offset = 0;   // 7
			memcpy(audio_tmp, &(gs_aac_enc.m_pOutAACBuffer[offset]), gs_aac_enc.aacSize - offset);

			frame_OK_flag = true;
			data_len = gs_aac_enc.aacSize - offset;

		#if 0
			aac_flv[0] = 0xaf;
			aac_flv[1] = 0x01;
			memcpy(&aac_flv[2], audio_tmp, data_len);
			flv_data_len = data_len + 2;

			RingCacheBuffer* cache = MyContext->rCache;
			cache->overlay_audio((char*)aac_flv, flv_data_len, timestamp/1000);			
		#elif USE_HTTP_FLV && !SMALL_MEMORY
			sendFlvAudioStream((char *)audio_tmp, data_len, timestamp/1000);
		#endif			
		}
	}

	if(frame_OK_flag)
	{
		//audio_stream.InPacketAudio(audio_tmp, data_len, timestamp);
		SK_sendAudioStream(MASTER_INDEX, audio_tmp, data_len, timestamp);
		SK_sendAudioStream(SUB_INDEX, audio_tmp, data_len, timestamp);

		if(m_rtmp_main_thread.b_start)
		{	
			m_rtmp_main_thread.InPacketAudio(audio_tmp, data_len, timestamp);	
		}

		if(m_rtmp_sub_thread.b_start)
		{	
			m_rtmp_sub_thread.InPacketAudio(audio_tmp, data_len, timestamp);					
		}

		if(1)   // to TS file, ADTS format
		{
			int profile = 2; // AAC_LC
			int freqIdx = 0x0b; // sampleRate: 8000-> 0x0b, 16000->8, 32000->5 48000->3
			int chanCfg = 1; // channel: 2->stero, 1->1 mon
			int aac_frame_size = data_len + 7;

			aac_frame[0] = 0xFF;
			aac_frame[1] = 0xF1;
			aac_frame[2] = (((profile - 1) << 6) + (freqIdx << 2) + (chanCfg >> 2));
			aac_frame[3] = (((chanCfg & 3) << 6) + (aac_frame_size >> 11));
			aac_frame[4] = ((aac_frame_size & 0x7FF) >> 3);
			aac_frame[5] = (((aac_frame_size & 7) << 5) + 0x1F);
			aac_frame[6] = 0xFC;

			memcpy(aac_frame + 7, audio_tmp, data_len);
			SK_FIFO_SendData(0, (char *)aac_frame, aac_frame_size, timestamp, kSDK_ENC_BUF_DATA_AAC, 1);
		}		
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////////////
int SK_getAudioDbValue(void)
{
    return gs_audio_db;
}

/*
note: get video from SOC and codec it to FIFO
parameter:  stream_index : 0: mastar stream, 1: ext stream
			v_buf: video data
            v_len: data size
		    pts: timestamp, mS !!!
			is_key : key frame flag, 0: P, 1 : I 
*/
int SK_sendEncVideoStream(int stream_index, unsigned char * v_buf, int v_len, int64_t pts, int is_key)
{
	int ret = -1;	

	if(stream_index == MASTER_INDEX)
	{
		// to record FIFO
		if(g_stream_encType[MASTER_INDEX] == VENC_CODEC_TYPE_H264)
		{
			SK_FIFO_SendData(0, (char *)v_buf, v_len, pts, kSDK_ENC_BUF_DATA_H264, is_key);
		}
		else if(g_stream_encType[MASTER_INDEX] = VENC_CODEC_TYPE_H265)
		{			
			SK_FIFO_SendData(0, (char *)v_buf, v_len, pts, kSDK_ENC_BUF_DATA_H265, is_key);
		}
	}
	else if(stream_index == SUB_INDEX)
	{

	}

	return ret;
}

// AAC to G.711A  
int SK_AUDIO_AAC_2_g711a(unsigned char * data, int size, unsigned char * data_out)
{
    static short tmp[8192];
	int pcmLen, channels = 1, len;

	if(!data || !data_out) 
	{
		printf("NULL audio buffer.\n");
		return 0;
	}

	if(((data[0] & 0xff) == 0xff) && ((data[1] & 0xf0) == 0xf0)) // if ADTS, TS is this format, ff, f1, 6c, 40, 60, ff, fc
	{
		gs_aac_decoder.fdk_decode_audio((INT_PCM *)tmp, &pcmLen, (unsigned char *)data + 7, size - 7);
	}
	else
	{
		gs_aac_decoder.fdk_decode_audio((INT_PCM *)tmp, &pcmLen, (unsigned char *)data, size);
	}

	pcmLen = pcmLen * channels;

	len = g711_encode((byte *)tmp, pcmLen, data_out);

	return len;
}