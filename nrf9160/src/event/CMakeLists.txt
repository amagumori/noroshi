
target_include_directories(noroshi PRIVATE .)
file(GLOB EVENT_FILES "*.c")
target_sources(noroshi PRIVATE ${EVENT_FILES} )
