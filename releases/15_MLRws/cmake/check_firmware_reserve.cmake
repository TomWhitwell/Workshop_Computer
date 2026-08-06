if(NOT DEFINED BIN_FILE)
	message(FATAL_ERROR "BIN_FILE not set")
endif()

if(NOT DEFINED FIRMWARE_RESERVE)
	message(FATAL_ERROR "FIRMWARE_RESERVE not set")
endif()

file(SIZE "${BIN_FILE}" BIN_SIZE)

if(BIN_SIZE GREATER FIRMWARE_RESERVE)
	message(FATAL_ERROR
		"Firmware binary (${BIN_SIZE} bytes) exceeds reserved flash (${FIRMWARE_RESERVE} bytes). "
		"Increase MLR_FIRMWARE_RESERVE_BYTES or reduce firmware size before flashing.")
endif()

math(EXPR FIRMWARE_HEADROOM "${FIRMWARE_RESERVE} - ${BIN_SIZE}")
message(STATUS
	"Firmware flash check passed: ${BIN_SIZE} bytes used, ${FIRMWARE_HEADROOM} bytes headroom within ${FIRMWARE_RESERVE}-byte reserve")