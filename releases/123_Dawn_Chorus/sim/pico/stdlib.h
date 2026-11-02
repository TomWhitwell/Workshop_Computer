#pragma once
#include <cstdint>
#include <cstdio>
#define __not_in_flash_func(x) x
#define PICO_ERROR_TIMEOUT -1
#ifndef SYS_CLK_HZ
#define SYS_CLK_HZ 200000000u
#endif
inline uint64_t time_us_64(){return 0;}
inline int getchar_timeout_us(int){return -1;}
