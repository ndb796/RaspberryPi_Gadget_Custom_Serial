#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <termios.h>
#include "serial.h"
#define SIZE 512

// 버퍼의 특정 인덱스부터 uint32_t형 데이터 쓰기
void val32_to_buf(char* buf, uint32_t val, uint32_t index) {
    buf[index] = val;
    buf[index + 1] = val >> 8;
    buf[index + 2] = val >> 16;
    buf[index + 3] = val >> 24;
}

int main()
{
    int r;
    char buf[SIZE];

    // 1D6B:0104:FF77:0009
    r = rawhid_open(1, 0x1D6B, 0x0104, 0xFF77, 0x0009);
    if (r <= 0) {
        r = rawhid_open(1, 0x1D6B, 0x0104, 0xFF77, 0x0009);
        if (r <= 0) {
            printf("no rawhid device found\n");
            return -1;
        }
    }
    printf("found rawhid device\n");

    /* 여기서부터 테스트용 데이터 전송 */
    // (File transfer start)
    // File sending OPCODE
    uint32_t opcode = 0xAAFFAAFF;
    val32_to_buf(buf, opcode, 0);

    // File size: 258 bytes
    uint32_t fileSize = 258;
    val32_to_buf(buf, fileSize, 4);
    rawhid_send(0, buf, 64, 100);

    // 'a' 64 bytes
    for (int i = 0; i < 64; i++) {
        buf[i] = 'a';
    }
    rawhid_send(0, buf, SIZE, 100);
    // 'b' 64 bytes
    for (int i = 0; i < 64; i++) {
        buf[i] = 'b';
    }
    rawhid_send(0, buf, SIZE, 100);
    // 'c' 64 bytes
    for (int i = 0; i < 64; i++) {
        buf[i] = 'c';
    }
    rawhid_send(0, buf, SIZE, 100);
    // 'd' 64 bytes
    for (int i = 0; i < 64; i++) {
        buf[i] = 'd';
    }
    rawhid_send(0, buf, SIZE, 100);
    // 'a' and 'b' 2 bytes
    buf[0] = 'a';
    buf[1] = 'b';
    rawhid_send(0, buf, SIZE, 100);
}
