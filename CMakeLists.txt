cmake_minimum_required(VERSION 3.9)
project(xthi LANGUAGES C)

set(CMAKE_C_STANDARD 99)

# Let's be really pernickety
add_compile_options(-Wall)
if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wextra -Wpedantic)
endif()

find_package(OpenMP REQUIRED)
find_package(MPI REQUIRED)

add_executable(xthi xthi.c)
target_link_libraries(xthi PUBLIC MPI::MPI_C OpenMP::OpenMP_C $<$<PLATFORM_ID:Linux>:numa>)

add_executable(xthi.nompi xthi.c)
target_compile_definitions(xthi.nompi PUBLIC NO_MPI=1)
target_link_libraries(xthi.nompi PUBLIC OpenMP::OpenMP_C $<$<PLATFORM_ID:Linux>:numa>)
