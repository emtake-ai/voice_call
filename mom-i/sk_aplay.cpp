#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "mi_ao.h"
#include "mi_aio_datatype.h"

#define AO_DEV_LINEOUT 0
#define AO_CHN 0
#define AUDIO_SAMPLE_PER_FRAME 768
#define AUDIO_SEND_BYTES 512

#define GPIO_IOC_MAGIC 'p'
#define GPIO_PIN_CLR _IOW(GPIO_IOC_MAGIC, 1, int)
#define GPIO_PIN_SET _IOW(GPIO_IOC_MAGIC, 2, int)
#define DIR_PIN_OUT _IOW(GPIO_IOC_MAGIC, 4, int)

#if defined(SSC3357)
#define SPEAKER_GPIO 45
#else
#define SPEAKER_GPIO 9
#endif

struct WavInfo {
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_size;
};

static uint16_t read_le16(const unsigned char *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_exact(FILE *fp, unsigned char *buf, size_t len)
{
    return fread(buf, 1, len, fp) == len ? 0 : -1;
}

static int parse_wav(FILE *fp, WavInfo *info)
{
    unsigned char hdr[12];
    int have_fmt = 0;
    int have_data = 0;

    memset(info, 0, sizeof(*info));
    if (read_exact(fp, hdr, sizeof(hdr)) < 0) return -1;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return -1;

    while (!have_data) {
        unsigned char chunk[8];
        uint32_t size;
        long data_pos;

        if (read_exact(fp, chunk, sizeof(chunk)) < 0) return -1;
        size = read_le32(chunk + 4);
        data_pos = ftell(fp);
        if (data_pos < 0) return -1;

        if (memcmp(chunk, "fmt ", 4) == 0) {
            unsigned char fmt[40];
            if (size < 16 || size > sizeof(fmt)) return -1;
            if (read_exact(fp, fmt, size) < 0) return -1;

            info->format = read_le16(fmt + 0);
            info->channels = read_le16(fmt + 2);
            info->sample_rate = read_le32(fmt + 4);
            info->bits_per_sample = read_le16(fmt + 14);
            have_fmt = 1;
        } else if (memcmp(chunk, "data", 4) == 0) {
            info->data_offset = (uint32_t)data_pos;
            info->data_size = size;
            have_data = 1;
            break;
        }

        if (!have_data) {
            long skip = data_pos + size + (size & 1);
            if (fseek(fp, skip, SEEK_SET) != 0) return -1;
        }
    }

    return have_fmt && have_data ? 0 : -1;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s <file.wav> [volume]\n", prog);
    printf("  Supports PCM WAV, 16-bit, mono, 8000/16000/32000/48000 Hz.\n");
    printf("  volume is optional AO volume, typical range -50..29.\n");
}

static void set_speaker_gpio(int value)
{
    char path[64];
    FILE *fp;
    int fd;
    int pin = SPEAKER_GPIO;

    fd = open("/dev/sk_gpio", O_RDWR);
    if (fd >= 0) {
        ioctl(fd, DIR_PIN_OUT, &pin);
        ioctl(fd, value ? GPIO_PIN_SET : GPIO_PIN_CLR, &pin);
        close(fd);
        return;
    }

    fp = fopen("/sys/class/gpio/export", "w");
    if (fp) {
        fprintf(fp, "%d", SPEAKER_GPIO);
        fclose(fp);
        usleep(50 * 1000);
    }

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", SPEAKER_GPIO);
    fp = fopen(path, "w");
    if (fp) {
        fputs("out", fp);
        fclose(fp);
    }

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", SPEAKER_GPIO);
    fp = fopen(path, "w");
    if (!fp) {
        printf("speaker gpio%d is not available\n", SPEAKER_GPIO);
        return;
    }

    fputs(value ? "1" : "0", fp);
    fclose(fp);
}

static int ao_init(uint32_t sample_rate, int volume)
{
    MI_AUDIO_Attr_t attr;
    MI_AUDIO_DEV dev = AO_DEV_LINEOUT;
    MI_AO_CHN chn = AO_CHN;
    MI_S32 ret;

    memset(&attr, 0, sizeof(attr));
    attr.eBitwidth = E_MI_AUDIO_BIT_WIDTH_16;
#if defined(SSC3357)
    attr.eWorkmode = E_MI_AUDIO_MODE_I2S_MASTER;
#else
    attr.eWorkmode = E_MI_AUDIO_MODE_I2S_SLAVE;
#endif
    attr.WorkModeSetting.stI2sConfig.bSyncClock = TRUE;
    attr.WorkModeSetting.stI2sConfig.eFmt = E_MI_AUDIO_I2S_FMT_I2S_MSB;
    attr.WorkModeSetting.stI2sConfig.eMclk = E_MI_AUDIO_I2S_MCLK_0;
    attr.u32PtNumPerFrm = AUDIO_SAMPLE_PER_FRAME;
    attr.u32ChnCnt = 1;
    attr.eSoundmode = E_MI_AUDIO_SOUND_MODE_MONO;
    attr.eSamplerate = (MI_AUDIO_SampleRate_e)sample_rate;

    ret = MI_AO_SetPubAttr(dev, &attr);
    if (ret != MI_SUCCESS) {
        printf("MI_AO_SetPubAttr failed: 0x%x\n", ret);
        return -1;
    }

    ret = MI_AO_Enable(dev);
    if (ret != MI_SUCCESS) {
        printf("MI_AO_Enable failed: 0x%x\n", ret);
        return -1;
    }

    ret = MI_AO_EnableChn(dev, chn);
    if (ret != MI_SUCCESS) {
        printf("MI_AO_EnableChn failed: 0x%x\n", ret);
        MI_AO_Disable(dev);
        return -1;
    }

    if (volume > -50 && volume < 30) {
        MI_AO_SetVolume(dev, volume);
    }

    return 0;
}

static void ao_deinit(void)
{
    MI_AO_DisableChn(AO_DEV_LINEOUT, AO_CHN);
    MI_AO_Disable(AO_DEV_LINEOUT);
}

static int ao_send(unsigned char *data, int len)
{
    MI_AUDIO_Frame_t frame;
    MI_S32 ret;

    memset(&frame, 0, sizeof(frame));
    frame.u32Len = len;
    frame.u64TimeStamp = 0;
    frame.apVirAddr[0] = data;
    frame.apVirAddr[1] = NULL;

    do {
        ret = MI_AO_SendFrame(AO_DEV_LINEOUT, AO_CHN, &frame, -1);
    } while (ret == MI_AO_ERR_NOBUF);

    if (ret != MI_SUCCESS) {
        printf("MI_AO_SendFrame failed: 0x%x\n", ret);
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    FILE *fp;
    WavInfo wav;
    unsigned char buf[4096];
    uint32_t left;
    int ret = 0;
    int volume = 0;

    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return 1;
    }

    if (argc == 3) {
        volume = atoi(argv[2]);
    }

    fp = fopen(argv[1], "rb");
    if (!fp) {
        printf("open %s failed: %s\n", argv[1], strerror(errno));
        return 1;
    }

    if (parse_wav(fp, &wav) < 0) {
        printf("invalid wav file: %s\n", argv[1]);
        fclose(fp);
        return 1;
    }

    printf("wav: format=%u channels=%u sample_rate=%u bits=%u data=%u\n",
           wav.format, wav.channels, wav.sample_rate, wav.bits_per_sample, wav.data_size);

    if (wav.format != 1 || wav.channels != 1 || wav.bits_per_sample != 16) {
        printf("unsupported wav format, need PCM 16-bit mono\n");
        fclose(fp);
        return 1;
    }

    if (wav.sample_rate != 8000 && wav.sample_rate != 16000 &&
        wav.sample_rate != 32000 && wav.sample_rate != 48000) {
        printf("unsupported sample rate: %u\n", wav.sample_rate);
        fclose(fp);
        return 1;
    }

    if (ao_init(wav.sample_rate, volume) < 0) {
        printf("AO init failed\n");
        fclose(fp);
        return 1;
    }

    set_speaker_gpio(1);

    if (fseek(fp, wav.data_offset, SEEK_SET) != 0) {
        printf("seek data failed\n");
        ret = 1;
        goto out;
    }

    left = wav.data_size;
    while (left > 0) {
        size_t frame_bytes = AUDIO_SEND_BYTES;
        size_t want = left > frame_bytes ? frame_bytes : left;
        size_t got = fread(buf, 1, want, fp);
        if (got == 0) break;

        if (ao_send(buf, (int)got) != 0) {
            ret = 1;
            break;
        }

        usleep((useconds_t)((got * 1000000ULL) / (wav.sample_rate * wav.channels * (wav.bits_per_sample / 8))));
        left -= (uint32_t)got;
    }

    sleep(5);

out:
    set_speaker_gpio(0);
    ao_deinit();
    fclose(fp);
    return ret;
}
