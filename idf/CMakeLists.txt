# Top-level ESP-IDF project file for the Matter-over-Thread build.
# Build from this directory:  idf.py set-target esp32c6 && idf.py build flash monitor
#
# Pure native build: esp-matter (see main/idf_component.yml) plus custom
# RMT/GPIO drivers. No Arduino core.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-airconditioner-thread)
