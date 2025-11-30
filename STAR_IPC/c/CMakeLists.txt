cmake_minimum_required(VERSION 3.5.0)
project(IPC C)

SET(COMPILER_FLAGS "-fsanitize=address,undefined")

add_executable(${PROJECT_NAME} process1.c
                               IPCHelper.c)
 
target_compile_options(${PROJECT_NAME} PRIVATE ${COMPILER_FLAGS})
target_link_options(${PROJECT_NAME} PRIVATE ${COMPILER_FLAGS})  