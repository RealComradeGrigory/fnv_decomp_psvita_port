## FNV Vita - Fallout: New Vegas Data Viewer for PS Vita
## Reads FNV game data files and renders them using GXM
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
  else()
    message(FATAL_ERROR "VITASDK environment variable not set. Please install VitaSDK.")
  endif()
endif()

project(fnv_vita LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

# Build options
set(VITA_APP_NAME "FNV Vita")
set(VITA_TITLEID  "FNVV00001")
set(VITA_VERSION  "00.10")

# Compiler flags
add_compile_options(
  -Wall -Wextra
  -O2
  -ftree-vectorize
  -mfpu=neon
  -mcpu=cortex-a9
  -mfloat-abi=hard
)

# Source files
set(SOURCES
  src/main.cpp
  src/bsa/bsa_reader.cpp
  src/dds/dds_loader.cpp
  src/esm/esm_reader.cpp
  src/nif/nif_loader.cpp
  src/renderer/gxm_renderer.cpp
  src/scene/cell_scene.cpp
  src/scene/map_data.cpp
  src/scene/world_scene.cpp
  src/pipboy/pipboy.cpp
  src/weapon/weapon_system.cpp
  src/audio/audio_system.cpp
  src/npc/npc_system.cpp
  src/physics/physics_engine.cpp
  src/ui/hud.cpp
  src/script/script_engine.cpp
)

# Main executable
add_executable(${PROJECT_NAME} ${SOURCES})
target_compile_definitions(${PROJECT_NAME} PRIVATE FNV_VITA_TARGET=1)

target_include_directories(${PROJECT_NAME} PRIVATE
  src/
)

target_link_libraries(${PROJECT_NAME}
  SceDisplay_stub
  SceGxm_stub
  SceCtrl_stub
  SceSysmodule_stub
  SceTouch_stub
  SceAudio_stub
  ScePower_stub
  SceCommonDialog_stub
  SceAppMgr_stub
  SceIofilemgr_stub  # renamed from SceLibc; needed for file I/O
  z                   # zlib for BSA decompression
  m                   # math
)

# Create the SELF (signed ELF)
vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)

# Create the VPK package
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE assets/player_arms.nif assets/player_arms.nif
  FILE assets/viewmodel0.vma assets/viewmodel0.vma
  FILE assets/viewmodel1.vma assets/viewmodel1.vma
  FILE assets/viewmodel2.vma assets/viewmodel2.vma
  FILE assets/viewmodel3.vma assets/viewmodel3.vma
  FILE assets/viewmodel4.vma assets/viewmodel4.vma
  FILE assets/doc_body.nif assets/doc_body.nif
  FILE assets/doc_right.nif assets/doc_right.nif
  FILE assets/doc_left.nif assets/doc_left.nif
  FILE assets/hands0_right.nif assets/hands0_right.nif
  FILE assets/hands0_left.nif assets/hands0_left.nif
  FILE assets/hands1_right.nif assets/hands1_right.nif
  FILE assets/hands1_left.nif assets/hands1_left.nif
  FILE assets/hands2_right.nif assets/hands2_right.nif
  FILE assets/hands2_left.nif assets/hands2_left.nif
  FILE assets/hands3_right.nif assets/hands3_right.nif
  FILE assets/hands3_left.nif assets/hands3_left.nif
  FILE assets/hands4_right.nif assets/hands4_right.nif
  FILE assets/hands4_left.nif assets/hands4_left.nif
  FILE cells/GSDocMitchellHouse.cell cells/GSDocMitchellHouse.cell
  FILE cells/GSGasStation.cell cells/GSGasStation.cell
  FILE cells/GSGeneralStore.cell cells/GSGeneralStore.cell
  FILE cells/GSProspectorSaloonInterior.cell cells/GSProspectorSaloonInterior.cell
  FILE cells/GSSchoolHouse.cell cells/GSSchoolHouse.cell
  # FILE sce_sys/icon0.png sce_sys/icon0.png  # uncomment when you have an icon
  # FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  # FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(TARGET ${PROJECT_NAME}.vpk-vpk POST_BUILD
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/package_world.py"
    --vpk "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.vpk"
    --world "${CMAKE_CURRENT_SOURCE_DIR}/world"
  VERBATIM
)
