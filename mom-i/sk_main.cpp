#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "configfile.h"
#include "media_server.h"
#include "soc_server.h"
#include "rtsp_enctry.h"
#include "i2c/sk_humid_temp.h"
#include "GEN2Protocol.h"
#include "net_trans.h"
#include "p2p_entry.h"
#include "check_wifi.h"
#include "Event.h"
#include "sys_conf.h"

#define SK_MAIN_HTTP_PORT 8090
#define SK_THERMAL_UART_DEV "/dev/ttyUSB1"
#define SK_THERMAL_UART_BAUD 2000000
#define SK_THERMAL_TCP_PORT 8554
#define SK_THERMAL_WIDTH 80
#define SK_THERMAL_HEIGHT 60
#define SK_THERMAL_RAW_SIZE (SK_THERMAL_WIDTH * SK_THERMAL_HEIGHT * 2)
#define SK_THERMAL_RX_SIZE (20 * 1024)
#define SK_THERMAL_MAGIC 0x534B5448u

#define SK_RADAR_UART_DEV "/dev/ttyS3"
#define SK_RADAR_UART_BAUD 115200
#define SK_RADAR_BUF_SIZE 256

typedef struct __attribute__((packed)) _SKThermalFrameHeader_ {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint16_t width;
    uint16_t height;
    uint16_t pixel_format;
    uint16_t fps;
    uint32_t frame_no;
    uint64_t timestamp_ms;
    uint32_t payload_size;
} SKThermalFrameHeader_t;

typedef struct _SKMainSensorState_ {
    int mic_db;
    float temperature;
    float humidity;
    char radar[128];
} SKMainSensorState_t;

static volatile sig_atomic_t gs_exit = 0;
static volatile bool gs_sensor_loop = false;
static volatile bool gs_http_loop = false;
static volatile bool gs_radar_loop = false;
static volatile bool gs_thermal_loop = false;

static pthread_t gs_sensor_tid;
static pthread_t gs_http_tid;
static pthread_t gs_radar_tid;
static pthread_t gs_thermal_tid;
static pthread_mutex_t gs_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static SKMainSensorState_t gs_state;
static int gs_thermal_uart_fd = -1;

int snap_short_num_jpg = 0;
CMyEvent g_myEvent;
CMyEvent g_notifyEvent;

int SK_SYS_isTestMode(void)
{
    return 0;
}

int get_focus_flag(void)
{
    return 0;
}

void send_IIRFIR_2_serial(int type, unsigned int value)
{
    (void)type;
    (void)value;
}

void SK_set_audioFormat(Audio_Infor_t audio_infor)
{
    (void)audio_infor;
}

void SK_set_videoCodec(int type, int width, int height)
{
    (void)type;
    (void)width;
    (void)height;
}

extern "C" void SK_P2P_SetVideoEncType(P2P_VENC_TYPE enc_type)
{
    (void)enc_type;
}

extern "C" int SK_WECHAT_SendG711Audio(unsigned char *data, int len, int64_t pts)
{
    (void)data;
    (void)len;
    (void)pts;
    return 0;
}

extern "C" int SK_WECHAT_SendH264Nalu(unsigned char *data, int len, int64_t pts, int is_key)
{
    (void)data;
    (void)len;
    (void)pts;
    (void)is_key;
    return 0;
}

extern "C" int SK_WECHAT_SetImageSize(int index, int width, int height)
{
    (void)index;
    (void)width;
    (void)height;
    return 0;
}

int SK_udp_trans_video(unsigned char *buffer, int av_frame_len, int key, long long ts, int type)
{
    (void)buffer;
    (void)av_frame_len;
    (void)key;
    (void)ts;
    (void)type;
    return 0;
}

int SK_P2P_putAlarmType_2_queue(int type)
{
    (void)type;
    return 0;
}

int get_netlink_status(const char *if_name)
{
    (void)if_name;
    return 0;
}

int SK_NET_getWifiScanFlag(void)
{
    return 0;
}

void SK_NET_resetWifiScanFlag(void)
{
}

void SK_NET_setWifiInfo(SK_ApInfo_t *ap_info, int num)
{
    (void)ap_info;
    (void)num;
}

extern "C" time_t time_HumanToUnix(int year, int month, int day, int hour, int minute, int second)
{
    struct tm tm_value;
    memset(&tm_value, 0, sizeof(tm_value));
    tm_value.tm_year = year - 1900;
    tm_value.tm_mon = month - 1;
    tm_value.tm_mday = day;
    tm_value.tm_hour = hour;
    tm_value.tm_min = minute;
    tm_value.tm_sec = second;
    return mktime(&tm_value);
}

extern "C" void time_UnixToHuman(time_t unix_time, int *pyear, int *pmonth, int *pday,
                                 int *phour, int *pminute, int *psecond)
{
    struct tm *tm_value = localtime(&unix_time);
    if(!tm_value) return;

    if(pyear) *pyear = tm_value->tm_year + 1900;
    if(pmonth) *pmonth = tm_value->tm_mon + 1;
    if(pday) *pday = tm_value->tm_mday;
    if(phour) *phour = tm_value->tm_hour;
    if(pminute) *pminute = tm_value->tm_min;
    if(psecond) *psecond = tm_value->tm_sec;
}

extern "C" int OnPlaybackVideo(void *context, char *userData, RECORD_FILE_NAME *fileName,
                               unsigned int nTimeOffset, int nEncodeType, char *data,
                               int size, int is_key)
{
    (void)context;
    (void)userData;
    (void)fileName;
    (void)nTimeOffset;
    (void)nEncodeType;
    (void)data;
    (void)size;
    (void)is_key;
    return 0;
}

extern "C" int OnPlaybackAudio(void *context, char *userData, RECORD_FILE_NAME *fileName,
                               unsigned int nTimeOffset, int nEncodeType, char *data,
                               int size, int is_key)
{
    (void)context;
    (void)userData;
    (void)fileName;
    (void)nTimeOffset;
    (void)nEncodeType;
    (void)data;
    (void)size;
    (void)is_key;
    return 0;
}

void SK_IRCAM_setBreathHigh(int value) { (void)value; }
void SK_IRCAM_setBreathLow(int value) { (void)value; }
void SK_IRCAM_setdBAlarmValue(int value) { (void)value; }
void SK_IRCAM_setCalibrationTemp(int value) { (void)value; }
void SK_IRCAM_setOffsetValue(int value) { (void)value; }

static void handle_signal(int sig)
{
    (void)sig;
    gs_exit = 1;
}

static uint64_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static uint64_t host_to_be64(uint64_t v)
{
    uint8_t out[8];
    out[0] = (uint8_t)(v >> 56);
    out[1] = (uint8_t)(v >> 48);
    out[2] = (uint8_t)(v >> 40);
    out[3] = (uint8_t)(v >> 32);
    out[4] = (uint8_t)(v >> 24);
    out[5] = (uint8_t)(v >> 16);
    out[6] = (uint8_t)(v >> 8);
    out[7] = (uint8_t)v;

    uint64_t ret;
    memcpy(&ret, out, sizeof(ret));
    return ret;
}

static int set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags < 0)
    {
        return -1;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static speed_t baud_to_speed(int baud)
{
    switch(baud)
    {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
#ifdef B2000000
    case 2000000: return B2000000;
#endif
    default: return B115200;
    }
}

static int open_uart(const char *dev_name, int baud)
{
    int fd = open(dev_name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(fd < 0)
    {
        printf("sk_main: open %s failed: %s\n", dev_name, strerror(errno));
        return -1;
    }

    struct termios tio;
    if(tcgetattr(fd, &tio) < 0)
    {
        printf("sk_main: tcgetattr %s failed: %s\n", dev_name, strerror(errno));
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, baud_to_speed(baud));
    cfsetospeed(&tio, baud_to_speed(baud));
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~CRTSCTS;
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if(tcsetattr(fd, TCSAFLUSH, &tio) < 0)
    {
        printf("sk_main: tcsetattr %s failed: %s\n", dev_name, strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

int SK_IRCAM_SendCommand(unsigned char *cmd, int len)
{
    if(gs_thermal_uart_fd < 0)
    {
        return -1;
    }

    return write(gs_thermal_uart_fd, cmd, len);
}

static int create_tcp_server(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    struct sockaddr_in addr;

    if(fd < 0)
    {
        return -1;
    }

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return -1;
    }

    if(listen(fd, 4) < 0)
    {
        close(fd);
        return -1;
    }

    set_nonblock(fd);
    return fd;
}

static int send_all(int fd, const unsigned char *buf, int len)
{
    int sent = 0;
    while(sent < len)
    {
        int ret = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if(ret <= 0)
        {
            if(ret < 0 && errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        sent += ret;
    }
    return 0;
}

static void send_thermal_frame(std::vector<int> &clients, const unsigned char *raw,
                               uint32_t frame_no, uint16_t fps)
{
    SKThermalFrameHeader_t header;
    memset(&header, 0, sizeof(header));
    header.magic = htonl(SK_THERMAL_MAGIC);
    header.version = htons(1);
    header.header_size = htons(sizeof(header));
    header.width = htons(SK_THERMAL_WIDTH);
    header.height = htons(SK_THERMAL_HEIGHT);
    header.pixel_format = htons(1);
    header.fps = htons(fps);
    header.frame_no = htonl(frame_no);
    header.timestamp_ms = host_to_be64(get_time_ms());
    header.payload_size = htonl(SK_THERMAL_RAW_SIZE);

    for(size_t i = 0; i < clients.size();)
    {
        int fd = clients[i];
        if(send_all(fd, (const unsigned char *)&header, sizeof(header)) < 0 ||
           send_all(fd, raw, SK_THERMAL_RAW_SIZE) < 0)
        {
            close(fd);
            clients.erase(clients.begin() + i);
            continue;
        }
        i++;
    }
}

static void accept_clients(int server_fd, std::vector<int> &clients)
{
    while(1)
    {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        int fd = accept(server_fd, (struct sockaddr *)&addr, &addr_len);
        if(fd < 0)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            {
                return;
            }
            printf("sk_main: thermal accept failed: %s\n", strerror(errno));
            return;
        }
        set_nonblock(fd);
        clients.push_back(fd);
    }
}

static void *thermal_thread(void *)
{
    int server_fd = create_tcp_server(SK_THERMAL_TCP_PORT);
    if(server_fd < 0)
    {
        printf("sk_main: thermal TCP port %d failed: %s\n", SK_THERMAL_TCP_PORT, strerror(errno));
        return NULL;
    }

    gs_thermal_uart_fd = open_uart(SK_THERMAL_UART_DEV, SK_THERMAL_UART_BAUD);
    if(gs_thermal_uart_fd < 0)
    {
        close(server_fd);
        return NULL;
    }

    cvGEN2Protocol_Initial();
    cvGEN2Protocol_SendCommand(IRCAM_CMD_SET_START_IMG);
    cvGEN2Protocol_SendCommand(IRCAM_CMD_SET_FPS_20);

    printf("sk_main: thermal TCP port %d, UART %s\n", SK_THERMAL_TCP_PORT, SK_THERMAL_UART_DEV);

    std::vector<int> clients;
    unsigned char rx_buf[SK_THERMAL_RX_SIZE];
    uint32_t frame_no = 0;

    while(gs_thermal_loop)
    {
        fd_set readfds;
        struct timeval timeout;
        int maxfd = server_fd > gs_thermal_uart_fd ? server_fd : gs_thermal_uart_fd;

        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        FD_SET(gs_thermal_uart_fd, &readfds);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ret = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if(ret < 0)
        {
            if(errno == EINTR) continue;
            break;
        }

        if(FD_ISSET(server_fd, &readfds))
        {
            accept_clients(server_fd, clients);
        }

        if(FD_ISSET(gs_thermal_uart_fd, &readfds))
        {
            int len = read(gs_thermal_uart_fd, rx_buf, sizeof(rx_buf));
            if(len > 0)
            {
                cvGEN2Protocol_Analysis(rx_buf, len);
                if(cvGEN2Protocol.FlagGetImageData)
                {
                    cvGEN2Protocol.FlagGetImageData = 0;
                    frame_no++;
                    send_thermal_frame(clients, cvGEN2Protocol.Data + 15, frame_no, cvGEN2Protocol.fps);
                }
            }
            else if(len < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            {
                break;
            }
        }
    }

    cvGEN2Protocol_SendCommand(IRCAM_CMD_SET_STOP_IMG);
    for(size_t i = 0; i < clients.size(); i++)
    {
        close(clients[i]);
    }
    close(gs_thermal_uart_fd);
    close(server_fd);
    gs_thermal_uart_fd = -1;
    return NULL;
}

static void *radar_thread(void *)
{
    int fd = open_uart(SK_RADAR_UART_DEV, SK_RADAR_UART_BAUD);
    if(fd < 0)
    {
        return NULL;
    }

    printf("sk_main: radar UART %s at %d baud\n", SK_RADAR_UART_DEV, SK_RADAR_UART_BAUD);

    unsigned char read_buf[SK_RADAR_BUF_SIZE];
    char line_buf[SK_RADAR_BUF_SIZE];
    int line_len = 0;

    while(gs_radar_loop)
    {
        fd_set readfds;
        struct timeval timeout;

        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
        if(ret < 0)
        {
            if(errno == EINTR) continue;
            break;
        }
        if(ret == 0) continue;

        int len = read(fd, read_buf, sizeof(read_buf));
        if(len < 0)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            break;
        }

        for(int i = 0; i < len; i++)
        {
            unsigned char ch = read_buf[i];
            if(ch == '\n' || ch == '\r')
            {
                if(line_len > 0)
                {
                    line_buf[line_len] = 0;
                    pthread_mutex_lock(&gs_state_mutex);
                    strncpy(gs_state.radar, line_buf, sizeof(gs_state.radar) - 1);
                    gs_state.radar[sizeof(gs_state.radar) - 1] = 0;
                    pthread_mutex_unlock(&gs_state_mutex);
                    line_len = 0;
                }
            }
            else if(line_len < (int)sizeof(line_buf) - 1)
            {
                line_buf[line_len++] = (char)ch;
            }
            else
            {
                line_len = 0;
            }
        }
    }

    close(fd);
    return NULL;
}

static void *sensor_thread(void *)
{
    while(gs_sensor_loop)
    {
        SK_HumidTempValue_t humid_temp;
        int db = SK_getAudioDbValue();
        int have_humid_temp = (SK_HUMID_TEMP_getValue(&humid_temp) == 0);

        pthread_mutex_lock(&gs_state_mutex);
        gs_state.mic_db = db;
        if(have_humid_temp)
        {
            gs_state.temperature = humid_temp.temp;
            gs_state.humidity = humid_temp.humidity;
        }
        pthread_mutex_unlock(&gs_state_mutex);

        sleep(1);
    }

    return NULL;
}

static void json_escape(char *dst, size_t dst_size, const char *src)
{
    size_t off = 0;
    if(dst_size == 0) return;

    for(size_t i = 0; src[i] && off + 1 < dst_size; i++)
    {
        unsigned char ch = (unsigned char)src[i];
        if((ch == '"' || ch == '\\') && off + 2 < dst_size)
        {
            dst[off++] = '\\';
            dst[off++] = (char)ch;
        }
        else if(ch >= 32)
        {
            dst[off++] = (char)ch;
        }
    }
    dst[off] = 0;
}

static void send_sensor_json(int client)
{
    SKMainSensorState_t state;
    char radar_json[256];
    char rtsp_name[128] = {0};
    char rtsp_path_json[256];
    char body[768];
    char header[256];
    int rtsp_port = 554;
    std::string rtsp_user;
    std::string rtsp_pwd;

    pthread_mutex_lock(&gs_state_mutex);
    state = gs_state;
    pthread_mutex_unlock(&gs_state_mutex);

    SK_INI_GetRtspInfo(rtsp_port, rtsp_user, rtsp_pwd);
    if(rtsp_port <= 0 || rtsp_port > 65535) rtsp_port = 554;
    strncpy(rtsp_name, "main", sizeof(rtsp_name) - 1);

    json_escape(radar_json, sizeof(radar_json), state.radar);
    json_escape(rtsp_path_json, sizeof(rtsp_path_json), rtsp_name);

    int body_len = snprintf(body, sizeof(body),
                            "{\"mic_db\":%d,\"temperature\":%.1f,\"humidity\":%.1f,"
                            "\"radar\":\"%s\",\"thermal_tcp_port\":%d,"
                            "\"rtsp\":\"enabled\",\"rtsp_port\":%d,\"rtsp_path\":\"/%s\"}\n",
                            state.mic_db, state.temperature, state.humidity,
                            radar_json, SK_THERMAL_TCP_PORT, rtsp_port, rtsp_path_json);

    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: %d\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              body_len);
    send(client, header, header_len, 0);
    send(client, body, body_len, 0);
}

static void send_not_found(int client)
{
    const char *body = "not found\n";
    char header[160];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 404 Not Found\r\n"
                              "Content-Type: text/plain\r\n"
                              "Content-Length: %d\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              (int)strlen(body));
    send(client, header, header_len, 0);
    send(client, body, strlen(body), 0);
}

static void handle_http_client(int server_fd)
{
    char req[1024];
    int client = accept(server_fd, NULL, NULL);
    if(client < 0) return;

    int n = recv(client, req, sizeof(req) - 1, 0);
    if(n <= 0)
    {
        close(client);
        return;
    }
    req[n] = 0;

    if(strncmp(req, "GET / ", 6) == 0 ||
       strncmp(req, "GET /sensors ", 13) == 0 ||
       strncmp(req, "GET /status ", 12) == 0 ||
       strncmp(req, "POST / ", 7) == 0 ||
       strncmp(req, "POST /sensors ", 14) == 0 ||
       strncmp(req, "POST /status ", 13) == 0)
    {
        send_sensor_json(client);
    }
    else
    {
        send_not_found(client);
    }

    close(client);
}

static void *http_thread(void *)
{
    int fd = create_tcp_server(SK_MAIN_HTTP_PORT);
    if(fd < 0)
    {
        printf("sk_main: HTTP port %d failed: %s\n", SK_MAIN_HTTP_PORT, strerror(errno));
        return NULL;
    }

    printf("sk_main: HTTP sensor server on port %d\n", SK_MAIN_HTTP_PORT);

    while(gs_http_loop)
    {
        fd_set readfds;
        struct timeval timeout;

        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
        if(ret < 0)
        {
            if(errno == EINTR) continue;
            break;
        }
        if(ret > 0 && FD_ISSET(fd, &readfds))
        {
            handle_http_client(fd);
        }
    }

    close(fd);
    return NULL;
}

static int start_threads(void)
{
    gs_sensor_loop = true;
    gs_http_loop = true;
    gs_radar_loop = true;
    gs_thermal_loop = true;

    if(pthread_create(&gs_sensor_tid, NULL, sensor_thread, NULL) != 0) return -1;
    if(pthread_create(&gs_http_tid, NULL, http_thread, NULL) != 0) return -1;
    if(pthread_create(&gs_radar_tid, NULL, radar_thread, NULL) != 0) return -1;
    if(pthread_create(&gs_thermal_tid, NULL, thermal_thread, NULL) != 0) return -1;
    return 0;
}

static void stop_threads(void)
{
    gs_sensor_loop = false;
    gs_http_loop = false;
    gs_radar_loop = false;
    gs_thermal_loop = false;

    pthread_join(gs_sensor_tid, NULL);
    pthread_join(gs_http_tid, NULL);
    pthread_join(gs_radar_tid, NULL);
    pthread_join(gs_thermal_tid, NULL);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    memset(&gs_state, 0, sizeof(gs_state));
    strcpy(gs_state.radar, "");

    printf("sk_main: starting RTSP video, thermal TCP, HTTP sensors, I2C temp/humidity, MIC dB, radar UART\n");

    SK_INI_loadConf();
    audio_encoder_init();

    if(SK_HUMID_TEMP_start() < 0)
    {
        printf("sk_main: I2C humidity/temp sensor failed to start\n");
    }

    if(SOC_Video_init() < 0)
    {
        printf("sk_main: SOC_Video_init failed\n");
        SK_HUMID_TEMP_stop();
        return 1;
    }

    if(SOC_Audio_Init() < 0)
    {
        printf("sk_main: SOC_Audio_Init failed\n");
    }

    init_all_server();
    SK_RTSP_Init();

    if(start_threads() < 0)
    {
        printf("sk_main: failed to start worker threads\n");
        gs_exit = 1;
    }

    while(!gs_exit)
    {
        sleep(1);
    }

    stop_threads();
    SK_RTSP_Deinit();
    deinit_all_server();
    SOC_Audio_exit();
    SOC_Video_deinit();
    SK_HUMID_TEMP_stop();
    audio_encoder_deinit();
    SOC_Exit_System();

    printf("sk_main: stopped\n");
    return 0;
}
