
#include "usb_interface.h"

libusb_device **list = NULL;
libusb_context *ctx = NULL;
libusb_device *found = NULL;
libusb_device_handle *handle = NULL; 

extern sig_atomic_t keep_running;
extern DumpData_t DumpData;

/*
 * Функция ввывадящая информацию об USB устройстве 
*/
static Users_USB_err_t print_device_summary(int index, libusb_device *device)
{
    struct libusb_device_descriptor desc;
    int r = libusb_get_device_descriptor(device, &desc);
    if (r != LIBUSB_SUCCESS) {
        fprintf(stderr, "[%d] Ошибка дескриптора: %s\n", index, libusb_strerror(r));
        return USB_FAIL_DESCRIPTOR;
    }
    if(desc.idVendor != FOUND_DEVICE_VID || desc.idProduct != FOUND_DEVICE_PID)
    {
        return USB_FAIL;
    }
    printf("--- Устройство #%d ---\n", index);
    printf("  VID:PID     %04x:%04x\n", desc.idVendor, desc.idProduct);
    printf("  USB         %x.%02x\n", desc.bcdUSB >> 8, desc.bcdUSB & 0xff);
    printf("  Class       %u  Sub %u  Proto %u\n",
           desc.bDeviceClass, desc.bDeviceSubClass, desc.bDeviceProtocol);
    printf("  iManufacturer / iProduct / iSerial индексы: %u / %u / %u\n",
           desc.iManufacturer, desc.iProduct, desc.iSerialNumber);
    printf("\n");

    // Буферы для хранения полученных строк
    char manufacturer[256] = "(нет данных)";
    char product[256]      = "(нет данных)";
    char serial[256]       = "(нет данных)";

    libusb_device_handle *temp_handle = NULL;
    r = libusb_open(device, &temp_handle);

    if (r == LIBUSB_SUCCESS && temp_handle != NULL) {
        // Если индекс не 0, значит строка существует, пытаемся её прочитать
        if (desc.iManufacturer) {
            libusb_get_string_descriptor_ascii(temp_handle, desc.iManufacturer, 
                                              (unsigned char*)manufacturer, sizeof(manufacturer));
        }
        if (desc.iProduct) {
            libusb_get_string_descriptor_ascii(temp_handle, desc.iProduct, 
                                              (unsigned char*)product, sizeof(product));
        }
        if (desc.iSerialNumber) {
            libusb_get_string_descriptor_ascii(temp_handle, desc.iSerialNumber, 
                                              (unsigned char*)serial, sizeof(serial));
        }
        
        // Закрываем устройство, так как открывали только для чтения строк
        libusb_close(temp_handle);
    } else {
        // Если открыть не удалось (часто из-за отсутствия прав sudo / udev), 
        // просто выводим индексы как было
        snprintf(manufacturer, sizeof(manufacturer), "[Нет прав доступа, индекс: %u]", desc.iManufacturer);
        snprintf(product, sizeof(product), "[Нет прав доступа, индекс: %u]", desc.iProduct);
        snprintf(serial, sizeof(serial), "[Нет прав доступа, индекс: %u]", desc.iSerialNumber);
    }

    printf("  Производитель : %s\n", manufacturer);
    printf("  Продукт       : %s\n", product);
    printf("  Серийный №    : %s\n", serial);
    printf("\n");

    return USB_OK;

}

ssize_t USB_Init(void){


    int err = 0;
    struct libusb_init_option options[] = {
        {.option = LIBUSB_OPTION_LOG_LEVEL, .value = {.ival = LIBUSB_LOG_LEVEL_INFO} } 
    };
    int num_options = sizeof(options) / sizeof(options[0]);
    /*
     * Инициализация контекста lubusb
    */
    int ret = libusb_init(&ctx);
    if(ret != LIBUSB_SUCCESS ) {
        fprintf(stderr,"Ошибка инициализации libusb %s\n", libusb_strerror(ret));
        return -1;
    }

    // Включаем уровень логов WARNING (INFO бывает слишком разговорчив в цикле)
    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);
    printf("lubusb успешно инициализирован\n");

    /*
     *  Нахождение всех подлючённых USB устройств
    */

    ssize_t cnt = libusb_get_device_list(ctx, &list);
    if(cnt < 0) {
        fprintf(stderr, "Ошибка libusb_get_device_list: %s\n", libusb_strerror((int)cnt));
        libusb_exit(ctx);
        return -1;
    }

    /*
     *  Вывод найденых устройств
    */

    printf("Найдено USB-устройств:%zd\n", cnt);
    if(cnt == 0) {
        libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return 0;
    }

    uint32_t count_user_devises = 0;

    for(ssize_t i = 0; i < cnt; i++)
    {
        if(print_device_summary((int)i, list[i]) == USB_OK){
            count_user_devises++;
        }
    } 
    printf("Общее колличество найденых устройств %zu\n", cnt);
    return (ssize_t)cnt;
}

uint8_t open_device(ssize_t cnt){

    /*
     *  Ввод порядкового номера выбраного нами устройства для работы с ним 
    */

    int number_device;
    printf("Выберите устройство из порядкового номера: ");
    fflush(stdout);
    if( scanf("%d", &number_device) != 1 || number_device < 0 || number_device >= cnt ){
        fprintf(stderr, "Ошибка ввода номера.\n");
        libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return 1;
    }

    printf("Вы выбрали устройство с порядковым номером: %d\n", number_device);
    
    if( (number_device < 0) || (number_device >= cnt) )
    {
        fprintf(stderr,"Ошибка ввода порядкого номера!\n");
        libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return 1;
    }

    /*
     *  Открытие устройства для работы сним
    */

    int ret = libusb_open(list[number_device], &handle);
    if( (ret != LIBUSB_SUCCESS) || (handle == NULL) )
    {
        fprintf(stderr, "libusb_open: %s (часто нужен sudo или udev для этого VID:PID)\n",
        libusb_strerror(ret));
        libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return 1;
    }

    libusb_free_device_list(list, 1);

    /*
     *  Если интерфес занят ОС, то отедляем его 
    */
    int interface[] = {CTRL_INTERFACE, DATA_INTERFACE};
    for(uint8_t i = 0; i < sizeof(interface); i++ )
    {
        if (libusb_kernel_driver_active(handle, USB_INTERFACE_NUM_1) ) {
        printf("[INFO] Отключаем драйвер ядра от интерфейса %d...\n", USB_INTERFACE_NUM_1);
        ret = libusb_detach_kernel_driver(handle, USB_INTERFACE_NUM_1);
        if (ret != LIBUSB_SUCCESS) {
            fprintf(stderr, "Ошибка отключения драйвера ядра: %s\n", libusb_strerror(ret));
            }
        }

    }


    /*
     *  Захват интерфейса  
    */
   for(uint8_t i = 0; i < sizeof(interface); i++ )
   {
        ret = libusb_claim_interface( handle, USB_INTERFACE_NUM_1);
        if(ret != LIBUSB_SUCCESS) {
            fprintf(stderr, "[ERROR] Не удалось захватить интерфейс %d: %s\n", USB_INTERFACE_NUM_1, libusb_strerror(ret));
            libusb_close(handle);
            libusb_exit(ctx);
            return 1;
        }
   }
    // 5. Посылаем сигнал DTR/RTS = 1 (чтобы tud_cdc_connected() на ESP32 вернул true)
    printf("Отправка сигнала DTR/RTS (SET_CONTROL_LINE_STATE)...\n");
    ret = libusb_control_transfer(
        handle,
        0x21, // bmRequestType: OUT | CLASS | INTERFACE
        CDC_SET_CONTROL_LINE_STATE, // bRequest
        CDC_CONTROL_LINE_STATE_DTR | CDC_CONTROL_LINE_STATE_RTS, // wValue
        CTRL_INTERFACE, // wIndex (Interface 0)
        NULL, // data
        0, // length
        1000 // timeout (ms)
    );

    if (ret < 0) {
    fprintf(stderr, "SET_CONTROL_LINE_STATE failed: %s\n", libusb_strerror(ret));
    }

    return 0;

}

static int packaging_dump(char *buffer_dump, uint32_t size_buf)
{

    for(uint32_t i = 0; i < size_buf; i++ )
    {
        uint32_t word_start;
        memcpy(&word_start, buffer_dump + i, sizeof(word_start));
        if(word_start == ID_DUMP_FRAME_START)
        {
            return (int)i;
        }
    }

    return -1;
}

/*
 * Функция считывает дамп из USB устройства 
*/
int read_usb_device(libusb_device_handle *handle, char *pathDumpFile)
{
    uint32_t count_frames = 0;
    uint32_t total_bytes_count = 0;
    int actual_length;
    int ret;

    FILE *Dump_file = fopen(pathDumpFile,"wb");
    if(Dump_file == NULL) {
        fprintf(stderr,"Не удалось создать файл дампа\n");
        return 0;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    size_t size_frame = sizeof(DumpData) - sizeof(DumpData.buffer) + (sizeof(ModulData_t) * USB_LENGTH_DUMP);

    char *ModulData = calloc( 1, size_frame);

    printf("Начался опрос устройства\n");

    while( keep_running )
    {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ret = libusb_bulk_transfer( handle, USB_ADDRES_ENDPIONT, (uint8_t*)ModulData, size_frame, &actual_length, 500);
        if(ret == 0 && actual_length > 0) {
            printf("Полчуено %d байт данных\n",actual_length);
            total_bytes_count += actual_length;
            if( total_bytes_count >= (sizeof(ModulData_t) * USB_LENGTH_DUMP) ) break;
        } else if(ret == LIBUSB_ERROR_TIMEOUT) {
            printf("Таймаут!\n");
            if( (t1.tv_sec - t0.tv_sec) >= 10) break;
            continue;
        } else {
            fprintf(stderr, "\n[ERROR] Ошибка чтения: %s\n", libusb_strerror(ret));
            break;
        }

    }

    uint32_t start_head = packaging_dump(ModulData, size_frame);
    if(start_head < 0)
    {
        printf("Дамп утерян, не найден старт кадр\n");
        return -1;
    }

    size_t write = fwrite(ModulData + start_head, sizeof(ModulData_t), total_bytes_count / sizeof(ModulData_t), Dump_file);
    if(write != (total_bytes_count / sizeof(ModulData_t)))
    {
        printf("Неверно записался файл сырого дампа. Записано:%u Принято:%u \n", write, (total_bytes_count / sizeof(ModulData_t)) );
        return -2;
    }
    fflush(Dump_file);

    printf("\n[INFO] Завершение записи дампа...\n");
    fclose(Dump_file);

    free(ModulData);

    return total_bytes_count;

}


uint8_t USB_DeInit(void)
{
            /* --- ОЧИСТКА --- */
    int ret = libusb_release_interface(handle, USB_INTERFACE_NUM_1);
    if(ret != 0)
    {
        fprintf(stderr, "Ошибка отчискти интерфеса\n");
        return 1;
    }
    /*
     *  Закрытие устройства и высвобаждение ресурсов 
    */
    libusb_close(handle);

    libusb_exit(ctx);

    return 0;

}