cmake_minimum_required(VERSION 3.20)

set(PICO_SDK_PATH "/home/martina/pico~/pico-sdk" CACHE PATH "")
set(PICO_BOARD "pico2" CACHE STRING "")
set(PICO_PLATFORM "rp2350-riscv" CACHE STRING "")
set(PICO_TOOLCHAIN_PATH "/home/martina/pico-tools/riscv-toolchain/bin/" CACHE PATH "")
set(PICO_GCC_TRIPLE "riscv32-unknown-elf" CACHE STRING "")

include("${PICO_SDK_PATH}/pico_sdk_init.cmake")
project(pico2_hardened_project C CXX ASM)
pico_sdk_init()

set(TARGET_NAME "pico2_hardened")
set(LLVM_BIN "/usr/lib/llvm-21/bin" CACHE PATH "")
set(ASPIS_DIR "/home/martina/CLionProjects/ASPIS2" CACHE PATH "")
set(RISCV_SYSROOT "/home/martina/pico-tools/riscv-toolchain/riscv32-unknown-elf" CACHE PATH "")
set(RP2350_RISCV_ARCH "rv32imac_zicsr_zifencei_zba_zbb_zbs_zbkb" CACHE STRING "")
set(RP2350_RISCV_ABI "ilp32" CACHE STRING "")

set(APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/multiple_functions.c")

set(HARDENED_LL "hardened.ll")
set(HARDENED_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/hardened.o")

# Esegue ASPIS con --anb-only al posto di --eddi/--cfcss
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${HARDENED_LL}"
    COMMAND
        "${ASPIS_DIR}/aspis.sh"
        --verbose
        --llvm-bin "${LLVM_BIN}"
        --anb-only
        --no-dup
        --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        ${APP_SOURCES}
        -o "${HARDENED_LL}"
        -g
        -S
        -emit-llvm
        --target=riscv32-unknown-elf
        "-march=${RP2350_RISCV_ARCH}"
        "-mabi=${RP2350_RISCV_ABI}"
        --sysroot="${RISCV_SYSROOT}"
        -ffreestanding
        -fno-builtin
        "-I${RISCV_SYSROOT}/include"
        "$<$<BOOL:$<TARGET_PROPERTY:${TARGET_NAME},INCLUDE_DIRECTORIES>>:-I$<JOIN:$<TARGET_PROPERTY:${TARGET_NAME},INCLUDE_DIRECTORIES>,;-I>>"
        "$<$<BOOL:$<TARGET_PROPERTY:${TARGET_NAME},COMPILE_DEFINITIONS>>:-D$<JOIN:$<TARGET_PROPERTY:${TARGET_NAME},COMPILE_DEFINITIONS>,;-D>>"
        --no-cleanup
    DEPENDS ${APP_SOURCES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Running the ASPIS hardening pipeline"
    COMMAND_EXPAND_LISTS
    VERBATIM
)

# Compila l'IR generato in un file oggetto RISC-V
add_custom_command(
    OUTPUT "${HARDENED_OBJECT}"
    COMMAND
        "${LLVM_BIN}/clang"
        --target=riscv32-unknown-elf
        "-march=${RP2350_RISCV_ARCH}"
        "-mabi=${RP2350_RISCV_ABI}"
        --sysroot="${RISCV_SYSROOT}"
        -ffreestanding
        -fno-builtin
        -fdata-sections
        -ffunction-sections
        -c "${CMAKE_CURRENT_BINARY_DIR}/${HARDENED_LL}"
        -o "${HARDENED_OBJECT}"
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${HARDENED_LL}"
    COMMENT "Compiling hardened LLVM IR to a RISC-V object"
    VERBATIM
)

add_custom_target(hardened_application_object DEPENDS "${HARDENED_OBJECT}")
set_source_files_properties("${HARDENED_OBJECT}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)

# Linka il file oggetto finale con la SDK di Pico
add_executable("${TARGET_NAME}" "${HARDENED_OBJECT}")
add_dependencies("${TARGET_NAME}" hardened_application_object)

set_target_properties("${TARGET_NAME}" PROPERTIES LINKER_LANGUAGE C)
target_link_libraries("${TARGET_NAME}" PRIVATE pico_stdlib m)

# Abilita l'output sia su USB che UART
pico_enable_stdio_uart("${TARGET_NAME}" 1)
pico_enable_stdio_usb("${TARGET_NAME}" 1)
pico_add_extra_outputs("${TARGET_NAME}")
