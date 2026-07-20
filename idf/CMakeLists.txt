# Top-level ESP-IDF project file for the Matter-over-Thread build.
# Build from this directory:  idf.py set-target esp32c6 && idf.py build flash monitor
#
# The Arduino core is pulled in as an IDF component (see main/idf_component.yml).
# The sketch (.ino) and its .cpp/.h live one level up and are compiled by main/.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-airconditioner-thread)
