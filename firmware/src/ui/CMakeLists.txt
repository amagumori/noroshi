
target_include_directories(noroshi PRIVATE .)
file(GLOB UI_FILES "*.c")
target_sources(noroshi PRIVATE ${UI_FILES} )
