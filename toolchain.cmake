set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Используем стандартные компиляторы, которые мы только что установили в WSL
set(CMAKE_C_COMPILER "/usr/bin/arm-linux-gnueabihf-gcc")
set(CMAKE_CXX_COMPILER "/usr/bin/arm-linux-gnueabihf-g++")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_FIND_ROOT_PATH
    /usr/arm-linux-gnueabihf
    /usr/lib/arm-linux-gnueabihf
)