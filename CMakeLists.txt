cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico-sdk/pico_sdk_init.cmake)

project(pico_examples C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_compile_options(-Wall -Werror -O2)

pico_sdk_init()

project(pico-cr100)

add_executable(cr100 chargen.c vt.c keyboard.c)

pico_enable_stdio_usb(cr100 1)



add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/vga_660x477_60.pio
  COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/vgamode.py
  DEPENDS vgamode.py
  )


pico_generate_pio_header(cr100 ${CMAKE_CURRENT_BINARY_DIR}/vga_660x477_60.pio)
pico_generate_pio_header(cr100 ${CMAKE_CURRENT_LIST_DIR}/atkbd.pio)

target_link_libraries(cr100 pico_stdlib pico_multicore hardware_pio)

pico_add_extra_outputs(cr100)
