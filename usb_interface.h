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
#define NUMBER_SUPPORT_DEVICES  24

#define ID_AVE_FRAME_START   (const uint32_t)0x22446688
#define ID_DUMP_FRAME_START  (const uint32_t)0x336699FF

#define ID_TAIL_FRMES        (const uint32_t)0x55AA55AA

#define DUMP_CHUNK_SIZE     (64 * 1024)
#define USB_INTERFACE_NUM_1 1
#define USB_INTERFACE_NUM_0 0
#define USB_ADDRES_ENDPIONT 0x82
#define USB_LENGTH_DUMP 10000
#define TOTAL_DUMP_BYTES    (sizeof(ModulData_t) * USB_LENGTH_DUMP)
#define DUMP_TIMEOUT_MS     0    



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

typedef struct 
{
    libusb_device **list;
    libusb_context *ctx;
    libusb_device_handle *temp_handle[NUMBER_SUPPORT_DEVICES];
    struct libusb_device_descriptor desc[NUMBER_SUPPORT_DEVICES];
    uint8_t device_list_target[NUMBER_SUPPORT_DEVICES];          // Массив номеров найденых целевых устройств 
    ssize_t CountDevicesFound;                                   // Колличество целевых устройств
    uint8_t UsersVelue_Handels; 
}USB_UsersListHandle_t;


typedef enum {
    DEV_STATE_WAITING = 0,   // ждём старт-кадр
    DEV_STATE_RECEIVING,     // приём идёт
    DEV_STATE_DONE,          // дамп принят
    DEV_STATE_ERROR
} dev_state_t;

typedef struct {
    int                     index;              // номер устройства (0..23)
    libusb_device_handle   *handle;
    struct libusb_transfer *xfer;
    uint8_t                 buffer[TOTAL_DUMP_BYTES];
    FILE                   *file;
    char                    path[256];
    uint32_t                total_received;
    dev_state_t             state;
    int                     start_found;
    struct timespec         last_data_ts;
} dev_ctx_t;

static dev_ctx_t g_devs[NUMBER_SUPPORT_DEVICES];
static int       g_active_devices = 0;




int read_all_usb_devices(USB_UsersListHandle_t *usb_p, const char *base_dir);
ssize_t USB_Init(void);
uint8_t open_device(USB_UsersListHandle_t *usb_p);
int read_usb_device(libusb_device_handle *handle, char *pathDumpFile);
uint8_t USB_DeInit(USB_UsersListHandle_t *usb_p);

/** Печать всех полей каждого кадра ModulData_t из буфера дампа (stdout). */
void logger_print_dump(DumpData_t *DumpData, size_t total_frames);