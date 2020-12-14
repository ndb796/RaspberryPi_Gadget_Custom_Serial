#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <usb.h>
#include "serial.h"

// 열린 모든 Serial 기기를 관리하기 위한 연결 리스트
typedef struct serial_struct serial_t;
static serial_t *first_serial = NULL;
static serial_t *last_serial = NULL;
struct serial_struct {
    usb_dev_handle *usb;
    int open;
    int iface;
    int ep_in;
    int ep_out;
    struct serial_struct *prev;
    struct serial_struct *next;
};

// 본 파일 안에서만 사용되는 private function들
static void add_serial(serial_t *h);
static serial_t * get_serial(int num);
static void free_all_serial(void);
static void serial_close(serial_t *serial);

// rawserial_recv(): USB 패킷 읽기 함수
// 입력: USB 기기 번호, 버퍼, 버퍼 크기, 타임아웃
// 출력: 전달 받은 패킷 크기 (오류: -1)
int rawserial_recv(int num, void *buf, int len, int timeout)
{
    serial_t *serial;
    int r;

    serial = get_serial(num);
    if (!serial || !serial->open) return -1;
    
    // 리눅스 usb.h 헤더에 구현되어 있는 usb_bulk_read() 함수 사용
    r = usb_bulk_read(serial->usb, serial->ep_in, buf, len, timeout);
    if (r >= 0) return r;
    if (r == -110) return 0; // timeout
    return -1;
}

// rawserial_send(): USB 패킷 전송 함수
// 입력: USB 기기 번호, 버퍼, 버퍼 크기, 타임아웃
// 출력: 전송한 패킷 크기 (오류: -1)
int rawserial_send(int num, void *buf, int len, int timeout)
{
    serial_t *serial;

    serial = get_serial(num);
    if (!serial || !serial->open) return -1;

    if (serial->ep_out) {
        // 리눅스 usb.h 헤더에 구현되어 있는 usb_bulk_write() 함수 사용
        return usb_bulk_write(serial->usb, serial->ep_out, buf, len, timeout);
    } else {
        return usb_control_msg(serial->usb, 0x21, 9, 0, serial->iface, buf, len, timeout);
    }
}

// rawserial_open(): Serial 기기 여는 함수
// 입력: 최대 기기 개수, Vendor ID (아무거나: -1), Product ID (아무거나: -1), Usage Page (아무거나: -1), Usage (아무거나: -1)
// 출력: 열린 Serial 기기 개수
int rawserial_open(int max, int vid, int pid, int usage_page, int usage)
{
    struct usb_bus *bus;
    struct usb_device *dev;
    struct usb_interface *iface;
    struct usb_interface_descriptor *desc;
    struct usb_endpoint_descriptor *ep;

    usb_dev_handle *u;
    uint8_t buf[1024];
    int i, n, ep_in, ep_out, count = 0, claimed;
    serial_t *serial;

    if (first_serial) free_all_serial();
    printf("rawserial_open, max = %d\n", max);
    if (max < 1) return 0;
    usb_init();
    usb_find_busses();
    usb_find_devices();

    for (bus = usb_get_busses(); bus; bus = bus->next) {
        // 연결되어 있는 모든 USB 장치를 하나씩 확인하며
        for (dev = bus->devices; dev; dev = dev->next) {
            // 설정한 Vendor ID와 Product ID를 가지는 장치를 찾기
            if (vid > 0 && dev->descriptor.idVendor != vid) continue;
            if (pid > 0 && dev->descriptor.idProduct != pid) continue;
            if (!dev->config) continue;
            if (dev->config->bNumInterfaces < 1) continue;
            printf("device: vid=%04X, pic=%04X, with %d iface\n",
                dev->descriptor.idVendor,
                dev->descriptor.idProduct,
                dev->config->bNumInterfaces);
            iface = dev->config->interface;
            u = NULL;
            claimed = 0;
            // 현재 기기의 모든 인터페이스(interface)를 확인하며
            for (i = 0; i < dev->config->bNumInterfaces && iface; i++, iface++) {
                desc = iface->altsetting;
                if (!desc) continue;
                printf("interface #%d: class = %d, subclass = %d, protocol = %d\n",
                    i,
                    desc->bInterfaceClass,
                    desc->bInterfaceSubClass,
                    desc->bInterfaceProtocol);
                // 미리 정의한 Serial descriptor와 일치하는 경우에만 처리
                if (desc->bInterfaceClass != 255) continue;
                if (desc->bInterfaceSubClass != 0) continue;
                if (desc->bInterfaceProtocol != 0) continue;

                ep = desc->endpoint;
                ep_in = ep_out = 0;
                // 해당 Serial 인터페이스의 모든 엔드포인트(endpoint)를 확인하며
                for (n = 0; n < desc->bNumEndpoints; n++, ep++) {
                    if (ep->bEndpointAddress & 0x80) {
                        if (!ep_in) ep_in = ep->bEndpointAddress;
                        printf("IN endpoint number: %d\n", ep_in);
                    } else {
                        if (!ep_out) ep_out = ep->bEndpointAddress;
                        printf("OUT endpoint: %d\n", ep_out);
                    }
                }
                if (!ep_in) continue;
                
                // 리눅스 usb.h 헤더에 구현되어 있는 usb_open() 함수 사용 
                if (!u) {
                    u = usb_open(dev);
                    if (!u) {
                        printf("unable to open device\n");
                        break;
                    }
                }
                printf("serial interface opened\n");
                
                // 사용할 수 있는지 확인 (다른 드라이버가 사용 중이라면 detach 시도)
                if (usb_get_driver_np(u, i, (char *)buf, sizeof(buf)) >= 0) {
                    printf("in use by driver \"%s\"\n", buf);
                    if (usb_detach_kernel_driver_np(u, i) < 0) {
                        printf("unable to detach from kernel\n");
                        continue;
                    }
                }
                if (usb_claim_interface(u, i) < 0) {
                    printf("unable claim interface %d\n", i);
                    continue;
                }

                // Serial 객체 초기화
                serial = (struct serial_struct *)malloc(sizeof(struct serial_struct));
                if (!serial) {
                    usb_release_interface(u, i);
                    continue;
                }
                serial->usb = u;
                serial->iface = i;
                serial->ep_in = ep_in;
                serial->ep_out = ep_out;
                serial->open = 1;
                add_serial(serial);
                claimed++;
                count++;
                if (count >= max) return count;
            }
            if (u && !claimed) usb_close(u);
        }
    }
    return count;
}


// rawserial_close(): Serial 기기 함수
// 입력: USB 기기 번호
// 출력: 없음
void rawserial_close(int num)
{
    serial_t *serial;

    serial = get_serial(num);
    if (!serial || !serial->open) return;
    serial_close(serial);
}

// add_serial(): 연결 리스트에 하나의 Serial 객체 추가
// 입력: Serial 객체
// 출력: 없음
static void add_serial(serial_t *h)
{
    if (!first_serial || !last_serial) {
        first_serial = last_serial = h;
        h->next = h->prev = NULL;
        return;
    }
    last_serial->next = h;
    h->prev = last_serial;
    h->next = NULL;
    last_serial = h;
}

// get_serial(): 번호로 Serial 객체 가져오기
// 입력: Serial 객체 번호
// 출력: Serial 객체
static serial_t* get_serial(int num)
{
    serial_t *p;
    for (p = first_serial; p && num > 0; p = p->next, num--);
    return p;
}

// free_all_serial(): 모든 Serial 객체 할당 해제
// 입력: 없음
// 출력: 없음
static void free_all_serial(void)
{
    serial_t *p, *q;

    for (p = first_serial; p; p = p->next) {
        serial_close(p);
    }
    p = first_serial;
    while (p) {
        q = p;
        p = p->next;
        free(q);
    }
    first_serial = last_serial = NULL;
}

// serial_close(): 특정 Serial 객체 할당 해제
// 입력: Serial 객체
// 출력: 없음
static void serial_close(serial_t *serial)
{
    serial_t *p;
    int others=0;

    usb_release_interface(serial->usb, serial->iface);
    for (p = first_serial; p; p = p->next) {
        if (p->open && p->usb == serial->usb) others++;
    }
    if (!others) usb_close(serial->usb);
    serial->usb = NULL;
}
