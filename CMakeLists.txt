cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})

set(CMAKE_TOOLCHAIN_FILE $ENV{ARDUINO_CMAKE_DIR}"/cmake/ArduinoToolchain.cmake")

#fixing bug with arduino toolchain
function(_cmake_record_install_prefix )
endfunction()

# include(pico_sdk_import.cmake)
include(${CMAKE_TOOLCHAIN_FILE})

project(necklace C CXX ASM)
set(CMAKE_C_STANDARD 11)

generate_arduino_firmware(blink_original
    SKETCH .
    PORT  /dev/ttyACM0
    SERIAL picocom @SERIAL_PORT@
    BOARD adafruit_feather_rp2040)

set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(necklace
  main.cpp
)

target_link_libraries(necklace 
  pico_stdlib
  hardware_pio
)

pico_add_extra_outputs(necklace)
