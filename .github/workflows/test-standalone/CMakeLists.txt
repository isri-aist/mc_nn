cmake_minimum_required(VERSION 3.22)
project(mcnn_consumer LANGUAGES CXX)

find_package(mc_nn REQUIRED)

add_executable(mcnn-consumer src/main.cpp)
target_compile_features(mcnn-consumer PRIVATE cxx_std_17)
target_link_libraries(mcnn-consumer PRIVATE mc_nn::MCNN)
install(TARGETS mcnn-consumer RUNTIME DESTINATION bin)
