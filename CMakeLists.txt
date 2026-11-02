cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_s3_mp3_lcd)

littlefs_create_partition_image(storage "${CMAKE_CURRENT_SOURCE_DIR}/audio_assets" FLASH_IN_PROJECT)
