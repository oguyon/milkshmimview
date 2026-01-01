#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "ImageStreamIO::ImageStreamIO" for configuration ""
set_property(TARGET ImageStreamIO::ImageStreamIO APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(ImageStreamIO::ImageStreamIO PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libImageStreamIO.so"
  IMPORTED_SONAME_NOCONFIG "libImageStreamIO.so"
  )

list(APPEND _cmake_import_check_targets ImageStreamIO::ImageStreamIO )
list(APPEND _cmake_import_check_files_for_ImageStreamIO::ImageStreamIO "${_IMPORT_PREFIX}/lib/libImageStreamIO.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
