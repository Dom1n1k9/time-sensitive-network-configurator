# wtsn-tsn endpoint (STM32 Nucleo-F767ZI) — Zephyr build.
#
# Replaces the previous STM32CubeIDE / FreeRTOS / lwIP CMake build (see git
# history). Build + flash with west (a Zephyr SDK is required; see README
# "Building & flashing"):
#
#   cd stm32-tsn
#   west build -b nucleo_f767zi .
#   west flash -r          # on-board ST-Link (or: west flash --runner openocd)
#
# The app's source tree is unchanged from the FreeRTOS build in *layout*; the
# C files use the Zephyr kernel / device model instead of FreeRTOS + STM32 HAL.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wtsn_tsn C ASM)

target_sources(app PRIVATE
  src/main.c
  Board/board.c
  Board/pwm.c
  tsn/ptp.c
  tsn/eth_proto.c
  actuators/wtsn_sonar.c
  actuators/wtsn_display.c
  actuators/wtsn_actuator.c)

target_include_directories(app PRIVATE
  inc
  Board
  tsn
  actuators)
