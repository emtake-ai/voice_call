#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mi_ai.h"
#include "mi_aio_datatype.h"
#include "mi_sys.h"

#define AI_DEV_AMIC 0
#define AI_CHN 0
#define AI_SAMPLE_PER_FRAME 768
#define AI_CHANNELS 1
#define AI_BITS_PER_SAMPLE 16
#define MIN_DB 20
#define MAX_DB 120

static volatile sig_atomic_t gs_exit = 0;

static void handle_signal(int sig)
{
    (void)sig;
    gs_exit = 1;
}

static int ai_init(uint32_t sample_rate, int volume)
{
    MI_AUDIO_Attr_t attr;
    MI_SYS_ChnPort_t port;
    MI_AUDIO_DEV dev = AI_DEV_AMIC;
    MI_AI_CHN chn = AI_CHN;
    MI_S32 ret;

    memset(&attr, 0, sizeof(attr));
    attr.eBitwidth = E_MI_AUDIO_BIT_WIDTH_16;
    attr.eSamplerate = (MI_AUDIO_SampleRate_e)sample_rate;
    attr.eSoundmode = E_MI_AUDIO_SOUND_MODE_MONO;
    attr.eWorkmode = E_MI_AUDIO_MODE_TDM_MASTER;
    attr.u32ChnCnt = AI_CHANNELS;
    attr.u32CodecChnCnt = 0;
    attr.u32FrmNum = 6;
    attr.u32PtNumPerFrm = AI_SAMPLE_PER_FRAME;
    attr.WorkModeSetting.stI2sConfig.bSyncClock = FALSE;
    attr.WorkModeSetting.stI2sConfig.eFmt = E_MI_AUDIO_I2S_FMT_I2S_MSB;
    attr.WorkModeSetting.stI2sConfig.eMclk = E_MI_AUDIO_I2S_MCLK_0;
    attr.WorkModeSetting.stI2sConfig.u32TdmSlots = 8;
    attr.WorkModeSetting.stI2sConfig.eI2sBitWidth = E_MI_AUDIO_BIT_WIDTH_32;

    ret = MI_AI_SetPubAttr(dev, &attr);
    if(ret != MI_SUCCESS)
    {
        printf("sk_mic: MI_AI_SetPubAttr failed: 0x%x\n", ret);
        return -1;
    }

    ret = MI_AI_Enable(dev);
    if(ret != MI_SUCCESS)
    {
        printf("sk_mic: MI_AI_Enable failed: 0x%x\n", ret);
        return -1;
    }

    memset(&port, 0, sizeof(port));
    port.eModId = E_MI_MODULE_ID_AI;
    port.u32DevId = dev;
    port.u32ChnId = chn;
    port.u32PortId = 0;
    MI_SYS_SetChnOutputPortDepth(&port, 4, 4);

    ret = MI_AI_EnableChn(dev, chn);
    if(ret != MI_SUCCESS)
    {
        printf("sk_mic: MI_AI_EnableChn failed: 0x%x\n", ret);
        MI_AI_Disable(dev);
        return -1;
    }

    if(volume > -50 && volume < 21)
    {
        MI_AI_SetVqeVolume(dev, chn, volume);
    }

    return 0;
}

static void ai_deinit(void)
{
    MI_AI_DisableChn(AI_DEV_AMIC, AI_CHN);
    MI_AI_Disable(AI_DEV_AMIC);
}

static int calc_db(const unsigned char *pcm, uint32_t len)
{
    const short *samples = (const short *)pcm;
    int sample_count = len / 2;
    long long square_sum = 0;

    if(sample_count <= 0)
    {
        return MIN_DB;
    }

    for(int i = 0; i < sample_count; i++)
    {
        square_sum += samples[i] * samples[i];
    }

    double mean = 1.0 * square_sum / sample_count;
    int db = (mean > 0.0) ? (int)(10.0 * log10(mean)) : MIN_DB;

    if(db < MIN_DB)
    {
        db = MIN_DB;
    }
    else if(db > MAX_DB)
    {
        db = MAX_DB;
    }

    return db;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [count] [sample_rate] [volume]\n", prog);
    printf("  count=0 means run forever. Defaults: count=0, sample_rate=8000, volume=0\n");
}

int main(int argc, char **argv)
{
    uint32_t sample_rate = 8000;
    int volume = 0;
    int count = 0;
    int printed = 0;

    if(argc > 4)
    {
        print_usage(argv[0]);
        return 1;
    }

    if(argc >= 2)
    {
        count = atoi(argv[1]);
    }

    if(argc >= 3)
    {
        sample_rate = (uint32_t)atoi(argv[2]);
    }

    if(argc >= 4)
    {
        volume = atoi(argv[3]);
    }

    if(sample_rate != 8000 && sample_rate != 16000 &&
       sample_rate != 32000 && sample_rate != 48000)
    {
        printf("sk_mic: unsupported sample rate: %u\n", sample_rate);
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if(ai_init(sample_rate, volume) < 0)
    {
        printf("sk_mic: AI init failed\n");
        return 1;
    }

    printf("sk_mic: sample_rate=%u volume=%d count=%d\n", sample_rate, volume, count);

    while(!gs_exit && (count <= 0 || printed < count))
    {
        MI_AUDIO_Frame_t frame;
        MI_AUDIO_AecFrame_t aec;
        MI_S32 ret;

        memset(&frame, 0, sizeof(frame));
        memset(&aec, 0, sizeof(aec));

        ret = MI_AI_GetFrame(AI_DEV_AMIC, AI_CHN, &frame, &aec, 200);
        if(ret != MI_SUCCESS)
        {
            printf("sk_mic: MI_AI_GetFrame failed: 0x%x\n", ret);
            usleep(20 * 1000);
            continue;
        }

        int db = calc_db((const unsigned char *)frame.apVirAddr[0], frame.u32Len);
        printf("mic_db=%d\n", db);
        fflush(stdout);
        printed++;

        MI_AI_ReleaseFrame(AI_DEV_AMIC, AI_CHN, &frame, NULL);
    }

    ai_deinit();
    return 0;
}
