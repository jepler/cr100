cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include(pico-sdk/pico_sdk_init.cmake)

project(pico_examples C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_compile_options(-Wall -O2 -DPICO_STDIO_DEFAULT_CRLF=0 -DUSBD_PRODUCT=\"Pico\ CR100\")

pico_sdk_init()

project(pico-cr100)

add_executable(cr100
    chargen.c
    keyboard.c
    hl-vt100/src/lw_terminal_parser.c
    hl-vt100/src/lw_terminal_vt100.c
    )

set_source_files_properties(chargen.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wmissing-prototypes;-Wmissing-declarations;-Wstrict-prototypes;-O2")
set_source_files_properties(vt.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wmissing-prototypes;-Wmissing-declarations;-Wstrict-prototypes;-O2")
set_source_files_properties(keyboard.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wmissing-prototypes;-Wmissing-declarations;-Wstrict-prototypes;-O2")
set_source_files_properties(hl-vt100/src/lw_terminal_parser.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wmissing-prototypes;-Wmissing-declarations;-Wstrict-prototypes;-O2")
set_source_files_properties(hl-vt100/src/lw_terminal_vt100.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wmissing-prototypes;-Wmissing-declarations;-Wstrict-prototypes;-O2")

target_compile_options(cr100 PRIVATE
    )

target_include_directories(cr100 PRIVATE hl-vt100/src ${CMAKE_CURRENT_LIST_DIR})

pico_enable_stdio_usb(cr100 1)



add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/vga_660x477_60.pio
  COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/vgamode.py
  DEPENDS vgamode.py
  )


pico_generate_pio_header(cr100 ${CMAKE_CURRENT_BINARY_DIR}/vga_660x477_60.pio)
pico_generate_pio_header(cr100 ${CMAKE_CURRENT_LIST_DIR}/atkbd.pio)

target_link_libraries(cr100 pico_stdlib pico_multicore hardware_pio cmsis_core)

pico_add_extra_outputs(cr100)
