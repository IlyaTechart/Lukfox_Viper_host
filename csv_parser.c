
#include "csv_parser.h"




/**
 * Экспорт всех кадров дампа в CSV файл
 */
void logger_export_dump_to_csv(const char *buffer, size_t n_frames, const char *filepath)
{
    if (buffer == NULL || n_frames == 0) {
        fprintf(stderr, "logger_export_dump_to_csv: пустой буфер\n");
        return;
    }

    FILE *csv = fopen(filepath, "w");
    if (!csv) {
        perror("Не удалось создать CSV файл");
        return;
    }

    // 1. Пишем заголовок (названия колонок через запятую)
    fprintf(csv, "frame_index,start_marker,packet_counter,system_time_ms,"
                 "status_raw,grid_status,bypass_grid_status,rectifier_status,inverter_status,pwr_via_inverter,pwr_via_bypass,sync_status,load_mode,sound_alarm,battery_status,ups_mode,"
                 "alarms_raw,err_low_in_v,err_hi_dc,err_low_bat,err_no_bat,err_inv_fault,err_inv_oc,err_hi_out_v,err_fan,err_repl_bat,err_rect_hot,err_inv_hot,"
                 "v_in_AB,v_in_BC,v_in_CA,v_byp_A,v_byp_B,v_byp_C,i_in_A,i_in_B,i_in_C,freq_in,"
                 "v_out_A,v_out_B,v_out_C,freq_out,i_out_A,i_out_B,i_out_C,"
                 "p_act_A,p_act_B,p_act_C,p_app_A,p_app_B,p_app_C,load_A,load_B,load_C,event_count,"
                 "bat_v,bat_cap,bat_groups,dc_bus_v,bat_i,backup_time,"
                 "crc32\n");

    const ModulData_t *frames = (const ModulData_t *)(const void *)buffer;

    // 2. В цикле пишем данные каждого кадра
    for (size_t i = 0; i < n_frames; i++) {
        const FpgaToEspPacket_t *p = &frames[i].packet;

        // Раскомментируйте строку ниже, если в CSV нужно писать ТОЛЬКО кадры с ошибками
        // if (p->alarms.raw == 0) continue;

        // Внимание: bat_current имеет знак (может быть отрицательным), поэтому используем %d и (int)
        fprintf(csv, "%zu,%" PRIu32 ",%" PRIu32 ","
                     "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
                     "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
                     "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
                     "%u,%u,%u,%u,%u,%u,%u,"
                     "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
                     "%u,%u,%u,%u,%d,%u,"
                     "%" PRIu32 "\n",
                i, p->start_marker, p->system_time_ms,
                
                (unsigned)p->status.raw, (unsigned)p->status.grid_status, (unsigned)p->status.bypass_grid_status, (unsigned)p->status.rectifier_status, (unsigned)p->status.inverter_status, (unsigned)p->status.pwr_via_inverter, (unsigned)p->status.pwr_via_bypass, (unsigned)p->status.sync_status, (unsigned)p->status.load_mode, (unsigned)p->status.sound_alarm, (unsigned)p->status.battery_status, (unsigned)p->status.ups_mode,
                
                (unsigned)p->alarms.raw, (unsigned)p->alarms.err_low_input_vol, (unsigned)p->alarms.err_high_dc_bus, (unsigned)p->alarms.err_low_bat_charge, (unsigned)p->alarms.err_bat_not_conn, (unsigned)p->alarms.err_inv_fault, (unsigned)p->alarms.err_inv_overcurrent, (unsigned)p->alarms.err_high_out_vol, (unsigned)p->alarms.err_fan_fault, (unsigned)p->alarms.err_replace_bat, (unsigned)p->alarms.err_rect_overheat, (unsigned)p->alarms.err_inv_overheat,
                
                (unsigned)p->input.v_in_AB, (unsigned)p->input.v_in_BC, (unsigned)p->input.v_in_CA, (unsigned)p->input.v_bypass_A, (unsigned)p->input.v_bypass_B, (unsigned)p->input.v_bypass_C, (unsigned)p->input.i_in_A, (unsigned)p->input.i_in_B, (unsigned)p->input.i_in_C, (unsigned)p->input.freq_in,
                
                (unsigned)p->output.v_out_A, (unsigned)p->output.v_out_B, (unsigned)p->output.v_out_C, (unsigned)p->output.freq_out, (unsigned)p->output.i_out_A, (unsigned)p->output.i_out_B, (unsigned)p->output.i_out_C,
                
                (unsigned)p->output.p_active_A, (unsigned)p->output.p_active_B, (unsigned)p->output.p_active_C, (unsigned)p->output.p_apparent_A, (unsigned)p->output.p_apparent_B, (unsigned)p->output.p_apparent_C, (unsigned)p->output.load_pct_A, (unsigned)p->output.load_pct_B, (unsigned)p->output.load_pct_C, (unsigned)p->output.event_count,
                
                (unsigned)p->battery.bat_voltage, (unsigned)p->battery.bat_capacity, (unsigned)p->battery.bat_groups_count, (unsigned)p->battery.dc_bus_voltage, (int)((int16_t)p->battery.bat_current), (unsigned)p->battery.backup_time,
                
                p->crc32);
    }

    fclose(csv);
    printf("[INFO] Дамп успешно сохранен в CSV-файл: %s\n", filepath);
}
