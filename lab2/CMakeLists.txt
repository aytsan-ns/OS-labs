cmake_minimum_required(VERSION 3.10)
project(lab2_ipc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -Wall -Werror
add_compile_options(-Wall -Wextra -Werror)

set(COMMON_SOURCES
    host.cpp
)

file(GLOB CONN_SOURCES "conn_*.cpp")

foreach(CONN_SRC ${CONN_SOURCES})
    get_filename_component(CONN_NAME ${CONN_SRC} NAME_WE)
    string(REPLACE "conn_" "" TYPE_CODE ${CONN_NAME})

    set(EXEC_NAME "host_${TYPE_CODE}")

    add_executable(${EXEC_NAME}
        ${COMMON_SOURCES}
        ${CONN_SRC}
    )

    target_compile_definitions(${EXEC_NAME} PRIVATE CURRENT_CONN_HEADER=${CONN_NAME}.hpp)

    target_link_libraries(${EXEC_NAME} PRIVATE pthread rt)
endforeach()
