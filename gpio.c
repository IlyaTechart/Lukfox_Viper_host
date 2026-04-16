#include "gpio.h"
#include <stdarg.h>
#include <sys/mman.h>
#include <stdint.h>


#define GPIO_BANK_0 0
#define GPIO_BANK_1 1
#define GPIO_BANK_2 2
#define GPIO_BANK_3 3
#define GPIO_BANK_4 4

#define GPIO_GROUP_A 0
#define GPIO_GROUP_B 1
#define GPIO_GROUP_C 2
#define GPIO_GROUP_D 3

#define GPIO2_BASE        0xFF540000UL
#define GPIO_SWPORTA_DR   0x0000
#define GPIO_SWPORTA_DDR  0x0004


const char *const gpio_banks_map[] = { [GPIO_BANK_0] = "GPIO_0",
                                       [GPIO_BANK_1] = "GPIO_1",
                                       [GPIO_BANK_2] = "GPIO_2",
                                       [GPIO_BANK_3] = "GPIO_3",
                                       [GPIO_BANK_4] = "GPIO_4", };

const char *const gpio_groups_map[] = { [GPIO_GROUP_A] = "_A",
                                        [GPIO_GROUP_B] = "_B",
                                        [GPIO_GROUP_C] = "_C",
                                        [GPIO_GROUP_D] = "_D", };


void print_GPIO_gruop(int number_pin)
{
    int number_bank = 0;
    int number_group = 0;
    int number_pin_x = 0;

    number_bank = number_pin / 32;
    number_group = (number_pin % 32) / 8;
    number_pin_x = (number_pin % 32) % 8;

    printf("%s%s%d\n", gpio_banks_map[number_bank], gpio_groups_map[number_group], number_pin_x );
}


char luck_fox_gpio(){
    int gpio_pin;
    int time_cucle;
    
    printf("Please enter the GPIO pin number: \n");
    scanf("%d", &gpio_pin);

    printf("Please enter time: ");
    scanf("%d", &time_cucle);

    print_GPIO_gruop(gpio_pin);
    
    int export_file = open("/sys/class/gpio/export", O_WRONLY);
    if (export_file == -1) {
        perror("Failed to open GPIO export file");
        exit(-1);
    }
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d", gpio_pin);
    write( export_file, buf, sizeof(gpio_pin));
    close(export_file);

    char direction_path[50];
    snprintf(direction_path, sizeof(direction_path), "/sys/class/gpio/gpio%d/direction", gpio_pin);

    int attempts = 0;
    while (access(direction_path, F_OK) != 0 && attempts < 500) {
        usleep(10000); // ждем 10мс
        attempts++;
    }
    if (attempts == 500) {
        printf("Ошибка: папка gpio%d так и не появилась!\n", gpio_pin);
        exit(-1);
    }

    int direction_file = open(direction_path, O_WRONLY);
    if (direction_file == -1) {
        perror("Failed to open GPIO direction file");
        exit(-1);
    }
    write( direction_file, "out", strlen("out"));
    close(direction_file);

    usleep(100000);

    char value_path[50];
    char cat_command[100];
    snprintf(value_path, sizeof(value_path), "/sys/class/gpio/gpio%d/value", gpio_pin);
    snprintf(cat_command, sizeof(cat_command), "cat %s", value_path);
    int value_file = open(value_path, O_WRONLY);
    if (value_file == -1) {
        perror("Failed to open GPIO value file");
        exit(-1);
    }   

    for (int i = 0; i < 10; i++) {

        int fd = open(value_path,O_WRONLY);
        write(value_file, "1", 1);
        close(fd);

        // int fd_read = open(value_path, O_RDONLY);
        // char state;
        // read(fd_read, &state, 1);
        // close(fd_read);
        // printf("GPIO state: %c\n", state);

        // usleep(time_cucle);

        fd = open(value_path,O_WRONLY);
        write(value_file, "0", 1);
        close(fd);

        // fd_read = open(value_path, O_RDONLY);
        // read(fd_read, &state, 1);
        // close(fd_read);
        // printf("GPIO state: %c\n", state);
        
        // usleep(time_cucle);
    }

    close(value_file);

    int unexport_file = open("/sys/class/gpio/unexport", O_WRONLY);
    if (unexport_file == -1) {
        perror("Failed to open GPIO unexport file");
        exit(-1);
    }
    char number_gpio[10];
    snprintf(number_gpio, sizeof(number_gpio),"%d",gpio_pin);
    write(unexport_file, number_gpio, strlen(number_gpio));
    close(unexport_file);

    return 0;
}

void dansing_pin_fast(void)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    
    // Отображаем физическую память в виртуальное адресное пространство
    volatile uint32_t *gpio = mmap(NULL, 0x1000,
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED, fd, GPIO2_BASE);
    
    // Настраиваем пин 0 (GPIO2_A0) как выход
    gpio[GPIO_SWPORTA_DDR/4] |= (1 << 0);
    
    // Максимально быстрое переключение!
    while(1) {
        gpio[GPIO_SWPORTA_DR/4] |= (1 << 0);   // HIGH
        gpio[GPIO_SWPORTA_DR/4] &= ~(1 << 0);  // LOW
    }
}

