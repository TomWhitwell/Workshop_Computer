.section .rodata.sample, "a"
.balign 4
.global sample_data
.global sample_data_end
sample_data:
#if FOUR33_SAMPLE_RATE == 24000
    .incbin "433_loop_24k_s16_stereo.bin"
#else
    .incbin "433_loop_10k_s8_stereo.bin"
#endif
sample_data_end:
.byte 0

.balign 4
.global seat_creak_data
.global seat_creak_data_end
seat_creak_data:
    .incbin "seat_creak_12k_s8_stereo.bin"
seat_creak_data_end:
.byte 0
