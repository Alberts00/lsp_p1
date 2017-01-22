cmake_minimum_required(VERSION 2.8)
project(lsp_p1)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror -std=gnu11")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lncurses")
set(CURSES_NEED_NCURSES TRUE)
find_package(Curses)

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

set(SERVER_SOURCE_FILES server/main.c)
set(CLIENT_SOURCE_FILES client/main.c)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
add_executable(lsp_p1_server ${SERVER_SOURCE_FILES})
add_executable(lsp_p1_client ${CLIENT_SOURCE_FILES})
target_link_libraries(lsp_p1_client ${CURSES_LIBRARIES})