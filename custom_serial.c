#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <termios.h>
#include "serial.h"
#define SIZE 512

int main()
{
    int i, r, num;
    char buf[SIZE];

    // 1D6B:0104:FF77:0009
    r = rawserial_open(1, 0x1D6B, 0x0104, 0xFF77, 0x0009);
    if (r <= 0) {
        r = rawserial_open(1, 0x1D6B, 0x0104, 0xFF77, 0x0009);
        if (r <= 0) {
            printf("no device found\n");
            return -1;
        }
    }
    printf("found device\n");

    while (1) {
        // Raw Serial 패킷이 도착했는지 확인 
        num = rawserial_recv(0, buf, SIZE, 220);
        if (num < 0) {
            printf("\nerror reading, device went offline\n");
            rawserial_close(0);
            return 0;
        }
        if (num > 0) {
            printf("\nrecv %d bytes:\n", num);
            for (i = 0; i < num; i++) {
                printf("%02X ", buf[i] & 255);
                if (i % 16 == 15 && i < num - 1) printf("\n");
            }
            printf("\n");
            // 전달 받은 이후에 의미 없는 패킷 전송
            for (i = 0; i < SIZE; i++) {
                buf[i] = 'F';
            }
            rawserial_send(0, buf, SIZE, 100);
        }
    }
}
