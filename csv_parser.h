#pragma once
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
#include <inttypes.h>
#include "frames_structure.h"





void logger_export_dump_to_csv(const char *buffer, size_t n_frames, const char *filepath);