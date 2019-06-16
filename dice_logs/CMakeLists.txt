cmake_minimum_required(VERSION 3.5)
project(arsp VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ion.cdt)

### Generate the wasm and abi
add_contract(dice_logs dice_logs dice_logs.cpp)
