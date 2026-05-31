add_subdirectory(common)
add_subdirectory(ingestion)
add_subdirectory(order_book)
add_subdirectory(main)

if(BUILD_PYTHON_BINDINGS)
    add_subdirectory(python)
endif()
