# Install script for directory: D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/iec104_slave")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("D:/project/中石化-四化协议/proj/104_slave/build_jsonc/lib60870/src/cmake_install.cmake")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xDevelopmentx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/lib60870" TYPE FILE FILES
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/hal/inc/hal_time.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/hal/inc/hal_thread.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/hal/inc/hal_socket.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/hal/inc/hal_serial.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/hal/inc/hal_base.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/hal/inc/tls_config.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/hal/inc/tls_ciphers.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/common/inc/linked_list.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/inc/api/cs101_master.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/inc/api/cs101_slave.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/inc/api/cs104_slave.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/inc/api/iec60870_master.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/inc/api/iec60870_slave.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/inc/api/iec60870_common.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/inc/api/cs101_information_objects.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/inc/api/cs104_connection.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/inc/api/link_layer_parameters.h"
    "D:/project/中石化-四化协议/proj/lib60870-master/lib60870-C/src/file-service/cs101_file_service.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE PROGRAM FILES
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT/msvcp140.dll"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT/msvcp140_1.dll"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT/msvcp140_2.dll"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT/msvcp140_atomic_wait.dll"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT/msvcp140_codecvt_ids.dll"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT/vcruntime140_1.dll"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT/vcruntime140.dll"
    "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT/concrt140.dll"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE DIRECTORY FILES "")
endif()

