#include "homey_service.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static const int HOMEY_HTTP_PORT = 8088;
static const int HOMEY_MDNS_PORT = 5353;
static const char *HOMEY_MDNS_GROUP = "224.0.0.251";
static const char *HOMEY_SERVICE_ENUM = "_services._dns-sd._udp.local";
static const char *HOMEY_SERVICE_TYPE = "_hanguo-ipc._tcp.local";
static const char *HOMEY_INSTANCE = "Hanguo IPC Homey._hanguo-ipc._tcp.local";
static const char *HOMEY_HOST_NAME = "hanguo-ipc.local";

static pthread_t gs_homey_tid;
static pthread_mutex_t gs_homey_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool gs_homey_loop = false;
static HOMEY_SENSOR_VALUE_S gs_homey_sensor;
static HOMEY_MEDIA_INFO_S gs_homey_media;

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void write_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

static void write_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xff);
    p[2] = (uint8_t)((v >> 8) & 0xff);
    p[3] = (uint8_t)(v & 0xff);
}

static int append_bytes(uint8_t *buf, size_t cap, size_t *off, const void *data, size_t len)
{
    if (*off + len > cap) return -1;
    memcpy(buf + *off, data, len);
    *off += len;
    return 0;
}

static int append_u16(uint8_t *buf, size_t cap, size_t *off, uint16_t v)
{
    uint8_t tmp[2];
    write_be16(tmp, v);
    return append_bytes(buf, cap, off, tmp, sizeof(tmp));
}

static int append_u32(uint8_t *buf, size_t cap, size_t *off, uint32_t v)
{
    uint8_t tmp[4];
    write_be32(tmp, v);
    return append_bytes(buf, cap, off, tmp, sizeof(tmp));
}

static int append_name(uint8_t *buf, size_t cap, size_t *off, const char *name)
{
    const char *p = name;

    while (*p) {
        const char *dot = strchr(p, '.');
        size_t len = dot ? (size_t)(dot - p) : strlen(p);
        if (len > 63 || *off + 1 + len > cap) return -1;
        buf[(*off)++] = (uint8_t)len;
        memcpy(buf + *off, p, len);
        *off += len;
        if (!dot) break;
        p = dot + 1;
    }

    if (*off + 1 > cap) return -1;
    buf[(*off)++] = 0;
    return 0;
}

static int append_txt(uint8_t *buf, size_t cap, size_t *off, const char *txt)
{
    size_t len = strlen(txt);
    if (len > 255 || *off + 1 + len > cap) return -1;
    buf[(*off)++] = (uint8_t)len;
    memcpy(buf + *off, txt, len);
    *off += len;
    return 0;
}

static int append_record_header(uint8_t *buf, size_t cap, size_t *off,
                                const char *name, uint16_t type, uint16_t klass,
                                uint32_t ttl, size_t *rdlen_pos)
{
    if (append_name(buf, cap, off, name) < 0) return -1;
    if (append_u16(buf, cap, off, type) < 0) return -1;
    if (append_u16(buf, cap, off, klass) < 0) return -1;
    if (append_u32(buf, cap, off, ttl) < 0) return -1;
    *rdlen_pos = *off;
    return append_u16(buf, cap, off, 0);
}

static void finish_record(uint8_t *buf, size_t rdlen_pos, size_t rdata_start, size_t off)
{
    write_be16(buf + rdlen_pos, (uint16_t)(off - rdata_start));
}

static uint32_t get_local_ip(void)
{
    struct ifaddrs *ifaddr = NULL;
    uint32_t best = inet_addr("127.0.0.1");

    if (getifaddrs(&ifaddr) != 0) return best;

    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK)) continue;

        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        best = addr->sin_addr.s_addr;
        break;
    }

    freeifaddrs(ifaddr);
    return best;
}

static int build_mdns_response(uint8_t *buf, size_t cap, uint16_t id, uint32_t ip)
{
    size_t off = 12;
    size_t rdlen_pos = 0;
    size_t rdata_start = 0;
    HOMEY_MEDIA_INFO_S media;
    char txt[160];

    pthread_mutex_lock(&gs_homey_mutex);
    media = gs_homey_media;
    pthread_mutex_unlock(&gs_homey_mutex);

    memset(buf, 0, cap);
    if (cap < 12) return -1;

    write_be16(buf + 0, id);
    write_be16(buf + 2, 0x8400);
    write_be16(buf + 4, 0);
    write_be16(buf + 6, 5);
    write_be16(buf + 8, 0);
    write_be16(buf + 10, 0);

    if (append_record_header(buf, cap, &off, HOMEY_SERVICE_ENUM, 12, 0x0001, 120, &rdlen_pos) < 0) return -1;
    rdata_start = off;
    if (append_name(buf, cap, &off, HOMEY_SERVICE_TYPE) < 0) return -1;
    finish_record(buf, rdlen_pos, rdata_start, off);

    if (append_record_header(buf, cap, &off, HOMEY_SERVICE_TYPE, 12, 0x0001, 120, &rdlen_pos) < 0) return -1;
    rdata_start = off;
    if (append_name(buf, cap, &off, HOMEY_INSTANCE) < 0) return -1;
    finish_record(buf, rdlen_pos, rdata_start, off);

    if (append_record_header(buf, cap, &off, HOMEY_INSTANCE, 33, 0x8001, 120, &rdlen_pos) < 0) return -1;
    rdata_start = off;
    if (append_u16(buf, cap, &off, 0) < 0) return -1;
    if (append_u16(buf, cap, &off, 0) < 0) return -1;
    if (append_u16(buf, cap, &off, (uint16_t)HOMEY_HTTP_PORT) < 0) return -1;
    if (append_name(buf, cap, &off, HOMEY_HOST_NAME) < 0) return -1;
    finish_record(buf, rdlen_pos, rdata_start, off);

    if (append_record_header(buf, cap, &off, HOMEY_INSTANCE, 16, 0x8001, 120, &rdlen_pos) < 0) return -1;
    rdata_start = off;
    if (append_txt(buf, cap, &off, "id=hanguo-ipc") < 0) return -1;
    if (append_txt(buf, cap, &off, "md=hanguo-camera") < 0) return -1;
    if (append_txt(buf, cap, &off, "path=/homey/sensors") < 0) return -1;
    snprintf(txt, sizeof(txt), "rtsp=rtsp://%s:%d/%s", inet_ntoa(*(struct in_addr *)&ip),
             media.rtsp_port > 0 ? media.rtsp_port : 554, media.rtsp_path);
    if (append_txt(buf, cap, &off, txt) < 0) return -1;
    finish_record(buf, rdlen_pos, rdata_start, off);

    if (append_record_header(buf, cap, &off, HOMEY_HOST_NAME, 1, 0x8001, 120, &rdlen_pos) < 0) return -1;
    rdata_start = off;
    if (append_bytes(buf, cap, &off, &ip, sizeof(ip)) < 0) return -1;
    finish_record(buf, rdlen_pos, rdata_start, off);

    return (int)off;
}

static int mdns_query_matches(const uint8_t *buf, size_t len)
{
    char query[256] = {0};
    size_t off = 12;

    if (len < 12) return 0;

    while (off < len && buf[off] != 0) {
        uint8_t part_len = buf[off++];
        if ((part_len & 0xc0) != 0 || part_len > 63 || off + part_len > len) return 0;
        size_t qlen = strlen(query);
        if (qlen + part_len + 2 >= sizeof(query)) return 0;
        if (qlen > 0) query[qlen++] = '.';
        memcpy(query + qlen, buf + off, part_len);
        query[qlen + part_len] = 0;
        off += part_len;
    }

    return strstr(query, HOMEY_SERVICE_ENUM) != NULL ||
           strstr(query, HOMEY_SERVICE_TYPE) != NULL ||
           strstr(query, HOMEY_HOST_NAME) != NULL;
}

static int create_mdns_socket(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int yes = 1;
    struct sockaddr_in addr;
    struct ip_mreq mreq;

    if (fd < 0) return -1;

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HOMEY_MDNS_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(HOMEY_MDNS_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &yes, sizeof(yes));
    return fd;
}

static int create_http_socket(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    struct sockaddr_in addr;

    if (fd < 0) return -1;

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HOMEY_HTTP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 4) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void send_json_response(int client)
{
    HOMEY_SENSOR_VALUE_S sensor;
    HOMEY_MEDIA_INFO_S media;
    char body[768];
    char header[256];

    pthread_mutex_lock(&gs_homey_mutex);
    sensor = gs_homey_sensor;
    media = gs_homey_media;
    pthread_mutex_unlock(&gs_homey_mutex);

    int body_len = snprintf(body, sizeof(body),
                            "{\"temperature\":%.1f,\"humidity\":%.1f,\"volume\":%d,"
                            "\"light\":%d,\"thermaltemp\":%.1f,\"rtsp_port\":%d,"
                            "\"rtsp_path\":\"/%s\",\"video_width\":%d,\"video_height\":%d,"
                            "\"video_fps\":%d,\"audio_freq\":%d,\"audio_channels\":%d,"
                            "\"audio_bits\":%d,\"thermal_frame\":\"80x60\",\"radar\":\"uart\"}\n",
                            sensor.temperature, sensor.humidity, sensor.volume,
                            sensor.light, sensor.thermal_temperature,
                            media.rtsp_port, media.rtsp_path, media.width,
                            media.height, media.fps, media.audio_freq,
                            media.audio_channels, media.audio_bits);

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

static void handle_http_client(int http_fd)
{
    char req[1024];
    int client = accept(http_fd, NULL, NULL);
    if (client < 0) return;

    int n = recv(client, req, sizeof(req) - 1, 0);
    if (n <= 0) {
        close(client);
        return;
    }
    req[n] = 0;

    if (strncmp(req, "GET /homey/sensors ", 19) == 0 ||
        strncmp(req, "GET /homey/info ", 16) == 0 ||
        strncmp(req, "GET / ", 6) == 0) {
        send_json_response(client);
    } else {
        send_not_found(client);
    }

    close(client);
}

static void send_mdns_announcement(int mdns_fd, uint32_t local_ip)
{
    uint8_t response[1200];
    int len = build_mdns_response(response, sizeof(response), 0, local_ip);
    if (len <= 0) return;

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(HOMEY_MDNS_PORT);
    dst.sin_addr.s_addr = inet_addr(HOMEY_MDNS_GROUP);

    sendto(mdns_fd, response, len, 0, (struct sockaddr *)&dst, sizeof(dst));
}

static void handle_mdns_query(int mdns_fd, uint32_t local_ip)
{
    uint8_t query[1500];
    uint8_t response[1200];
    struct sockaddr_in src;
    socklen_t src_len = sizeof(src);

    int n = recvfrom(mdns_fd, query, sizeof(query), 0, (struct sockaddr *)&src, &src_len);
    if (n <= 0 || !mdns_query_matches(query, (size_t)n)) return;

    uint16_t id = read_be16(query);
    int len = build_mdns_response(response, sizeof(response), id, local_ip);
    if (len <= 0) return;

    src.sin_port = htons(HOMEY_MDNS_PORT);
    sendto(mdns_fd, response, len, 0, (struct sockaddr *)&src, src_len);
}

static void *homey_service_thread(void *)
{
    int http_fd = create_http_socket();
    if (http_fd < 0) {
        printf("homey service: HTTP socket failed: %s\n", strerror(errno));
        gs_homey_loop = false;
        return NULL;
    }

    int mdns_fd = create_mdns_socket();
    if (mdns_fd < 0) {
        printf("homey service: mDNS socket failed: %s\n", strerror(errno));
        close(http_fd);
        gs_homey_loop = false;
        return NULL;
    }

    uint32_t local_ip = get_local_ip();
    printf("homey service: HTTP port %d, mDNS-SD service %s\n", HOMEY_HTTP_PORT, HOMEY_SERVICE_TYPE);
    send_mdns_announcement(mdns_fd, local_ip);

    while (gs_homey_loop) {
        fd_set fds;
        struct timeval tv;
        int maxfd = http_fd > mdns_fd ? http_fd : mdns_fd;

        FD_ZERO(&fds);
        FD_SET(http_fd, &fds);
        FD_SET(mdns_fd, &fds);

        tv.tv_sec = 5;
        tv.tv_usec = 0;

        int ret = select(maxfd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (ret == 0) {
            local_ip = get_local_ip();
            send_mdns_announcement(mdns_fd, local_ip);
            continue;
        }

        if (FD_ISSET(mdns_fd, &fds)) {
            local_ip = get_local_ip();
            handle_mdns_query(mdns_fd, local_ip);
        }

        if (FD_ISSET(http_fd, &fds)) {
            handle_http_client(http_fd);
        }
    }

    close(mdns_fd);
    close(http_fd);
    return NULL;
}

int SK_HOMEY_startService(void)
{
    memset(&gs_homey_sensor, 0, sizeof(gs_homey_sensor));
    memset(&gs_homey_media, 0, sizeof(gs_homey_media));
    gs_homey_media.rtsp_port = 554;
    strcpy(gs_homey_media.rtsp_path, "live0");
    gs_homey_media.audio_bits = 16;

    if (gs_homey_loop) return 0;
    gs_homey_loop = true;
    return pthread_create(&gs_homey_tid, NULL, homey_service_thread, NULL);
}

void SK_HOMEY_stopService(void)
{
    if (gs_homey_loop) {
        gs_homey_loop = false;
        pthread_join(gs_homey_tid, NULL);
    }
}

void SK_HOMEY_updateSensorValue(const HOMEY_SENSOR_VALUE_S *value)
{
    if (!value) return;

    pthread_mutex_lock(&gs_homey_mutex);
    gs_homey_sensor = *value;
    pthread_mutex_unlock(&gs_homey_mutex);
}

void SK_HOMEY_updateMediaInfo(const HOMEY_MEDIA_INFO_S *info)
{
    if (!info) return;

    pthread_mutex_lock(&gs_homey_mutex);
    gs_homey_media = *info;
    gs_homey_media.rtsp_path[sizeof(gs_homey_media.rtsp_path) - 1] = 0;
    pthread_mutex_unlock(&gs_homey_mutex);
}
