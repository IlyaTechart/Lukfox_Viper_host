#define _POSIX_C_SOURCE 200809L 

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  // Для системных вызовов: read, write, close, sleep, lseek
#include <fcntl.h>   // Для системного вызова open и флагов (O_WRONLY, O_RDWR)
#include <string.h>
#include "gpio.h"
#include <libusb-1.0/libusb.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <stddef.h>
#include "frames_structure.h"

#define FOUND_DEVICE_VID 0x303A
#define FOUND_DEVICE_PID 0x1002 

#define USB_INTERFACE_NUM_1 1
#define USB_INTERFACE_NUM_0 0
#define USB_ADDRES_ENDPIONT 0x82
#define USB_LENGTH_DUMP 10000

// Интерфейсы CDC
#define CTRL_INTERFACE  0
#define DATA_INTERFACE  1
#define EP_DATA_IN      0x82

// Настройки CDC
#define CDC_SET_CONTROL_LINE_STATE 0x22
#define CDC_CONTROL_LINE_STATE_DTR (1 << 0)
#define CDC_CONTROL_LINE_STATE_RTS (1 << 1)


typedef enum{
    USB_OK,
    USB_FAIL,
    USB_ERR_NO_MEM,
    USB_ERR_INVALID_ARG,
    USB_FAIL_DESCRIPTOR,
    USB_INVALID_STATE,
    USB_ERR_INVALID_SIZE,
    USB_ERR_NOT_FOUND,
    USB_ERR_NOT_SUPPORTED,
    USB_ERR_TIMEOUT
}Users_USB_err_t;




ssize_t USB_Init(void);
uint8_t open_device(ssize_t cnt);
uint32_t read_usb_device(libusb_device_handle *handle);
uint8_t USB_DeInit(void);

/** Печать всех полей каждого кадра ModulData_t из буфера дампа (stdout). */
void logger_print_dump(DumpData_t *DumpData, size_t total_frames);