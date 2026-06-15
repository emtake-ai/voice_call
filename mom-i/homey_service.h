#ifndef __HOMEY_SERVICE_H__
#define __HOMEY_SERVICE_H__

typedef struct _HOMEY_SENSOR_VALUE_S {
    float temperature;
    float humidity;
    int volume;
    int light;
    float thermal_temperature;
} HOMEY_SENSOR_VALUE_S;

typedef struct _HOMEY_MEDIA_INFO_S {
    int rtsp_port;
    char rtsp_path[128];
    int width;
    int height;
    int fps;
    int audio_freq;
    int audio_channels;
    int audio_bits;
} HOMEY_MEDIA_INFO_S;

int SK_HOMEY_startService(void);
void SK_HOMEY_stopService(void);
void SK_HOMEY_updateSensorValue(const HOMEY_SENSOR_VALUE_S *value);
void SK_HOMEY_updateMediaInfo(const HOMEY_MEDIA_INFO_S *info);

#endif
