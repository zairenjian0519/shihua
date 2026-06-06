cmake_minimum_required(VERSION 3.10)

project(iec104_slave C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LIB60870_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib60870-master/lib60870-C" CACHE PATH "Path to lib60870-C")
set(JSON_C_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../json-c-master" CACHE PATH "Path to json-c source tree")

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${LIB60870_DIR}" "${CMAKE_BINARY_DIR}/lib60870")

if (EXISTS "${JSON_C_SOURCE_DIR}/CMakeLists.txt")
    set(BUILD_APPS OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(DISABLE_WERROR ON CACHE BOOL "" FORCE)
    add_subdirectory("${JSON_C_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/json-c")
    set(IEC104_JSON_C_TARGET json-c)
else()
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(JSON_C QUIET json-c)
    endif()
endif()

add_executable(iec104_slave
    src/main.c
    src/config.c
    src/active_upload.c
    src/client_context.c
    src/diag_server.c
    src/log.c
    src/point_table.c
    src/asdu_builder.c
    src/command_handler.c
    src/custom_asdu.c
    src/soe_history.c
    src/report_scheduler.c
    src/shm_adapter.c
    src/iec104_server.c
)

add_executable(iec104_diag
    tools/iec104_diag.c
)

target_include_directories(iec104_slave PRIVATE
    include
    "${LIB60870_DIR}/config"
    "${LIB60870_DIR}/src/inc/api"
    "${LIB60870_DIR}/src/inc/internal"
    "${LIB60870_DIR}/src/common/inc"
    "${LIB60870_DIR}/src/hal/inc"
)

target_link_libraries(iec104_slave PRIVATE lib60870)

if (TARGET ${IEC104_JSON_C_TARGET})
    target_compile_definitions(iec104_slave PRIVATE HAVE_JSON_C=1)
    target_link_libraries(iec104_slave PRIVATE ${IEC104_JSON_C_TARGET})
elseif (JSON_C_FOUND)
    target_compile_definitions(iec104_slave PRIVATE HAVE_JSON_C=1)
    target_include_directories(iec104_slave PRIVATE ${JSON_C_INCLUDE_DIRS})
    target_compile_options(iec104_slave PRIVATE ${JSON_C_CFLAGS_OTHER})
    target_link_libraries(iec104_slave PRIVATE ${JSON_C_LDFLAGS})
else()
    message(WARNING "json-c was not found. iec104_slave will build with legacy config scanner fallback.")
endif()

if (WIN32)
    target_compile_definitions(iec104_slave PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(iec104_slave PRIVATE ws2_32 iphlpapi bcrypt)
    target_compile_definitions(iec104_diag PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(iec104_diag PRIVATE ws2_32)
endif()
