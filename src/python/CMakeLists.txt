find_package(Python COMPONENTS Interpreter Development REQUIRED)

pybind11_add_module(backtester_cpp
    bindings.cpp
    OrderAPI.cpp
    BacktestEngine.cpp
)

target_link_libraries(backtester_cpp PRIVATE
    back-tester-common
    back-tester-ingestion
    back-tester-order-book
)

# Output alongside the Python package so it can be imported directly
set_target_properties(backtester_cpp PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/python/backtester"
)
