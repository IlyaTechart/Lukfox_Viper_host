cmake_minimum_required(VERSION 3.10)
project(hello_luckfox C)
# Указываем, что хотим собирать с отладочной информацией (для дебага)
set(CMAKE_BUILD_TYPE Debug)
# Добавляем наш исполняемый файл
add_executable(hello_luckfox main.c gpio.c usb_interface.c
    csv_parser.c)
# Заголовки libusb (путь может отличаться — см. ниже)
target_include_directories(hello_luckfox PRIVATE /usr/include)
# Линковка с ARM-библиотекой
target_link_libraries(hello_luckfox PRIVATE usb-1.0)
# Где лежат .so для armhf (линкер)
target_link_directories(hello_luckfox PRIVATE /usr/lib/arm-linux-gnueabihf)