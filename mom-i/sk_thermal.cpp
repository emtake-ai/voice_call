#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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

#include "GEN2Protocol.h"

#define THERMAL_UART_DEV "/dev/ttyUSB1"
#define THERMAL_UART_BAUD 2000000
#define THERMAL_TCP_PORT 8554
#define THERMAL_WIDTH 80
#define THERMAL_HEIGHT 60
#define THERMAL_RAW_SIZE (THERMAL_WIDTH * THERMAL_HEIGHT * 2)
#define THERMAL_RX_SIZE (20 * 1024)
#define SK_THERMAL_MAGIC 0x534B5448u /* SKTH */

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

static volatile sig_atomic_t gs_exit = 0;
static int gs_uart_fd = -1;

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
    case 115200:
        return B115200;
#ifdef B2000000
    case 2000000:
        return B2000000;
#endif
    default:
        return B115200;
    }
}

static int open_uart(const char *dev_name, int baud)
{
    int fd = open(dev_name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(fd < 0)
    {
        printf("sk_thermal: open %s failed: %s\n", dev_name, strerror(errno));
        return -1;
    }

    struct termios tio;
    if(tcgetattr(fd, &tio) < 0)
    {
        printf("sk_thermal: tcgetattr failed: %s\n", strerror(errno));
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
        printf("sk_thermal: tcsetattr failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

int SK_IRCAM_SendCommand(unsigned char *cmd, int len)
{
    if(gs_uart_fd < 0)
    {
        return -1;
    }

    return write(gs_uart_fd, cmd, len);
}

static int create_tcp_server(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
    {
        printf("sk_thermal: socket failed: %s\n", strerror(errno));
        return -1;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        printf("sk_thermal: bind port %d failed: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }

    if(listen(fd, 4) < 0)
    {
        printf("sk_thermal: listen failed: %s\n", strerror(errno));
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
        if(ret < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if(ret == 0)
        {
            return -1;
        }

        sent += ret;
    }

    return 0;
}

static void send_frame_to_clients(std::vector<int> &clients, const unsigned char *raw, uint32_t frame_no, uint16_t fps)
{
    SKThermalFrameHeader_t header;
    memset(&header, 0, sizeof(header));
    header.magic = htonl(SK_THERMAL_MAGIC);
    header.version = htons(1);
    header.header_size = htons(sizeof(header));
    header.width = htons(THERMAL_WIDTH);
    header.height = htons(THERMAL_HEIGHT);
    header.pixel_format = htons(1); /* 1: big-endian uint16 thermal pixel */
    header.fps = htons(fps);
    header.frame_no = htonl(frame_no);
    header.timestamp_ms = htobe64(get_time_ms());
    header.payload_size = htonl(THERMAL_RAW_SIZE);

    for(size_t i = 0; i < clients.size();)
    {
        int fd = clients[i];
        if(send_all(fd, (const unsigned char *)&header, sizeof(header)) < 0 ||
           send_all(fd, raw, THERMAL_RAW_SIZE) < 0)
        {
            printf("sk_thermal: client disconnected\n");
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

            printf("sk_thermal: accept failed: %s\n", strerror(errno));
            return;
        }

        printf("sk_thermal: client connected %s:%d\n",
               inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
        clients.push_back(fd);
    }
}

int main(int argc, char **argv)
{
    const char *uart_dev = THERMAL_UART_DEV;
    int tcp_port = THERMAL_TCP_PORT;
    int fps_cmd = IRCAM_CMD_SET_FPS_20;

    if(argc > 1)
    {
        tcp_port = atoi(argv[1]);
    }

    if(argc > 2)
    {
        uart_dev = argv[2];
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int server_fd = create_tcp_server(tcp_port);
    if(server_fd < 0)
    {
        return 1;
    }

    gs_uart_fd = open_uart(uart_dev, THERMAL_UART_BAUD);
    if(gs_uart_fd < 0)
    {
        close(server_fd);
        return 2;
    }

    cvGEN2Protocol_Initial();
    cvGEN2Protocol_SendCommand(IRCAM_CMD_SET_START_IMG);
    cvGEN2Protocol_SendCommand(fps_cmd);

    printf("sk_thermal: TCP raw thermal server on port %d\n", tcp_port);
    printf("sk_thermal: reading %s at %d baud, frame %dx%d u16 raw\n",
           uart_dev, THERMAL_UART_BAUD, THERMAL_WIDTH, THERMAL_HEIGHT);

    std::vector<int> clients;
    unsigned char rx_buf[THERMAL_RX_SIZE];
    uint32_t frame_no = 0;

    while(!gs_exit)
    {
        fd_set readfds;
        struct timeval timeout;
        int maxfd = server_fd;

        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        FD_SET(gs_uart_fd, &readfds);
        if(gs_uart_fd > maxfd)
        {
            maxfd = gs_uart_fd;
        }

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ret = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if(ret < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }

            printf("sk_thermal: select failed: %s\n", strerror(errno));
            break;
        }

        if(FD_ISSET(server_fd, &readfds))
        {
            accept_clients(server_fd, clients);
        }

        if(FD_ISSET(gs_uart_fd, &readfds))
        {
            int len = read(gs_uart_fd, rx_buf, sizeof(rx_buf));
            if(len > 0)
            {
                cvGEN2Protocol_Analysis(rx_buf, len);
                if(cvGEN2Protocol.FlagGetImageData)
                {
                    cvGEN2Protocol.FlagGetImageData = 0;
                    frame_no++;

                    send_frame_to_clients(clients, cvGEN2Protocol.Data + 15, frame_no, cvGEN2Protocol.fps);

                    if((frame_no % 20) == 1)
                    {
                        printf("sk_thermal: frame=%u clients=%zu fps=%d len=%d\n",
                               frame_no, clients.size(), cvGEN2Protocol.fps, cvGEN2Protocol.Length);
                    }
                }
            }
            else if(len < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            {
                printf("sk_thermal: uart read failed: %s\n", strerror(errno));
                break;
            }
        }
    }

    cvGEN2Protocol_SendCommand(IRCAM_CMD_SET_STOP_IMG);

    for(size_t i = 0; i < clients.size(); i++)
    {
        close(clients[i]);
    }

    close(gs_uart_fd);
    close(server_fd);
    return 0;
}
