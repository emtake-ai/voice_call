#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>

#define DEFAULT_RADAR_UART "/dev/ttyS3"
#define DEFAULT_RADAR_BAUD 115200
#define RADAR_BUF_SIZE 256

static volatile sig_atomic_t gs_exit = 0;

static void handle_signal(int sig)
{
    (void)sig;
    gs_exit = 1;
}

static speed_t baud_to_speed(int baud)
{
    switch(baud)
    {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    default:
        return B115200;
    }
}

static int open_radar_uart(const char *dev_name, int baud)
{
    int fd = open(dev_name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(fd < 0)
    {
        printf("sk_radar: open %s failed: %s\n", dev_name, strerror(errno));
        return -1;
    }

    struct termios tio;
    if(tcgetattr(fd, &tio) < 0)
    {
        printf("sk_radar: tcgetattr failed: %s\n", strerror(errno));
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
        printf("sk_radar: tcsetattr failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

int main(int argc, char **argv)
{
    const char *dev_name = DEFAULT_RADAR_UART;
    int baud = DEFAULT_RADAR_BAUD;
    int max_lines = 0;

    if(argc > 1)
    {
        dev_name = argv[1];
    }

    if(argc > 2)
    {
        baud = atoi(argv[2]);
    }

    if(argc > 3)
    {
        max_lines = atoi(argv[3]);
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int fd = open_radar_uart(dev_name, baud);
    if(fd < 0)
    {
        return 1;
    }

    printf("sk_radar: reading %s at %d baud\n", dev_name, baud);

    unsigned char read_buf[RADAR_BUF_SIZE];
    char line_buf[RADAR_BUF_SIZE];
    int line_len = 0;
    int line_count = 0;

    while(!gs_exit)
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
            if(errno == EINTR)
            {
                continue;
            }

            printf("sk_radar: select failed: %s\n", strerror(errno));
            close(fd);
            return 2;
        }

        if(ret == 0)
        {
            continue;
        }

        int len = read(fd, read_buf, sizeof(read_buf));
        if(len < 0)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }

            printf("sk_radar: read failed: %s\n", strerror(errno));
            close(fd);
            return 3;
        }

        for(int i = 0; i < len; i++)
        {
            unsigned char ch = read_buf[i];

            if(ch == '\n' || ch == '\r')
            {
                if(line_len > 0)
                {
                    time_t now = time(NULL);
                    line_buf[line_len] = 0;
                    printf("radar=%ld,%s\n", now, line_buf);
                    fflush(stdout);

                    line_len = 0;
                    line_count++;

                    if(max_lines > 0 && line_count >= max_lines)
                    {
                        gs_exit = 1;
                        break;
                    }
                }
            }
            else if(line_len < (int)sizeof(line_buf) - 1)
            {
                line_buf[line_len++] = (char)ch;
            }
            else
            {
                line_len = 0;
                printf("sk_radar: frame too long, dropped\n");
            }
        }
    }

    close(fd);
    return 0;
}
