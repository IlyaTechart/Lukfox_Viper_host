#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stddef.h>
#include <signal.h>
#include <unistd.h>

#include "usb_interface.h"
#include "csv_parser.h"


extern USB_UsersListHandle_t USB_UsersHandle;
extern libusb_device_handle *handle;
extern libusb_context *ctx;

volatile sig_atomic_t keep_running = 1;
DumpData_t DumpData = {0};

static void int_handler(int dummy)
{
    keep_running = 0;
}

/** Печать одного кадра дампа (все поля FpgaToEspPacket_t) в stdout. */
static void logger_print_one_frame(const ModulData_t *m, size_t frame_index)
{
    const FpgaToEspPacket_t *p = &m->packet;

    printf("\n========== frame #%zu ==========\n", frame_index);
    printf("start_marker:   0x%08" PRIx32 " (%" PRIu32 ")\n", p->start_marker, p->start_marker);

    printf("[STATUS] raw=0x%04x | Grid:%u BypGrid:%u Rect:%u Inv:%u PwrInv:%u PwrByp:%u Sync:%u Load:%u Sound:%u BatSt:%u UpsMode:%u\n",
           (unsigned)p->status.raw,
           (unsigned)p->status.grid_status, (unsigned)p->status.bypass_grid_status,
           (unsigned)p->status.rectifier_status, (unsigned)p->status.inverter_status,
           (unsigned)p->status.pwr_via_inverter, (unsigned)p->status.pwr_via_bypass,
           (unsigned)p->status.sync_status, (unsigned)p->status.load_mode,
           (unsigned)p->status.sound_alarm, (unsigned)p->status.battery_status,
           (unsigned)p->status.ups_mode);

    printf("[ALARM] raw=0x%04x | LowIn:%u HiDC:%u LowBat:%u NoBat:%u InvF:%u InvOC:%u HiOut:%u Fan:%u ReplBat:%u RectHot:%u InvHot:%u\n",
           (unsigned)p->alarms.raw,
           (unsigned)p->alarms.err_low_input_vol, (unsigned)p->alarms.err_high_dc_bus,
           (unsigned)p->alarms.err_low_bat_charge, (unsigned)p->alarms.err_bat_not_conn,
           (unsigned)p->alarms.err_inv_fault, (unsigned)p->alarms.err_inv_overcurrent,
           (unsigned)p->alarms.err_high_out_vol, (unsigned)p->alarms.err_fan_fault,
           (unsigned)p->alarms.err_replace_bat, (unsigned)p->alarms.err_rect_overheat,
           (unsigned)p->alarms.err_inv_overheat);

    printf("[INPUT] V_in AB/BC/CA (x0.1 V): %u %u %u | V_byp A/B/C: %u %u %u\n",
           (unsigned)p->input.v_in_AB, (unsigned)p->input.v_in_BC, (unsigned)p->input.v_in_CA,
           (unsigned)p->input.v_bypass_A, (unsigned)p->input.v_bypass_B, (unsigned)p->input.v_bypass_C);
    printf("[INPUT] I_in A/B/C (x0.1 A): %u %u %u | freq_in (x0.01 Hz): %u\n",
           (unsigned)p->input.i_in_A, (unsigned)p->input.i_in_B, (unsigned)p->input.i_in_C,
           (unsigned)p->input.freq_in);

    printf("[OUTPUT] V_out A/B/C (x0.1 V): %u %u %u | freq_out (x0.01 Hz): %u\n",
           (unsigned)p->output.v_out_A, (unsigned)p->output.v_out_B, (unsigned)p->output.v_out_C,
           (unsigned)p->output.freq_out);
    printf("[OUTPUT] I_out A/B/C (x0.1 A): %u %u %u\n",
           (unsigned)p->output.i_out_A, (unsigned)p->output.i_out_B, (unsigned)p->output.i_out_C);
    printf("[OUTPUT] P_active A/B/C (x0.1 kW): %u %u %u | P_apparent A/B/C (x0.1 kVA): %u %u %u\n",
           (unsigned)p->output.p_active_A, (unsigned)p->output.p_active_B, (unsigned)p->output.p_active_C,
           (unsigned)p->output.p_apparent_A, (unsigned)p->output.p_apparent_B, (unsigned)p->output.p_apparent_C);
    printf("[OUTPUT] Load %% A/B/C (x0.1): %u %u %u | event_count: %u\n",
           (unsigned)p->output.load_pct_A, (unsigned)p->output.load_pct_B, (unsigned)p->output.load_pct_C,
           (unsigned)p->output.event_count);

    {
        int16_t cur = (int16_t)p->battery.bat_current;
        uint16_t cur_abs = (uint16_t)(cur < 0 ? -cur : cur);
        printf("[BAT] Vbat (x0.1 V): %u | Cap (Ah): %u | groups: %u | DC (x0.1 V): %u\n",
               (unsigned)p->battery.bat_voltage, (unsigned)p->battery.bat_capacity,
               (unsigned)p->battery.bat_groups_count, (unsigned)p->battery.dc_bus_voltage);
        printf("[BAT] I (x0.1 A, signed): %s%u.%u | backup (min): %u\n",
               cur < 0 ? "-" : "",
               (unsigned)(cur_abs / 10u), (unsigned)(cur_abs % 10u),
               (unsigned)p->battery.backup_time);
    }

    printf("crc32:          0x%08" PRIx32 "\n", p->crc32);
    printf("system_time_ms: %" PRIu32 "\n", p->system_time_ms);

}

/**
 * Вывод всех полей каждого кадра ModulData_t из непрерывного буфера дампа.
 * @param buffer   сырой буфер (каждые sizeof(ModulData_t) байт — один кадр)
 * @param n_frames число кадров ModulData_t в буфере
 */
void logger_print_dump(DumpData_t *DumpData, size_t total_frames)
{
    if (DumpData->buffer == NULL || total_frames < 2) {
        fprintf(stderr, "Дамп слишком мал\n");
        return;
    }

    if ( DumpData->head_frames != ID_DUMP_FRAME_START ) {
        uint32_t head = 0 ;
        fprintf(stderr, "Неверный формат заголовка: %X\n", DumpData->head_frames);
        return;
    }

    uint32_t n_frames_from_esp = 0;
    memcpy(&n_frames_from_esp, &DumpData->count_elements, sizeof(DumpData->count_elements));

    const ModulData_t *frames = (const ModulData_t *)(const void *)DumpData->buffer;

    for (size_t i = 0; i < total_frames; i++) {
        if(frames[i].packet.alarms.raw != 0)
        {
            logger_print_one_frame(&frames[i], i);
            //printf("Delta ms: %" PRIu32 "\n", frames[i]->packet);
        }
        //logger_print_one_frame(&frames[i], i);
    }

    printf("В заголовке заявлено: %u кадров. Реально скачано: %zu кадров\n", 
    n_frames_from_esp, total_frames);
}


void error_handler(char *name_err, uint16_t line)
{
    printf("%s - on line:%d\n", name_err, line);
}


int main(int argc, char *argv[])
{
    signal(SIGINT, int_handler);

    ssize_t cnt = USB_Init();
    if (cnt <= 0) {
        if (cnt < 0) {
            fprintf(stderr, "USB_Init: ошибка\n");
        } else {
            fprintf(stderr, "USB: нет устройств %d\n", cnt);
        }
        return 1;
    }

    if (open_device(&USB_UsersHandle) != 0) {
        printf("Ошибка в функции: open_device\n");
        return 1;
    }

    int rc = read_all_usb_devices(&USB_UsersHandle, "/home/pico/log_folder_USB");
    if (rc < 0) {
        fprintf(stderr, "Ошибка приёма дампов\n");
    }

    // for(uint8_t i = 0; i < USB_UsersHandle.UsersVelue_Handels; i++)
    // {
    //     char path[200];
    //     snprintf(path, sizeof(path),"/home/pico/log_folder_USB/primary_dump%d", USB_UsersHandle.device_list_target[i]);
    //     int recive_bytes = read_usb_device(USB_UsersHandle.temp_handle[i], path);
    //     if(recive_bytes < 0)
    //     {
    //         return 1;
    //     }
    // }

    USB_DeInit(&USB_UsersHandle);

    DumpData.buffer = calloc(USB_LENGTH_DUMP, sizeof(ModulData_t));
    if (DumpData.buffer == NULL) {
        fprintf(stderr, "calloc: не удалось выделить буфер под дамп\n");
        return 1;
    }

    FILE *dump_file = fopen("/home/pico/primary_dump", "rb");
    if (dump_file == NULL) {
        perror("fopen /home/pico/primary_dump");
        free(DumpData.buffer);
        return 1;
    }

    if(!fread(&DumpData.head_frames, sizeof(DumpData.head_frames), 1, dump_file)){
        error_handler("Head_frame", 229);
        goto exit;
    } 
    if(!fread(&DumpData.count_elements, sizeof(DumpData.count_elements), 1, dump_file)){
        error_handler("Count_elements", 233);
        goto exit;
    } 
    size_t t_read = fread(DumpData.buffer, sizeof(ModulData_t), DumpData.count_elements, dump_file);
    // if(!fread(&DumpData.tail_frames, sizeof(DumpData.tail_frames), 1, dump_file))
    // {
    //     error_handler("Count_elements", 238);
    //     goto exit;
    // } 
    fclose(dump_file);


    if ( t_read != DumpData.count_elements ){
        exit:
        fprintf(stderr, "Файл дампа пуст или не прочитан.\n");
        free(DumpData.buffer);
        return 1;
    }
   // uint32_t frames = ( t_read - sizeof(DumpData) + sizeof(DumpData.buffer) ) / sizeof(ModulData_t);

    logger_print_dump(&DumpData, t_read); // Ошибка

    FILE *format_csv_file = fopen("/home/pico/format_csv_file.csv", "wb");
    if (format_csv_file == NULL) {
        perror("fopen /home/pico/primary_dump");
        free(DumpData.buffer);
        return 1;
    }

    //logger_export_dump_to_csv(buffer, n_read, "/home/pico/dump_data.csv");

    /*

        // 3. ОТПРАВЛЯЕМ ФАЙЛ ЧЕРЕЗ system()
    char command[256];

    printf("\n[INFO] Отправка CSV файла на компьютер %s...\n", target_ip);



    // --- ВАРИАНТ 2: Отправка через SCP (Если на Linux настроен SSH сервер) ---
    // Если хотите использовать SCP, закомментируйте строку выше и раскомментируйте эту.
    // Замените "user" на имя пользователя вашего Linux компьютера!
     snprintf(command, sizeof(command), "scp %s user@%s:/tmp/", csv_path, target_ip);

    // Выполняем системную команду
    int sys_ret = system(command);
    
    if (sys_ret == 0) {
        printf("[INFO] Файл успешно отправлен!\n");
    } else {
        printf("[ERROR] Ошибка отправки файла (код возврата system: %d).\n", sys_ret);
    }
*/

    for (int count = 1; count <= 100; count++) {
        // \r возвращает курсор в начало строки
        printf("\rТекущее значение: %d %%", count);
        
        // Принудительно выталкиваем данные на экран
        fflush(stdout);
        
        // Ждем 0.1 секунды (100 000 микросекунд), чтобы увидеть результат
        usleep(10000); 
    }
    //printf("\nОбщее колличесво полученых данных: %d KB\n", (recive_bytes / 1024) );
    free(DumpData.buffer);
    printf("Программа УСПЕШНО выполнена!\n");
    return 0;
}
