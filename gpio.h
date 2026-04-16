#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  // Для системных вызовов: read, write, close, sleep, lseek
#include <fcntl.h>   // Для системного вызова open и флагов (O_WRONLY, O_RDWR)
#include <string.h>
#include <sys/stat.h>


char luck_fox_gpio(void);
char cursor_fuck(void);
void dansing_pin_fast(void);
