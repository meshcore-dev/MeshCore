# MeshCore "minimal" Zephyr sample.
#
# Brings up the mesh core with the Zephyr platform helpers and a loopback radio,
# proving the engine compiles and runs on a Zephyr target with no external
# radio hardware or extra dependencies.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshcore_mesh_min)

target_sources(app PRIVATE src/main.cpp)

# The MeshCore module publishes its own include paths via zephyr_include_directories.
