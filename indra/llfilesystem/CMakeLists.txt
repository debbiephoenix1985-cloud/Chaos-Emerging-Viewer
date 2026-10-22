# -*- cmake -*-

project(llfilesystem)

include(00-Common)
include(LLCommon)
include(Boost)

set(llfilesystem_SOURCE_FILES
	lldir.cpp
	lldiriterator.cpp
	lldiskcache.cpp
	llfilesystem.cpp
	)

set(llfilesystem_HEADER_FILES
	CMakeLists.txt

	lldir.h
	lldiriterator.h
	lldiskcache.h
	llfilesystem.h
	)

if (DARWIN)
	list(APPEND llfilesystem_SOURCE_FILES lldir_macos.cpp)
	list(APPEND llfilesystem_SOURCE_FILES lldir_macos_objc.mm)
	list(APPEND llfilesystem_HEADER_FILES lldir_macos_objc.h)
endif (DARWIN)

if (LINUX)
	list(APPEND llfilesystem_SOURCE_FILES lldir_linux.cpp)
endif (LINUX)

if (WINDOWS)
	list(APPEND llfilesystem_SOURCE_FILES lldir_windows.cpp)
endif (WINDOWS)

set_source_files_properties(${llfilesystem_HEADER_FILES}
							PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND llfilesystem_SOURCE_FILES ${llfilesystem_HEADER_FILES})

add_library (llfilesystem ${llfilesystem_SOURCE_FILES})
add_dependencies(llfilesystem prepare)

if (DARWIN)
  find_library(COCOA_LIBRARY Cocoa)
endif (DARWIN)
target_link_libraries(llfilesystem
                      ${COCOA_LIBRARY}
                      ${BOOST_FILESYSTEM_LIBRARY})

if (USEUNITYBUILD)
	set_target_properties(llfilesystem PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 0)
else (USEUNITYBUILD)
	target_precompile_headers(llfilesystem PRIVATE <linden_common.h>)
endif (USEUNITYBUILD)

get_directory_property(ALLDEFINES COMPILE_DEFINITIONS)
message("llfilesystem COMPILE_DEFINITIONS = ${ALLDEFINES}")
