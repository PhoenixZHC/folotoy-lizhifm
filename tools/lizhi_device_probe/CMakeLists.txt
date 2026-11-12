cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.1.0-probe")
set(EXTRA_COMPONENT_DIRS "../../firmware/components/lizhi_direct_client"
    "../../firmware/components/audio_stream"
    "../../firmware/ai-passport/components/bsp")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Lizhi-Direct-Probe)
