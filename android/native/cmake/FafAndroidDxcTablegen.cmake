# Script (cmake -P) for faf_dxc_tablegen's install step (FafAndroidDxc.cmake): copies DXC's
# llvm-tblgen and clang-tblgen from the host build FROM, made by a single-configuration generator
# (bin/) or a multi-configuration one (Release/bin/), to TO. SUFFIX is the host's executable suffix.

foreach(_tool IN ITEMS llvm-tblgen clang-tblgen)
  set(_found "")
  foreach(_dir IN ITEMS "${FROM}/bin" "${FROM}/Release/bin")
    if(EXISTS "${_dir}/${_tool}${SUFFIX}")
      set(_found "${_dir}/${_tool}${SUFFIX}")
      break()
    endif()
  endforeach()
  if(NOT _found)
    message(FATAL_ERROR "${_tool}${SUFFIX} is not in ${FROM}/bin or ${FROM}/Release/bin")
  endif()
  file(MAKE_DIRECTORY "${TO}")
  file(COPY_FILE "${_found}" "${TO}/${_tool}${SUFFIX}" ONLY_IF_DIFFERENT)
endforeach()
