cmake_minimum_required(VERSION 3.3)
project(lsp_p1)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Werror -std=c11")

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

set(SERVER_SOURCE_FILES server/main.c)
set(CLIENT_SOURCE_FILES client/main.c)

add_executable(lsp_p1_server ${SERVER_SOURCE_FILES})
add_executable(lsp_p1_client ${CLIENT_SOURCE_FILES})
