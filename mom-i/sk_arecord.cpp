#include <errno.h>
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

static int write_pcm(FILE *fp, const unsigned char *data, uint32_t len, int out_channels)
{
    uint32_t i;

    if (out_channels == 1) {
        return fwrite(data, 1, len, fp) == len ? 0 : -1;
    }

    for (i = 0; i + 1 < len; i += 2) {
        unsigned char stereo[4];
        stereo[0] = data[i];
        stereo[1] = data[i + 1];
        stereo[2] = data[i];
        stereo[3] = data[i + 1];
        if (fwrite(stereo, 1, sizeof(stereo), fp) != sizeof(stereo)) {
            return -1;
        }
    }

    return 0;
}

static void write_le16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void write_le32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static int write_wav_header(FILE *fp, uint32_t sample_rate, int channels, uint32_t data_size)
{
    unsigned char hdr[44];
    uint16_t block_align = channels * (AI_BITS_PER_SAMPLE / 8);
    uint32_t byte_rate = sample_rate * block_align;

    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr + 0, "RIFF", 4);
    write_le32(hdr + 4, 36 + data_size);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    write_le32(hdr + 16, 16);
    write_le16(hdr + 20, 1);
    write_le16(hdr + 22, (uint16_t)channels);
    write_le32(hdr + 24, sample_rate);
    write_le32(hdr + 28, byte_rate);
    write_le16(hdr + 32, block_align);
    write_le16(hdr + 34, AI_BITS_PER_SAMPLE);
    memcpy(hdr + 36, "data", 4);
    write_le32(hdr + 40, data_size);

    return fwrite(hdr, 1, sizeof(hdr), fp) == sizeof(hdr) ? 0 : -1;
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <out.wav|-> [seconds] [sample_rate] [volume] [channels]\n", prog);
    fprintf(stderr, "  File mode writes PCM WAV. '-' writes raw PCM to stdout for piping to nc.\n");
    fprintf(stderr, "  Defaults: 5 seconds, 8000 Hz, volume 0, 1 channel.\n");
    fprintf(stderr, "  seconds=0 records forever in raw stdout mode. channels=2 duplicates mono to stereo.\n");
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
    if (ret != MI_SUCCESS) {
        fprintf(stderr, "MI_AI_SetPubAttr failed: 0x%x\n", ret);
        return -1;
    }

    ret = MI_AI_Enable(dev);
    if (ret != MI_SUCCESS) {
        fprintf(stderr, "MI_AI_Enable failed: 0x%x\n", ret);
        return -1;
    }

    memset(&port, 0, sizeof(port));
    port.eModId = E_MI_MODULE_ID_AI;
    port.u32DevId = dev;
    port.u32ChnId = chn;
    port.u32PortId = 0;
    MI_SYS_SetChnOutputPortDepth(&port, 4, 4);

    ret = MI_AI_EnableChn(dev, chn);
    if (ret != MI_SUCCESS) {
        fprintf(stderr, "MI_AI_EnableChn failed: 0x%x\n", ret);
        MI_AI_Disable(dev);
        return -1;
    }

    if (volume > -50 && volume < 21) {
        MI_AI_SetVqeVolume(dev, chn, volume);
    }

    return 0;
}

static void ai_deinit(void)
{
    MI_AI_DisableChn(AI_DEV_AMIC, AI_CHN);
    MI_AI_Disable(AI_DEV_AMIC);
}

int main(int argc, char **argv)
{
    FILE *fp;
    uint32_t sample_rate = 8000;
    uint32_t seconds = 5;
    uint64_t target_in_bytes;
    uint64_t input_written = 0;
    uint64_t output_written = 0;
    int volume = 0;
    int out_channels = 1;
    int raw_stdout = 0;
    int forever = 0;
    int ret = 0;

    if (argc < 2 || argc > 6) {
        print_usage(argv[0]);
        return 1;
    }

    if (argc >= 3) seconds = (uint32_t)atoi(argv[2]);
    if (argc >= 4) sample_rate = (uint32_t)atoi(argv[3]);
    if (argc >= 5) volume = atoi(argv[4]);
    if (argc >= 6) out_channels = atoi(argv[5]);

    raw_stdout = strcmp(argv[1], "-") == 0;
    if (seconds == 0) {
        if (raw_stdout) {
            forever = 1;
        } else {
            seconds = 5;
        }
    }
    if (sample_rate != 8000 && sample_rate != 16000 &&
        sample_rate != 32000 && sample_rate != 48000) {
        fprintf(stderr, "unsupported sample rate: %u\n", sample_rate);
        return 1;
    }
    if (out_channels != 1 && out_channels != 2) {
        fprintf(stderr, "unsupported channels: %d\n", out_channels);
        return 1;
    }

    fp = raw_stdout ? stdout : fopen(argv[1], "wb+");
    if (!fp) {
        fprintf(stderr, "open %s failed: %s\n", argv[1], strerror(errno));
        return 1;
    }

    setvbuf(fp, NULL, _IONBF, 0);

    if (!raw_stdout) {
        if (write_wav_header(fp, sample_rate, out_channels, 0) < 0) {
            fprintf(stderr, "write wav header failed\n");
            fclose(fp);
            return 1;
        }
    }

    if (ai_init(sample_rate, volume) < 0) {
        fprintf(stderr, "AI init failed\n");
        if (!raw_stdout) fclose(fp);
        return 1;
    }

    target_in_bytes = (uint64_t)sample_rate * AI_CHANNELS * (AI_BITS_PER_SAMPLE / 8) * seconds;
    fprintf(stderr, "record: output=%s mode=%s seconds=%u sample_rate=%u bits=%u channels=%d\n",
            argv[1], raw_stdout ? "raw" : "wav", seconds, sample_rate, AI_BITS_PER_SAMPLE, out_channels);

    while (forever || input_written < target_in_bytes) {
        MI_AUDIO_Frame_t frame;
        MI_AUDIO_AecFrame_t aec;
        MI_S32 s32ret;
        uint32_t want;

        memset(&frame, 0, sizeof(frame));
        memset(&aec, 0, sizeof(aec));

        s32ret = MI_AI_GetFrame(AI_DEV_AMIC, AI_CHN, &frame, &aec, 200);
        if (s32ret != MI_SUCCESS) {
            fprintf(stderr, "MI_AI_GetFrame failed: 0x%x\n", s32ret);
            usleep(20 * 1000);
            continue;
        }

        want = frame.u32Len;
        if (!forever && input_written + want > target_in_bytes) {
            want = (uint32_t)(target_in_bytes - input_written);
        }

        if (want > 0 && write_pcm(fp, (const unsigned char *)frame.apVirAddr[0], want, out_channels) != 0) {
            fprintf(stderr, "write pcm failed: %s\n", strerror(errno));
            ret = 1;
        } else {
            input_written += want;
            output_written += (uint64_t)want * out_channels;
        }

        MI_AI_ReleaseFrame(AI_DEV_AMIC, AI_CHN, &frame, NULL);

        if (ret != 0) break;
    }

    ai_deinit();

    if (!raw_stdout && fseek(fp, 0, SEEK_SET) == 0) {
        write_wav_header(fp, sample_rate, out_channels, (uint32_t)output_written);
    }

    if (!raw_stdout) fclose(fp);
    fprintf(stderr, "record done: %llu bytes\n", (unsigned long long)output_written);
    return ret;
}
