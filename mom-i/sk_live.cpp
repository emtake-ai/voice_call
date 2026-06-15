#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include <opus/opus.h>

#include "mi_ai.h"
#include "mi_ao.h"
#include "mi_aio_datatype.h"
#include "mi_sys.h"

#define AI_DEV_AMIC 0
#define AI_CHN 0
#define AI_SAMPLE_PER_FRAME 768
#define AI_CHANNELS 1
#define AI_BITS_PER_SAMPLE 16
#define AO_DEV_LINEOUT 0
#define AO_CHN 0
#define AUDIO_SAMPLE_PER_FRAME 768

#define GPIO_IOC_MAGIC 'p'
#define GPIO_PIN_CLR _IOW(GPIO_IOC_MAGIC, 1, int)
#define GPIO_PIN_SET _IOW(GPIO_IOC_MAGIC, 2, int)
#define DIR_PIN_OUT _IOW(GPIO_IOC_MAGIC, 4, int)

#if defined(SSC3357)
#define SPEAKER_GPIO 45
#else
#define SPEAKER_GPIO 9
#endif

#define DEFAULT_PC_IP "192.168.1.2"
#define DEFAULT_RTP_PORT 5004
#define DEFAULT_SAMPLE_RATE 16000
#define DEFAULT_FRAME_MS 20
#define DEFAULT_BITRATE 24000
#define DEFAULT_MIC_VOLUME 10
#define DEFAULT_SPEAKER_VOLUME 20
#define DEFAULT_SPEAKER_GAIN 2
#define RTP_PAYLOAD_TYPE_OPUS 96
#define RTP_CLOCK_RATE 48000
#define MAX_OPUS_PACKET 4000
#define AO_SEND_BYTES 512

static volatile sig_atomic_t g_exit = 0;

static void on_signal(int signo)
{
    (void)signo;
    g_exit = 1;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [pc_ip] [port] [sample_rate] [bitrate] [mic_volume] [speaker_volume] [speaker_gain]\n"
            "  Defaults: %s %d %d %d %d %d %d\n"
            "  Full duplex: MIC -> Opus/RTP/UDP -> PC, and PC -> Opus/RTP/UDP -> speaker.\n"
            "  PC receive IoT MIC:\n"
            "    gst-launch-1.0 -v udpsrc port=%d "
            "caps=\"application/x-rtp,media=audio,clock-rate=48000,encoding-name=OPUS,payload=96\" "
            "! rtpopusdepay ! opusdec ! audioconvert ! audioresample ! autoaudiosink\n"
            "  PC send audio to IoT speaker:\n"
            "    gst-launch-1.0 -v autoaudiosrc ! audioconvert ! audioresample ! opusenc bitrate=%d "
            "! rtpopuspay pt=96 ! udpsink host=192.168.1.2 port=%d\n",
            prog, DEFAULT_PC_IP, DEFAULT_RTP_PORT, DEFAULT_SAMPLE_RATE,
            DEFAULT_BITRATE, DEFAULT_MIC_VOLUME, DEFAULT_SPEAKER_VOLUME, DEFAULT_SPEAKER_GAIN, DEFAULT_RTP_PORT,
            DEFAULT_BITRATE, DEFAULT_RTP_PORT);
}

static int parse_int_arg(const char *s, int min_value, int max_value, int *out)
{
    char *end = NULL;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < min_value || v > max_value) {
        return -1;
    }

    *out = (int)v;
    return 0;
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
    if (fp) {
        fputs(value ? "1" : "0", fp);
        fclose(fp);
    }
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
        fprintf(stderr, "MI_AO_SetPubAttr failed: 0x%x\n", ret);
        return -1;
    }

    ret = MI_AO_Enable(dev);
    if (ret != MI_SUCCESS) {
        fprintf(stderr, "MI_AO_Enable failed: 0x%x\n", ret);
        return -1;
    }

    ret = MI_AO_EnableChn(dev, chn);
    if (ret != MI_SUCCESS) {
        fprintf(stderr, "MI_AO_EnableChn failed: 0x%x\n", ret);
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
    } while (ret == MI_AO_ERR_NOBUF && !g_exit);

    if (ret != MI_SUCCESS) {
        fprintf(stderr, "MI_AO_SendFrame failed: 0x%x\n", ret);
        return -1;
    }

    return 0;
}

static void pcm_apply_gain(opus_int16 *pcm, int samples, int gain)
{
    int i;

    if (gain <= 1) {
        return;
    }

    for (i = 0; i < samples; ++i) {
        int v = pcm[i] * gain;
        if (v > 32767) {
            v = 32767;
        } else if (v < -32768) {
            v = -32768;
        }
        pcm[i] = (opus_int16)v;
    }
}

static int ao_send_chunked(unsigned char *data, int len)
{
    int offset = 0;

    while (offset < len && !g_exit) {
        int chunk = len - offset;
        if (chunk > AO_SEND_BYTES) {
            chunk = AO_SEND_BYTES;
        }
        chunk &= ~1;
        if (chunk <= 0) {
            break;
        }

        if (ao_send(data + offset, chunk) < 0) {
            return -1;
        }
        offset += chunk;
    }

    return 0;
}

static uint32_t make_ssrc(void)
{
    uint32_t v = (uint32_t)time(NULL);
    v ^= (uint32_t)getpid() << 16;
    v ^= 0x534b4f50u;
    return v;
}

static int send_rtp_packet(int sockfd, const struct sockaddr_in *dst,
                           uint8_t payload_type, uint16_t seq, uint32_t timestamp,
                           uint32_t ssrc, int marker,
                           const unsigned char *payload, int payload_len)
{
    unsigned char packet[12 + MAX_OPUS_PACKET];

    if (payload_len <= 0 || payload_len > MAX_OPUS_PACKET) {
        return -1;
    }

    packet[0] = 0x80;
    packet[1] = (unsigned char)((marker ? 0x80 : 0x00) | (payload_type & 0x7f));
    packet[2] = (unsigned char)(seq >> 8);
    packet[3] = (unsigned char)(seq & 0xff);
    packet[4] = (unsigned char)(timestamp >> 24);
    packet[5] = (unsigned char)(timestamp >> 16);
    packet[6] = (unsigned char)(timestamp >> 8);
    packet[7] = (unsigned char)(timestamp & 0xff);
    packet[8] = (unsigned char)(ssrc >> 24);
    packet[9] = (unsigned char)(ssrc >> 16);
    packet[10] = (unsigned char)(ssrc >> 8);
    packet[11] = (unsigned char)(ssrc & 0xff);
    memcpy(packet + 12, payload, payload_len);

    return sendto(sockfd, packet, 12 + payload_len, 0,
                  (const struct sockaddr *)dst, sizeof(*dst)) == 12 + payload_len ? 0 : -1;
}

static int rtp_payload(const unsigned char *packet, int packet_len,
                       const unsigned char **payload, int *payload_len)
{
    int cc;
    int hdr_len;

    if (packet_len < 12 || (packet[0] & 0xc0) != 0x80) {
        return -1;
    }
    if ((packet[1] & 0x7f) != RTP_PAYLOAD_TYPE_OPUS) {
        return -1;
    }

    cc = packet[0] & 0x0f;
    hdr_len = 12 + cc * 4;
    if (packet_len < hdr_len) {
        return -1;
    }

    if (packet[0] & 0x10) {
        int ext_len;
        if (packet_len < hdr_len + 4) {
            return -1;
        }
        ext_len = ((packet[hdr_len + 2] << 8) | packet[hdr_len + 3]) * 4;
        hdr_len += 4 + ext_len;
        if (packet_len < hdr_len) {
            return -1;
        }
    }

    *payload = packet + hdr_len;
    *payload_len = packet_len - hdr_len;
    return *payload_len > 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *pc_ip = DEFAULT_PC_IP;
    int port = DEFAULT_RTP_PORT;
    int sample_rate = DEFAULT_SAMPLE_RATE;
    int bitrate = DEFAULT_BITRATE;
    int mic_volume = DEFAULT_MIC_VOLUME;
    int speaker_volume = DEFAULT_SPEAKER_VOLUME;
    int speaker_gain = DEFAULT_SPEAKER_GAIN;
    int frame_samples;
    int frame_bytes;
    int rtp_ts_step;
    int opus_err;
    int sockfd = -1;
    int flags;
    int ret = 1;
    int ai_started = 0;
    int ao_started = 0;
    uint16_t seq = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = make_ssrc();
    OpusEncoder *encoder = NULL;
    OpusDecoder *decoder = NULL;
    struct sockaddr_in dst;
    struct sockaddr_in local;
    std::vector<unsigned char> pcm_buf;
    unsigned char opus_packet[MAX_OPUS_PACKET];
    unsigned char rx_packet[1600];
    opus_int16 decode_pcm[RTP_CLOCK_RATE * 120 / 1000];

    if (argc > 8 || (argc == 2 && strcmp(argv[1], "-h") == 0)) {
        print_usage(argv[0]);
        return argc > 8 ? 1 : 0;
    }

    if (argc >= 2) pc_ip = argv[1];
    if (argc >= 3 && parse_int_arg(argv[2], 1, 65535, &port) < 0) {
        fprintf(stderr, "invalid port: %s\n", argv[2]);
        return 1;
    }
    if (argc >= 4 && parse_int_arg(argv[3], 8000, 48000, &sample_rate) < 0) {
        fprintf(stderr, "invalid sample_rate: %s\n", argv[3]);
        return 1;
    }
    if (sample_rate != 8000 && sample_rate != 12000 && sample_rate != 16000 &&
        sample_rate != 24000 && sample_rate != 48000) {
        fprintf(stderr, "unsupported Opus sample_rate: %d\n", sample_rate);
        return 1;
    }
    if (argc >= 5 && parse_int_arg(argv[4], 6000, 510000, &bitrate) < 0) {
        fprintf(stderr, "invalid bitrate: %s\n", argv[4]);
        return 1;
    }
    if (argc >= 6 && parse_int_arg(argv[5], -50, 20, &mic_volume) < 0) {
        fprintf(stderr, "invalid mic_volume: %s\n", argv[5]);
        return 1;
    }
    if (argc >= 7 && parse_int_arg(argv[6], -50, 29, &speaker_volume) < 0) {
        fprintf(stderr, "invalid speaker_volume: %s\n", argv[6]);
        return 1;
    }
    if (argc >= 8 && parse_int_arg(argv[7], 1, 8, &speaker_gain) < 0) {
        fprintf(stderr, "invalid speaker_gain: %s\n", argv[7]);
        return 1;
    }

    frame_samples = sample_rate * DEFAULT_FRAME_MS / 1000;
    frame_bytes = frame_samples * AI_CHANNELS * (AI_BITS_PER_SAMPLE / 8);
    rtp_ts_step = RTP_CLOCK_RATE * DEFAULT_FRAME_MS / 1000;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGPIPE, SIG_IGN);

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)port);
    if (inet_aton(pc_ip, &dst.sin_addr) == 0) {
        fprintf(stderr, "invalid pc_ip: %s\n", pc_ip);
        return 1;
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "socket failed: %s\n", strerror(errno));
        return 1;
    }

    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons((uint16_t)port);
    if (bind(sockfd, (const struct sockaddr *)&local, sizeof(local)) < 0) {
        fprintf(stderr, "bind UDP port %d failed: %s\n", port, strerror(errno));
        goto out;
    }

    flags = fcntl(sockfd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    }

    encoder = opus_encoder_create(sample_rate, AI_CHANNELS, OPUS_APPLICATION_VOIP, &opus_err);
    if (!encoder || opus_err != OPUS_OK) {
        fprintf(stderr, "opus_encoder_create failed: %s\n", opus_strerror(opus_err));
        goto out;
    }
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitrate));
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(5));

    decoder = opus_decoder_create(sample_rate, AI_CHANNELS, &opus_err);
    if (!decoder || opus_err != OPUS_OK) {
        fprintf(stderr, "opus_decoder_create failed: %s\n", opus_strerror(opus_err));
        goto out;
    }

    if (ai_init((uint32_t)sample_rate, mic_volume) < 0) {
        fprintf(stderr, "AI init failed\n");
        goto out;
    }
    ai_started = 1;

    if (ao_init((uint32_t)sample_rate, speaker_volume) < 0) {
        fprintf(stderr, "AO init failed\n");
        goto out;
    }
    ao_started = 1;
    set_speaker_gpio(1);

    fprintf(stderr,
            "sk_live: full-duplex Opus/RTP/UDP pc=%s port=%d sample_rate=%d frame=%dms bitrate=%d payload=%d mic_vol=%d spk_vol=%d spk_gain=%d\n",
            pc_ip, port, sample_rate, DEFAULT_FRAME_MS, bitrate, RTP_PAYLOAD_TYPE_OPUS,
            mic_volume, speaker_volume, speaker_gain);

    ret = 0;
    while (!g_exit) {
        MI_AUDIO_Frame_t frame;
        MI_AUDIO_AecFrame_t aec;
        MI_S32 s32ret;
        int rx_count = 0;

        while (rx_count++ < 8) {
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            const unsigned char *payload = NULL;
            int payload_len = 0;
            int packet_len;
            int decoded_samples;

            packet_len = recvfrom(sockfd, rx_packet, sizeof(rx_packet), 0,
                                  (struct sockaddr *)&from, &from_len);
            if (packet_len < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    fprintf(stderr, "recvfrom failed: %s\n", strerror(errno));
                }
                break;
            }

            if (rtp_payload(rx_packet, packet_len, &payload, &payload_len) < 0) {
                continue;
            }

            decoded_samples = opus_decode(decoder, payload, payload_len, decode_pcm,
                                          sizeof(decode_pcm) / sizeof(decode_pcm[0]), 0);
            if (decoded_samples < 0) {
                fprintf(stderr, "opus_decode failed: %s\n", opus_strerror(decoded_samples));
                continue;
            }

            pcm_apply_gain(decode_pcm, decoded_samples * AI_CHANNELS, speaker_gain);

            if (ao_send_chunked((unsigned char *)decode_pcm,
                                decoded_samples * AI_CHANNELS * (AI_BITS_PER_SAMPLE / 8)) < 0) {
                ret = 1;
                g_exit = 1;
                break;
            }
        }

        memset(&frame, 0, sizeof(frame));
        memset(&aec, 0, sizeof(aec));

        s32ret = MI_AI_GetFrame(AI_DEV_AMIC, AI_CHN, &frame, &aec, 20);
        if (s32ret != MI_SUCCESS) {
            usleep(5 * 1000);
            continue;
        }

        if (frame.u32Len > 0 && frame.apVirAddr[0]) {
            const unsigned char *p = (const unsigned char *)frame.apVirAddr[0];
            pcm_buf.insert(pcm_buf.end(), p, p + frame.u32Len);
        }

        MI_AI_ReleaseFrame(AI_DEV_AMIC, AI_CHN, &frame, NULL);

        while ((int)pcm_buf.size() >= frame_bytes) {
            const opus_int16 *pcm = (const opus_int16 *)&pcm_buf[0];
            int opus_len = opus_encode(encoder, pcm, frame_samples, opus_packet, sizeof(opus_packet));
            if (opus_len < 0) {
                fprintf(stderr, "opus_encode failed: %s\n", opus_strerror(opus_len));
                ret = 1;
                g_exit = 1;
                break;
            }

            if (send_rtp_packet(sockfd, &dst, RTP_PAYLOAD_TYPE_OPUS, seq, timestamp, ssrc,
                                seq == 0, opus_packet, opus_len) < 0) {
                fprintf(stderr, "sendto failed: %s\n", strerror(errno));
                ret = 1;
                g_exit = 1;
                break;
            }

            seq++;
            timestamp += (uint32_t)rtp_ts_step;
            pcm_buf.erase(pcm_buf.begin(), pcm_buf.begin() + frame_bytes);
        }
    }

    if (ao_started) {
        set_speaker_gpio(0);
        ao_deinit();
    }
    if (ai_started) {
        ai_deinit();
    }

out:
    if (ao_started && !g_exit) {
        set_speaker_gpio(0);
        ao_deinit();
    }
    if (ai_started && !g_exit) {
        ai_deinit();
    }
    if (decoder) opus_decoder_destroy(decoder);
    if (encoder) opus_encoder_destroy(encoder);
    if (sockfd >= 0) close(sockfd);
    fprintf(stderr, "sk_live: stopped\n");
    return ret;
}
