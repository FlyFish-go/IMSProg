cmake_minimum_required(VERSION 3.10.0)
project(IMSProg LANGUAGES CXX C)

add_subdirectory("IMSProg_programmer")
if(NOT WIN32)
    add_subdirectory("IMSProg_editor")
    add_subdirectory("IMSProg_database_update")
endif()
