#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "i2c/sk_humid_temp.h"

int main(int argc, char **argv)
{
    int loop_count = 1;
    int interval_sec = 1;

    if(argc > 1)
    {
        loop_count = atoi(argv[1]);
    }

    if(argc > 2)
    {
        interval_sec = atoi(argv[2]);
    }

    if(loop_count <= 0)
    {
        loop_count = 1;
    }

    if(interval_sec <= 0)
    {
        interval_sec = 1;
    }

    if(SK_HUMID_TEMP_start() < 0)
    {
        printf("sk_humid_temp: failed to start I2C humidity/temp sensor\n");
        return 1;
    }

    for(int i = 0; i < loop_count; i++)
    {
        SK_HumidTempValue_t value;

        if(SK_HUMID_TEMP_getValue(&value) < 0)
        {
            printf("sk_humid_temp: failed to read sensor\n");
            SK_HUMID_TEMP_stop();
            return 2;
        }

        printf("temp=%.1f humidity=%.1f\n", value.temp, value.humidity);

        if(i + 1 < loop_count)
        {
            sleep(interval_sec);
        }
    }

    SK_HUMID_TEMP_stop();
    return 0;
}
