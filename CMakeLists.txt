cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico-sdk/pico_sdk_init.cmake)

project(pico_examples C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_compile_options(-Wall -Werror -O2)

pico_sdk_init()

project(pico-cr100)


add_executable(cr100 chargen.c)

pico_enable_stdio_usb(cr100 1)

pico_generate_pio_header(cr100 ${CMAKE_CURRENT_LIST_DIR}/vga_660_480_60.pio)

target_link_libraries(cr100 pico_stdlib pico_multicore hardware_pio)

pico_add_extra_outputs(cr100)
