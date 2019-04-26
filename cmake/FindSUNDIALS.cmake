include(FindPackageHandleStandardArgs)

find_path(SUNDIALS_INCLUDE_DIR
  sundials_config.h
  HINTS ENV SUNDIALS_DIR
  PATH_SUFFIXES include include/sundials
  DOC "SUNDIALS Directory")

if (SUNDIALS_INCLUDE_DIR)
  message(STATUS "Found SUNDIALS include directory: ${SUNDIALS_INCLUDE_DIR}")
else()
  message(STATUS "Could not find SUNDIALS include directory")
endif()

set(SUNDIALS_INCLUDE_DIRS
  "${SUNDIALS_INCLUDE_DIR}"
  "${SUNDIALS_INCLUDE_DIR}/..")

find_library(SUNDIALS_nvecparallel_LIBRARY
  NAMES sundials_nvecparallel
  HINTS "${SUNDIALS_INCLUDE_DIR}/../.."
  PATH_SUFFIXES lib lib64
  )

if (SUNDIALS_nvecparallel_LIBRARY)
  list(APPEND SUNDIALS_LIBRARIES "${SUNDIALS_nvecparallel_LIBRARY}")
endif()
mark_as_advanced(SUNDIALS_nvecparallel_LIBRARY)

set(SUNDIALS_COMPONENTS arkode cvode ida)
set(SUNDIALS_INT_TYPES int64_t;"long long";long;int32_t;int;)

foreach (LIB ${SUNDIALS_COMPONENTS})
  find_library(SUNDIALS_${LIB}_LIBRARY
    NAMES sundials_${LIB}
    HINTS "${SUNDIALS_INCLUDE_DIR}/../.."
    PATH_SUFFIXES lib lib64
    )

  if (SUNDIALS_${LIB}_LIBRARY)
    list(APPEND SUNDIALS_LIBRARIES "${SUNDIALS_${LIB}_LIBRARY}")
  endif()
  mark_as_advanced(SUNDIALS_${LIB}_LIBRARY)
endforeach()

find_package_handle_standard_args(SUNDIALS
  REQUIRED_VARS SUNDIALS_LIBRARIES SUNDIALS_INCLUDE_DIR SUNDIALS_INCLUDE_DIRS)

mark_as_advanced(SUNDIALS_LIBRARIES SUNDIALS_INCLUDE_DIR SUNDIALS_INCLUDE_DIRS)

if (SUNDIALS_FOUND AND NOT TARGET SUNDIALS::SUNDIALS)
  add_library(SUNDIALS::NVecParallel UNKNOWN IMPORTED)
  set_target_properties(SUNDIALS::NVecParallel PROPERTIES
    IMPORTED_LOCATION "${SUNDIALS_nvecparallel_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${SUNDIALS_INCLUDE_DIRS}")

  foreach (LIB ${SUNDIALS_COMPONENTS})  
    add_library(SUNDIALS_${LIB}::${LIB} UNKNOWN IMPORTED)
    target_link_libraries(SUNDIALS_${LIB}::${LIB} INTERFACE SUNDIALS::NVecParallel)
    set_target_properties(SUNDIALS_${LIB}::${LIB} PROPERTIES
      IMPORTED_LOCATION "${SUNDIALS_${LIB}_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${SUNDIALS_INCLUDE_DIRS}")
  endforeach()
endif()