# Fail the build if the firmware image would run into the user sample region.
#
# See samplestore.h: user samples live in the last 1MB of a 2MB flash, at a
# FIXED address so reflashing the firmware does not wipe them. If code + baked
# samples reach that address the two overwrite each other — flashing would
# destroy uploaded samples, and an upload would destroy the firmware.
#
# Reads the highest flash address actually used from the ELF's section headers,
# which is the real end of the image (the .bin is not always generated).
if (NOT EXISTS ${ELF})
  message(FATAL_ERROR "checksize: ${ELF} not found")
endif()

execute_process(COMMAND ${OBJDUMP} -h ${ELF} OUTPUT_VARIABLE _hdrs)

set(_end 0)
# Section header lines look like:  Idx Name Size VMA LMA File-off Algn
string(REGEX MATCHALL "[0-9a-f]+  10[0-9a-f]+  10[0-9a-f]+" _rows "${_hdrs}")
foreach (_row IN LISTS _rows)
  string(REGEX MATCH "^([0-9a-f]+)  10([0-9a-f]+)" _m "${_row}")
  if (_m)
    math(EXPR _sz  "0x${CMAKE_MATCH_1}")
    math(EXPR _lma "0x${CMAKE_MATCH_2}")
    math(EXPR _top "${_lma} + ${_sz}")
    if (_top GREATER ${_end})
      set(_end ${_top})
    endif()
  endif()
endforeach()

if (_end EQUAL 0)
  message(WARNING "checksize: could not read section headers; boundary unchecked")
  return()
endif()

if (_end GREATER_EQUAL ${LIMIT})
  math(EXPR _over "${_end} - ${LIMIT}")
  message(FATAL_ERROR
    "Firmware reaches ${_end} bytes and overruns the user sample region at "
    "${LIMIT} by ${_over} bytes.\n"
    "Shorten or drop some samples/*.raw, or move kUserRegionOff in "
    "samplestore.h (which invalidates samples already uploaded to a card).")
endif()

math(EXPR _kb "(${LIMIT} - ${_end}) / 1024")
message(STATUS "Firmware ends at ${_end}; ${_kb} KB clear of the user region")
