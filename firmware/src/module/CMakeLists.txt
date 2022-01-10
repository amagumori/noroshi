
target_include_directories(noroshi PRIVATE .)
file(GLOB MODULE_FILES "*.c")
target_sources(noroshi PRIVATE ${MODULE_FILES} )
