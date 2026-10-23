// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <atomic>
inline void __dmb(){std::atomic_thread_fence(std::memory_order_seq_cst);}
inline uint32_t save_and_disable_interrupts(){return 0;}
inline void restore_interrupts(uint32_t){}
