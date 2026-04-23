
#include "usb_interface.h"

USB_UsersListHandle_t USB_UsersHandle;

extern sig_atomic_t keep_running;
extern DumpData_t DumpData;


/*
Функция ищет начало ID фрейма и возвращает значения по катрому в буфере начианется Дамп. 
*/

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

static void LIBUSB_CALL read_callback(struct libusb_transfer *xfer)
{
    dev_ctx_t *d = (dev_ctx_t *)xfer->user_data;

    switch (xfer->status) {
    case LIBUSB_TRANSFER_COMPLETED:
        if (xfer->actual_length > 0) {
            clock_gettime(CLOCK_MONOTONIC, &d->last_data_ts);

            /* Если ещё не нашли старт-кадр — ищем */
            if (!d->start_found) {
                uint32_t off = packaging_dump((char*)xfer->buffer, xfer->actual_length);
                /* ВАЖНО: убедитесь, что packaging_dump возвращает 
                   знаковое значение или какой-то сентинел типа UINT32_MAX
                   для «не найдено». Ниже предполагаю int32_t. */
                if ((int32_t)off < 0) {
                    /* старт-кадра нет в этой порции — ждём следующую */
                    break;
                }
                d->start_found = 1;
                d->state = DEV_STATE_RECEIVING;
                /* пишем хвост этой порции начиная со старт-кадра */
                size_t payload = xfer->actual_length - off;
                fwrite(xfer->buffer + off, 1, payload, d->file);
                d->total_received += payload;
                printf("[dev %d] Старт-кадр найден, offset=%u\n", d->index, off);
            } else {
                /* Обычный приём */
                fwrite(xfer->buffer, 1, xfer->actual_length, d->file);
                d->total_received += xfer->actual_length;
            }

            printf("[dev %d] +%d байт (итого %u/%zu)\n",
                   d->index, xfer->actual_length,
                   d->total_received, (size_t)TOTAL_DUMP_BYTES);

            if (d->total_received >= TOTAL_DUMP_BYTES) {
                printf("[dev %d] Дамп принят полностью\n", d->index);
                d->state = DEV_STATE_DONE;
                fflush(d->file);
                g_active_devices--;
                return; /* НЕ пересабмитим */
            }
        }
        break;

    case LIBUSB_TRANSFER_TIMED_OUT:
        /* При timeout=0 сюда не попадём */
        break;

    case LIBUSB_TRANSFER_CANCELLED:
        printf("[dev %d] Transfer cancelled\n", d->index);
        d->state = DEV_STATE_ERROR;
        g_active_devices--;
        return;

    case LIBUSB_TRANSFER_NO_DEVICE:
        fprintf(stderr, "[dev %d] Устройство отключено\n", d->index);
        d->state = DEV_STATE_ERROR;
        g_active_devices--;
        return;

    case LIBUSB_TRANSFER_STALL:
    case LIBUSB_TRANSFER_OVERFLOW:
    case LIBUSB_TRANSFER_ERROR:
    default:
        fprintf(stderr, "[dev %d] Transfer error: %d\n", d->index, xfer->status);
        d->state = DEV_STATE_ERROR;
        g_active_devices--;
        return;
    }

    /* Перезапускаем чтение */
    if (keep_running && d->state != DEV_STATE_DONE) {
        int ret = libusb_submit_transfer(xfer);
        if (ret != LIBUSB_SUCCESS) {
            fprintf(stderr, "[dev %d] resubmit failed: %s\n",
                    d->index, libusb_strerror(ret));
            d->state = DEV_STATE_ERROR;
            g_active_devices--;
        }
    }
}

int read_all_usb_devices(USB_UsersListHandle_t *usb_p, const char *base_dir)
{
    int ret;

    /* Подготовка контекстов */
    for (uint8_t i = 0; i < usb_p->UsersVelue_Handels; i++) {
        dev_ctx_t *d = &g_devs[i];
        memset(d, 0, sizeof(*d));
        d->index  = i;
        d->handle = usb_p->temp_handle[i];
        d->state  = DEV_STATE_WAITING;

        snprintf(d->path, sizeof(d->path), "%s/primary_dump%d",       // TODO
                 base_dir, usb_p->device_list_target[i]);

        d->file = fopen(d->path, "wb");
        if (!d->file) {
            fprintf(stderr, "[dev %d] fopen %s: %s\n",
                    i, d->path, strerror(1));
            goto fail;
        }

        d->xfer = libusb_alloc_transfer(0);
        if (!d->xfer) {
            fprintf(stderr, "[dev %d] alloc_transfer failed\n", i);
            goto fail;
        }

        libusb_fill_bulk_transfer(
            d->xfer,
            d->handle,
            USB_ADDRES_ENDPIONT,
            d->buffer,
            DUMP_CHUNK_SIZE,
            read_callback,
            d,                      /* user_data */
            DUMP_TIMEOUT_MS);

        ret = libusb_submit_transfer(d->xfer);
        if (ret != LIBUSB_SUCCESS) {
            fprintf(stderr, "[dev %d] submit: %s\n", i, libusb_strerror(ret));
            goto fail;
        }
        g_active_devices++;
    }

    printf("Запущено прослушивание %d устройств\n", g_active_devices);

    /* Главный event-loop. Блокируется внутри handle_events до событий. */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    while (keep_running && g_active_devices > 0) {
        ret = libusb_handle_events_timeout_completed(usb_p->ctx, &tv, NULL);
        if (ret != LIBUSB_SUCCESS) {
            fprintf(stderr, "handle_events: %s\n", libusb_strerror(ret));
            break;
        }
        /* Здесь можно печатать прогресс или проверять глобальный таймаут */
    }

    /* Если вышли по Ctrl+C — отменяем всё, что в полёте */
    if (!keep_running) {
        printf("Останов по сигналу, отмена transfers...\n");
        for (uint8_t i = 0; i < usb_p->UsersVelue_Handels; i++) {
            if (g_devs[i].state != DEV_STATE_DONE &&
                g_devs[i].state != DEV_STATE_ERROR) {
                libusb_cancel_transfer(g_devs[i].xfer);
            }
        }
        /* Дождаться, пока колбэки отработают CANCELLED */
        while (g_active_devices > 0) {
            libusb_handle_events_timeout_completed(usb_p->ctx, &tv, NULL);
        }
    }

    /* Cleanup */
    for (uint8_t i = 0; i < usb_p->UsersVelue_Handels; i++) {
        if (g_devs[i].file)  { fclose(g_devs[i].file); g_devs[i].file = NULL; }
        if (g_devs[i].xfer)  { libusb_free_transfer(g_devs[i].xfer); g_devs[i].xfer = NULL; }
    }

    return 0;

fail:
    for (uint8_t i = 0; i < usb_p->UsersVelue_Handels; i++) {
        if (g_devs[i].file)  fclose(g_devs[i].file);
        if (g_devs[i].xfer)  libusb_free_transfer(g_devs[i].xfer);
    }
    return -1;
}



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

    memset(&USB_UsersHandle, 0x00, sizeof(USB_UsersHandle));

    int err = 0;
    struct libusb_init_option options[] = {
        {.option = LIBUSB_OPTION_LOG_LEVEL, .value = {.ival = LIBUSB_LOG_LEVEL_INFO} } 
    };
    int num_options = sizeof(options) / sizeof(options[0]);
    /*
     * Инициализация контекста lubusb
    */
    int ret = libusb_init(&USB_UsersHandle.ctx);
    if(ret != LIBUSB_SUCCESS ) {
        fprintf(stderr,"Ошибка инициализации libusb %s\n", libusb_strerror(ret));
        return -1;
    }

    // Включаем уровень логов WARNING (INFO бывает слишком разговорчив в цикле)
    libusb_set_option(USB_UsersHandle.ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);
    printf("lubusb успешно инициализирован\n");

    /*
     *  Нахождение всех подлючённых USB устройств
    */

    USB_UsersHandle.CountDevicesFound = libusb_get_device_list(USB_UsersHandle.ctx, &USB_UsersHandle.list);
    if(USB_UsersHandle.CountDevicesFound <= 0) {
        fprintf(stderr, "Ошибка libusb_get_device_list: %s\n", libusb_strerror((int)USB_UsersHandle.CountDevicesFound));
        libusb_free_device_list(USB_UsersHandle.list, 1);
        libusb_exit(USB_UsersHandle.ctx);
        return -1;
    }

    /*
     *  Вывод найденых устройств
    */

    printf("Найдено USB-устройств:%zd\n", USB_UsersHandle.CountDevicesFound);

    uint32_t count_user_devises = 0;

    for(ssize_t i = 0; i < USB_UsersHandle.CountDevicesFound; i++)
    {
        if(print_device_summary((int)i, USB_UsersHandle.list[i]) == USB_OK){
            count_user_devises++;
            USB_UsersHandle.device_list_target[count_user_devises] = i;
        }
    } 
    printf("Общее колличество найденых устройств %zu\n", USB_UsersHandle.CountDevicesFound);
    return (ssize_t)USB_UsersHandle.CountDevicesFound;
}

uint8_t open_device(USB_UsersListHandle_t *usb_p){

    /*
     *  Ввод порядкового номера выбраного нами устройства для работы с ним 
    */

    int number_device[NUMBER_SUPPORT_DEVICES];
    int count_device;
    printf("Введите количество опрашиваемых устройств: ");
    fflush(stdout);
    if( scanf("%d", &count_device) != 1 || count_device <= 0)
    {
        fprintf(stderr, "Ошибка ввода коллчества.\n");
        libusb_free_device_list(usb_p->list, 1);
        libusb_exit(usb_p->ctx);
        return 1;
    }
    usb_p->UsersVelue_Handels = count_device;

    printf("\nВыберите устройства из порядкового номера: ");
    fflush(stdout);
    
    for(uint8_t i = 0; i < count_device; i++)
    {
        if( scanf("%d", &number_device[i]) != 1 || number_device[i] < 0 || number_device[i] >= usb_p->CountDevicesFound ){
            fprintf(stderr, "Ошибка ввода номера.\n");
            libusb_free_device_list(usb_p->list, 1);
            libusb_exit(usb_p->ctx);
            return 1;
        }

    }

    printf("Выбраны следующиие устройства: ");
    for(uint8_t i = 0; i < count_device; i++)
    {
        printf(" %d", number_device[i]);
    }
    printf("\n\n");
    
    int ret;
    /*
     *  Открытие устройства для работы сним
    */
    for(uint8_t i = 0; i < count_device; i++ )
    {
        ret = libusb_open(usb_p->list[number_device[i]], &usb_p->temp_handle[i]);
        if( (ret != LIBUSB_SUCCESS) || (usb_p->temp_handle[i] == NULL) )
        {
            fprintf(stderr, "libusb_open: %s (часто нужен sudo или udev для этого VID:PID)\n",
            libusb_strerror(ret));
            libusb_free_device_list(usb_p->list, 1);
            libusb_exit(usb_p->ctx);
            return 1;
        }

    }


    libusb_free_device_list(usb_p->list, 1);
    if(usb_p->list != NULL){
        usb_p->list = NULL;
    }

    /*
     *  Если интерфес занят ОС, то отедляем его 
    */
    int interface[] = {CTRL_INTERFACE, DATA_INTERFACE};
    const size_t iface_count = sizeof(interface) /  sizeof(interface[0]);
    for(uint8_t i = 0; i < count_device; i++)
    {
        for(uint8_t t = 0; t < iface_count; t++ )
        {
            if (libusb_kernel_driver_active(usb_p->temp_handle[i], USB_INTERFACE_NUM_1) ) {
            printf("[INFO] Отключаем драйвер ядра от интерфейса %d...\n", USB_INTERFACE_NUM_1);
            ret = libusb_detach_kernel_driver(usb_p->temp_handle[i], USB_INTERFACE_NUM_1);
            if (ret != LIBUSB_SUCCESS) {
                fprintf(stderr, "Ошибка отключения драйвера ядра: %s\n", libusb_strerror(ret));
                }
            }
        }
    }

    /*
     *  Захват интерфейса  
    */
   for(uint8_t i = 0; i < count_device; i++ )
   {
        for(uint8_t t = 0; t < iface_count; t++ )
        {
            ret = libusb_claim_interface( usb_p->temp_handle[i] , USB_INTERFACE_NUM_1 );
            if(ret != LIBUSB_SUCCESS) {
                fprintf(stderr, "[ERROR] Не удалось захватить интерфейс %d: %s\n", USB_INTERFACE_NUM_1, libusb_strerror(ret));
                libusb_close(usb_p->temp_handle[i]);
                libusb_exit(usb_p->ctx);
                return 1;
            }
        }
        // 5. Посылаем сигнал DTR/RTS = 1 (чтобы tud_cdc_connected() на ESP32 вернул true)
        printf("Отправка сигнала DTR/RTS (SET_CONTROL_LINE_STATE)...\n");
        ret = libusb_control_transfer(
             usb_p->temp_handle[i],
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
   }

      printf("Программа дошла до сюда\n ");


    return 0;

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
        fclose(Dump_file);
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

    int start_head = packaging_dump(ModulData, size_frame);
    if(start_head < 0)
    {
        printf("Дамп утерян, не найден старт кадр\n");
        fclose(Dump_file);
        return -1;
    }

    size_t write = fwrite(ModulData + start_head, sizeof(ModulData_t), total_bytes_count / sizeof(ModulData_t), Dump_file);
    if(write != (total_bytes_count / sizeof(ModulData_t)))
    {
        printf("Неверно записался файл сырого дампа. Записано:%u Принято:%u \n", write, (total_bytes_count / sizeof(ModulData_t)) );
        fclose(Dump_file);
        return -2;
    }
    fflush(Dump_file);

    printf("\n[INFO] Завершение записи дампа...\n");
    fclose(Dump_file);

    free(ModulData);

    return total_bytes_count;

}

uint8_t USB_DeInit(USB_UsersListHandle_t *usb_p)
{
    if (usb_p == NULL) {
        return 1;
    }

    /* 1) Освобождаем интерфейсы и закрываем открытые устройства */
    for (uint8_t i = 0; i < usb_p->UsersVelue_Handels; i++) {
        libusb_device_handle *h = usb_p->temp_handle[i];
        if (h == NULL) {
            continue;
        }

        /* Если вы захватывали только DATA_INTERFACE (1), освобождаем его */
        int ret = libusb_release_interface(h, DATA_INTERFACE);
        if (ret != LIBUSB_SUCCESS &&
            ret != LIBUSB_ERROR_NOT_FOUND &&
            ret != LIBUSB_ERROR_NO_DEVICE) {
            fprintf(stderr, "[WARN] release interface dev %u: %s\n",
                    i, libusb_strerror(ret));
        }

        /* Опционально: вернуть kernel-драйвер обратно, если нужно */
        /* libusb_attach_kernel_driver(h, DATA_INTERFACE); */

        libusb_close(h);
        usb_p->temp_handle[i] = NULL;
    }

    /* 2) Освобождаем список устройств, если ещё не освобождён */
    if (usb_p->list != NULL) {
        libusb_free_device_list(usb_p->list, 1);
        usb_p->list = NULL;
    }

    /* 3) Завершаем libusb context */
    if (usb_p->ctx != NULL) {
        libusb_exit(usb_p->ctx);
        usb_p->ctx = NULL;
    }

    usb_p->UsersVelue_Handels = 0;
    usb_p->CountDevicesFound = 0;

    return 0;
}