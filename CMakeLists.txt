cmake_minimum_required(VERSION 3.10)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VITASDK})
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "")
endif()
project(VitaCoverConverter C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
set(CMAKE_C_STANDARD 11)
add_compile_options(-O2 -Wall -Wextra)
add_executable(vita-cover-converter src/main.c src/artwork.c)
target_include_directories(vita-cover-converter PRIVATE src)
target_link_libraries(vita-cover-converter
  vita2d SceCtrl_stub SceTouch_stub SceDisplay_stub SceGxm_stub
  SceSysmodule_stub SceAppMgr_stub SceCommonDialog_stub SceIme_stub
  ScePgf_stub ScePvf_stub SceIofilemgr_stub m z png jpeg)
vita_create_self(eboot.bin vita-cover-converter)
vita_create_vpk(VitaCoverConverter.vpk ONURCOV01 eboot.bin
  VERSION "01.10" NAME "Vita Cover Converter"
  FILE build-assets/sce_sys/icon0.png sce_sys/icon0.png
  FILE build-assets/sce_sys/pic0.png sce_sys/pic0.png
  FILE build-assets/sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
  FILE build-assets/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE build-assets/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml)
