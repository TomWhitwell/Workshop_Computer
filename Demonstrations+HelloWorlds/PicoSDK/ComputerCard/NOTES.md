# ComputerCard programming tips
This document is a collection of short tips for ComputerCard development. It's split into two sections, the first about squeezing as much performance as possible from the RP2040 microcontroller in the Workshop System Computer, and the second about algorithms that are handy for audio development on this platform.

# Programming and optimisation
## Clock speed
By default the RP2040 cores run at 125MHz, with an maximum supported speed of 200MHz (originally 133MHz at release).


As described below, clock speeds that are are multiples of 48MHz minimise noise on the audio inputs, and so a rate of 144MHz, 192MHz or 240MHz is recommended. These can be set with

```
	set_sys_clock_khz(144000, true); // officially supported at default voltage
```
```
	vreg_set_voltage(VREG_VOLTAGE_1_15); // officially supported at 1.15V
	set_sys_clock_khz(192000, true);
```
```
	vreg_set_voltage(VREG_VOLTAGE_1_20); // overclock, not officially supported
	set_sys_clock_khz(240000, true);     // Works on some (but not all) Computers
```

at the start of `main()`, which requires the headers
```
#include "hardware/clocks.h"
#include "hardware/vreg.h"
```

The Workshop System Computer seems less amenable to overclocking than an RPi Pico - possibly because the flash memory on program cards is further from the microcontroller. 

### Clock speed and ADC noise
The RP2040 ADC in the workshop computer is somewhat sensitive to what the RP2040 is doing. I'm not sure if this internal crosstalk within the RP2040, or the fluctuating power requirements of the RP2040 causing interference external to the RP2040. This is most noticable in the audio inputs.

Any periodic signals that are not synchronised to the 48kHz sample clock are liable to alias into the audio signal. These periodic signals include: the clock speed of the RP2040, the interrupts that handle sigma-delta modulation of CV PWM outputs, and any periodic code running on the non-ComputerCard core.

To minimise these:
* In all versions of ComputerCard, choose a clock frequency that exactly multiplies the Nyquist frequency of 24kHz. 144MHz, 192MHz are sensible choices, or 240MHz (overclock) if needed.
* In ComputerCard 0.2.6 - 0.2.8, the CV out PWM runs at the clock frequency divided by 2048. No clock frequency achievable on the RP2040 makes this a multiple of 24kHz, so these versions have aliased tones in the audio input.
* In ComputerCard 0.3.x, the CV out PWM runs at the clock frequency divided by 2000. For clock frequencies 48MHz, 96MHz, 144MHz, 192MHz and 240MHz this PWM frequency, and the clock frequency itself, exactly multiply 24kHz and so is alias-free.

## Assembly language
For micro-optimisations it can be helpful to see what machine code instructions the C/C++ is compiling to. The Cortex M0+ cores in the RP2040 implement the ARMv6-M architecture, which has a fairly simple instruction set. The important information for understanding the architecture is in:
* §2.4.3 of [RP2040 datasheet](https://pip-assets.raspberrypi.com/categories/814-rp2040/documents/RP-008371-DS-1-rp2040-datasheet.pdf)
* ARM's online [instruction set summary and timings](https://developer.arm.com/documentation/ddi0484/c/Programmers-Model/Instruction-set-summary) from the Cortex M0+ manual
* [details of each instruction](https://developer.arm.com/documentation/ddi0419/c/Application-Level-Architecture/The-ARMv6-M-Instruction-Set) in the ARMv6-M manual
* Function calling conventions in the [ARM 32-bit ABI](https://github.com/ARM-software/abi-aa/blob/2982a9f3b512a5bfdc9e3fea5d3b298f9165c36b/aapcs32/aapcs32.rst)

A beginners guide to the above is the book 'RP2040 Assembly Language Programming: ARM Cortex-M0+ on the Raspberry Pi Pico' by Stephen Smith.

There are two convenient ways to look at the assembly language instructions that are produced by the C/C++ compiler. Firstly, alongside the usual compiled binary formats (`.uf2`, `.bin` etc) the RPi Pico SDK also outputs a disassembly file `.dis` in the `build/` directory, containing the full assembly language output of the program. This output contains all of the code that will be programmed to on the flash card, and so is a convenient way to see what calls into the Pico SDK library (including 'hidden' ones such as the sofware floating point implementation) are actually doing.

Secondly, the online tool [Compiler Explorer](https://godbolt.org/) is helpful for exploring how particular C/C++ constructs compile to assembly language. To generate RP2040 code, use the compiler `ARM GCC xxx (unknown eabi)` with flags `-mthumb -mcpu=cortex-m0 -O2`. Compiler Explorer doesn't know about the RPi Pico SDK or the specifics of the RP2040, so is best for looking at how snippets of user code compile to machine code.

## Memory
The memory map is described in [the datasheet](https://pip-assets.raspberrypi.com/categories/814-rp2040/documents/RP-008371-DS-1-rp2040-datasheet.pdf) §2.2 and §2.6.

The most relevant areas for ComputerCard programming are:
| Area                     | Start address               | Size        |
|--------------------------|-----------------------------|-------------|
| Flash program card (XIP) | `0x10000000`                 | 16MB or 2MB |
| RAM (striped)            | `0x20000000`                 | 256kB       |
| RAM (scratch X)          | `0x20040000`                 | 4kB       |
| RAM (scratch Y)          | `0x20041000`                 | 4kB       |

The data on the flash program card is mapped to address `0x10000000` onwards.

There are several banks of RAM which can be accessed in a variety of ways. Only one part of the chip (e.g. ARM core, DMA access) can access a bank at once, with wait cycles introduced if there is contention for access to a bank. The default strategy for optimising bank access is for the stack of each M0+ core to live in a dedicated 4kB 'scratch' bank and to combine the remaining four 64kB banks into one 'striped' bank starting at `0x20000000`, which contains static data, code that is explicitly copied to RAM, and the heap. Much more detailed information about the allocation for a particular program is available at the top of the `.dis` disassembly file generated in the CMake `build/` directory.

### Heap, stack and stack overflow
As described in this [blog post](https://petewarden.com/2024/01/16/understanding-the-raspberry-pi-picos-memory-layout/), the stack for the default core 0 starts at the top of scratch bank Y at `0x20041FFF`, and grows downwards. The heap starts near the bottom of the striped RAM section (but above bss/data sections that store global variables, and any code copied to RAM) and grows upwards from (e.g.) `0x20001000`. For single-core operation, the stack of core 0 is free to grow downwards, through the scratch X bank, and into the striped RAM block, so long as it does not crash into the top of the heap. When the second core is in use, the stack for this core starts at the top of scratch X, `0x20040FFF`, and grows downwards. The stack for core 0 must then not exceed 4kB.

Where stack over flow is caused by a few large arrays, the usual solution is to either allocate these on the heap (with `malloc` or `new`) or as global data (in which case they live in the data or bss section). 

In the context of ComputerCard, it's worth noting that
```
int main()
{
	MyComputerCard mc;
    mc.Run();
}
```
places the `mc` object on the stack, and so means that, if the second core is to be used, `MyComputerCard` instances must be somewhat smaller than 4kB, . 
By contrast,
```
MyComputerCard mc;

int main()
{
    mc.Run();
}
```
places `mc` as a global variable nar the start of the 256kB RAM block, so large statically allocated arrays can be used as members of `MyComputerCard`.

There are a handful of compile-time and run-time tools to help monitor stack usage:
* Defining `PICO_USE_STACK_GUARDS=1` within a `target_compile_definitions` command in the `CMakeLists.txt` enables a hardware memory protection unit in the RP2040 for lowest 32 bytes in scratch X and Y, causing an immediate crash when the top of the stack is reached, rather than allowing (potentially harder to debug) memory corruption to occur.
* Adding `-Wstack-usage=<num_bytes>` to the `target_compile_options` in the `CMakeLists.txt` generates a compile-time warning for any function whose stack usage exceeds `<num_bytes>`.
* Adding `-fstack-usage` to the `target_compile_options` in the `CMakeLists.txt` generates `.su` files within the build directory (`build/CMakeFiles/<projectname>.dir/...`) which show the stack usage for all functions.

### Heap allocation

The default heap allocator is from [newlib](https://github.com/eblot/newlib/blob/master/newlib/libc/stdlib/mallocr.c). This is usually fine for allocating big buffers that just need to be out of the stack space, but my experience has been that repeated or small-object heap allocation (`malloc`/`free` or `new`/`delete`) is not as optimised as one might like on a memory-constrained platform. In particular, this means avoiding libraries that make use of this (such as C++ `<string>`, `<vector>` etc.). The usual advice for small embedded systems is to structure programs to avoid repeated `malloc`/`free`, or using a simple [custom allocator](https://en.wikipedia.org/wiki/Region-based_memory_management) that has greater knowledge of object size and lifetime than a generic heap allocator. 

## Running code from RAM

By default, the RP2040 runs program code directly from the flash chip on the program card (so-called 'execute in-place', or XIP), but can also run code stored in its internal RAM. Reading from the flash chip is more than an order of magnitude slower than reading from RAM, and this has a corresponding impact on the speed at which code stored in flash can runs.

The situation is mitigated by a caching: data read from the flash card is stored in 16kB of dedicated RAM, which allows frequently-used code to be accessed at RAM speeds. For many small ComputerCard programs, this cache is sufficient to store all or nearly all the code executed every sample, and execution speed is nearly that of a program stored entirely in RAM. For larger programs, which don't fit into the cache, or those with particularly tight timing requirements, it is necessary to explicitly copy the code to RAM and run it from there.

The easiest option is to copy *all* program code to RAM before it is executed. In the RPi Pico SDK, this can be done by addding
`pico_set_binary_type(${PROJECT_NAME} copy_to_ram)`
to the `CMakeLists.txt` file.
Obviously this is only possible if the total code (and constant data) size, plus any RAM used to store data during the course of the program, is smaller than the ~256kB of available RAM. The linker will produce an error if this is not the case.
	
Another option, which is used in ComputerCard itself, is to decorate functions and class methods that should be stored in RAM with ` __time_critical_func`, e.g.
```c++
int32_t MyFunction(int32_v a) {...}
```
becomes
```c++
int32_t __time_critical_func(MyFunction)(int32_v a) {...}
```

There is a small overhead in jumping between code executed in RAM and in flash, so my approach has been to liberally apply `__time_critical_func` to all frequently-run code. For example, in ComputerCard, `ProcessSample` and all the methods likely to be called within it (`KnobVal`, `CVOut1`, etc.) are all specified as running from RAM. This process may require going into any and all library code called from `ProcessSample` to apply `__time_critical_func` to methods there.

N.B. `__time_critical_func` currently does the same as `__not_in_flash_func` (which is used in ComputerCard), but the SDK indicates that `__time_critical_func` is preferred and may have further optimisations in future.


The `.dis` disassembly files generated in the CMake `build/` directory is a useful way to verify which code will run from RAM and which from flash/cache, as it show the address in memory at which each assembly language instruction will be stored.
Addresses are eight-digit hexadecimal values that start each line of the disassembly file, with those starting `1xxxxxxx` correspond to data stored in flash, and those starting `2xxxxxxx` correspond to data stored in RAM.

Where cache or RAM size limits force some code to be run directly from flash, the first few passes through a loop can be slower than subsequent passes, where the XIP cache is better filled. In the Radio Music source code, certain CPU-intensive parts of the algorithm are turned off for the first few samples, so that the initial, slower, code execution does not cause a buffer underflow.

## Integer types

Because integer types are so much faster than (software-emulated) floating-point on the RP2040, most variables in a ComputerCard program will be integers.

**I tend to use the signed 32-bit integer `int32_t` (defined in the `<cstdint>` header) for most variables.**

A simple `int` can be used instead, which on the RP2040 is also a signed 32-bit integer.

### Why not unsigned types?

Many people advise using unsigned types (e.g. `uint32_t`) for variables that can never be negative. These do have the advantage of double the positive range (`uint32_t` ranges between 0 and 4.2 billion, compared to ±2.1 billion for `int32_t`).
However, operations on mixed signed and unsigned types in C++ can be confusing, thanks to [integer conversion and promotion rules](http://en.cppreference.com/w/c/language/conversion.html).

These rules say that if a mathematical operation such as `a + b` is performed on two integer operands, and
- both operands are of the same size as `int` (32-bit) or larger.
- one operand is signed (e.g. `int32_t`) and the other unsigned (e.g `uint32_t),
  
then *the operand with the signed type is implicitly converted to the unsigned type*. This means that the program
```c++
int32_t  a = -10;
uint32_t b = 2;

int32_t c = a / b;
```
sets `c` not to -5, as might be expected, but to 2147483643. (`a` is implicitly converted to an `unsigned int` with value $2^{32} - 10$, which is then divided by 2.) 
This type of bug can be sufficiently hard to find and fix that I prefer almost all variables to be signed.

### Why not smaller types?
Many variables don't need the full range of 32-bit numbers and it's tempting to think that smaller 16- or 8-bit types might be faster. 
In fact, the opposite can be true. Compare for example a simple function to add two 32-bit numbers

```c++
#include <cstdint>
int32_t sum(int32_t a, int32_t b)
{
    return a + b;
}
```
which compiles to a two instructions (an addition, and the return)

```asm
sum(int, int):
        adds    r0, r1, r0
        bx      lr
```

The 16-bit equivalent:
```c++
#include <cstdint>

int16_t sum(int16_t a, int16_t b)
{
    return a + b;
}
```
requires an additional instruction `sxth`
```asm
sum(short, short):
        adds    r0, r1, r0
        sxth    r0, r0
        bx      lr
```
following the 32-bit addition `adds`, in order to truncate the result to 16 bits. 

Of course, the smaller types are invaluable when RAM is limited, in long arrays or audio buffers.


## Speed of mathematical operations

It's difficult to benchmark operations in isolation because their speed depends on the context of the surrounding code. The timings here, all at 200MHz, are therefore very approximate.

On the RP2040, 32-bit `+`, `-`, `*`, bitshifts (`<<`, `>>`) and bitwise operators (`|`, `&`, `^`) are fast single-cycle instructions (though loading operands may well take several more cycles). A single cycle at 200MHz is 5ns.

All other operations are emulated by functions, which have some function call overhead (they are not, as far as I can tell, inlined).
- 32-bit division and modulus `/` and `%` are handled by an 8-cycle hardware divider in the RP2040, and take ~120ns. In principle, functions in the Pico SDK `hardware_divider` header, with reduced function call overhead might speed this up.
- For 64-bit integers, `+` and `-` take ~50ns, `*` takes ~175ns and `/` and `%` take ~250ns.
- Single-precision floating-point operations (`+`, `-`, `*`, `/`) are of the order 360ns, with (single precision) floating point functions like `sinf` and `expf` taking nearly 3µs. Timings for these are given in the RP2040 datasheet §2.8.3.2.2.

The executive summary is: wherever performance really matters, stick with 32-bit integer `+`, `-`, `*`, bitshifts and bitwise operators, as far as possible.

### Operations vs loads/stores
As noted above, the time taken to load operands into registers may itself take several cycles. The two random number generation algorithms discussed below make for an interesting comparison.

The linear congruential generator `lcg_u32` has only two integer operations, `*` and `+`, each done in a single cycle with `muls` and `add` instructions respectively. But the expression requires two integer constants, each requiring a two-cycle load instruction `ldr`. The result is a 14-cycle execution, of which only 2 cycles are the underlying mathematical operations.
```asm
100002d4 <_Z7lcg_u32v>:
100002d4:	4b04      	ldr	r3, [pc, #16]	@ (100002e8 <_Z7lcg_u32v+0x14>)
100002d6:	4805      	ldr	r0, [pc, #20]	@ (100002ec <_Z7lcg_u32v+0x18>)
100002d8:	681a      	ldr	r2, [r3, #0]
100002da:	4350      	muls	r0, r2
100002dc:	4a04      	ldr	r2, [pc, #16]	@ (100002f0 <_Z7lcg_u32v+0x1c>)
100002de:	4694      	mov	ip, r2
100002e0:	4460      	add	r0, ip
100002e2:	6018      	str	r0, [r3, #0]
100002e4:	4770      	bx	lr
100002e6:	46c0      	nop			@ (mov r8, r8)
100002e8:	20000f5c 	.word	0x20000f5c
100002ec:	0019660d 	.word	0x0019660d
100002f0:	3c6ef35f 	.word	0x3c6ef35f
```
By contrast, `xorshift32` has six operations - three XOR and three bitshifts, executed with single-cycle `eors` and `lsls`, but requires only the load and store of the `static` state variable (which is also required by `lcg_u32`). This executes in 12 cycles, marginally quicker than `lcg_u32`.
```asm
100002f4 <_Z10xorshift32v>:
100002f4:	4904      	ldr	r1, [pc, #16]	@ (10000308 <_Z10xorshift32v+0x14>)
100002f6:	680b      	ldr	r3, [r1, #0]
100002f8:	035a      	lsls	r2, r3, #13
100002fa:	405a      	eors	r2, r3
100002fc:	0c53      	lsrs	r3, r2, #17
100002fe:	4053      	eors	r3, r2
10000300:	0158      	lsls	r0, r3, #5
10000302:	4058      	eors	r0, r3
10000304:	6008      	str	r0, [r1, #0]
10000306:	4770      	bx	lr
10000308:	20000f58 	.word	0x20000f58
```
Such timings will change if the function calls are inlined. In particular, successive calls to the linear congruential generator `lcg_u32` can be made much more quickly if the function is inlined and the constants are retained in registers between successive calls. That could be useful for, for example, rapidly filling an audio buffer with white noise. 

### Division using right shifts
Divisions by powers of two can be calculated by right shifts (`x >> n` divides `x` by $2^n$). For unsigned integers, right shift and divide by power of two are identical, but for signed integers the rounding behaviour differs for negative numbers. Specifically, divide `/` always rounds towards zero, whereas right shift `>>` rounds towards negative infinity.


Where approximate results are acceptable, divisions by non-powers of two (including non-integer divisors) can be approximated by a combination of a multiply and bitshift. For example,
```c++
int32_t y = x / 5;
```
could sometimes be approximated by
```c++
int32_t y = (x * 3277) >> 14;
```
since $3277/2^{14} = 0.20001\ldots \approx \tfrac{1}{5}$. This of course limits `x` to values for which `x * 3277` does not overflow.


### Fast multiplication
The `muls` instruction multiplies two (32-bit) registers and returns the *least significant* 32 bits of the result. As a result, from a single `muls`, we can only get the most significant bits of a multiplication by ensuring that the result does is no greater than can be stored than 32 bits (or 31 bits, for signed integers). This allows combinations such as 16×16-bit multiply (unsigned), or 15×15-bit (signed), or some unequal 8×24-bit (unsigned). This is usually acceptable on the workshop system, where most audio and CV values are 12-bit.

Within C/C++, we can extract the *most* significant 32 bits of a 32×32-bit multiply by putting the two values to be multiplied `int64_t` variables, and then selecting the upper 32 bits of the result. This requires a full 64-bit multiply and is fairly slow (~175ns). But if we don't need this full 64-bit multiply, we can roll our own, for example this routine returning the uppermost 32-bits of a signed 32×16-bit multiply:
```c++
int32_t mul_s32_s16(int32_t a, int32_t b) // b could as well be an int16_t here
{
	int32_t al = a & 0xFFFF;
	int32_t ah = a >> 16;
	return (ah * b) + ((al * b) >> 16);
}
```
which compiles to only 8 instructions (40ns), and fewer if inlined.
```asm
mul_s32_s16(long, long):
        lsls    r3, r0, #16
        lsrs    r3, r3, #16
        muls    r3, r1
        asrs    r0, r0, #16
        muls    r0, r1
        asrs    r3, r3, #16
        adds    r0, r3, r0
        bx      lr
```

#### The interpolator
The RP2040 contains a two specialised interpolator units per CPU core, detailed in the RP2040 datasheet section 2.3.1.6, which perform a series of mathematical operations in one clock cycle. When in *blend mode*, these have the capability of performing an 8×32-bit multiply and returning the most significant 32-bits of the result.


## Writing to flash
The RP2040 can write data to the flash program card as well as read it. Writing to flash is used primarily for saving small amounts of data as a settings file that persists across reboots, but can also save larger amounts of data, e.g. streaming audio data onto flash. 

On the RP2040, the flash program card is divided into a heirarchy of 64kB *blocks*, which are divided into sixteen 4kB *sectors*, which in turn are divided into sixteen 256-byte *pages*. 

There are two steps to writing data to flash memory: erasing and programming. These are performed by the functions `flash_range_erase` and `flash_range_program`, respectively, in the `hardware_flash` Pico SDK library.

* Erasing is a slow process, which can only be performed on one or more entire (4096-byte) sectors at once. Erasing turns all bits in those sectors to a `1`. Erasing a single 4kB sector on a program card takes about 5ms (~80kB/s), but erasing all sixteen sectors in a block is proportionally faster, taking about 35ms (~180kB/s).
* Programming is much faster than erasing, and is performed on one or more (256-byte) pages at once. Programming turns selected bits in those pages to `0`.


### Managing flash reads and writes
It is not possible to read data from flash while also erasing or programming it. Because the RP2040 is almost always reading program instructions from flash in order to execute them, writing to flash requires some care, to ensure these reads do not clash with the ongoing write. Such a clash will halt the RP2040.

The `flash_range_erase` and `flash_range_program` functions call lower-level flash writing routines stored in RP2040's built-in boot ROM, meaning that they do themselves cause any flash reads. However, there are two ways in which flash reads and subsequent crashes could occur. Firstly, if interrupts are enabled, it's possible for an interrupt handler stored in flash to be requested during a flash write. Secondly, if both cores are in use, then the core that is not running `flash_range_erase`/`flash_range_program` may well be reading instructions and other data from flash.

The simplest way to avoid these problems is to run the entire program from RAM (using `pico_set_binary_type(... copy_to_ram)`, as detailed above), which allows the flash to be written to at any time. (The `copy_to_ram` binary option copies both code and read only variables, such as constant arrays of predefined data, to RAM, so reading from such arrays does not result in flash reads.)

Many programs, though, are too big to fit in the RP2040's RAM. In that case, to avoid flash reads:
* code that uses interrupts must call `save_and_disable_interrupts` before either flash writing function is called, and `restore_interrupts` after the write has finished. These are in the Pico SDK `hardware_sync` library.
* code that uses multiple cores must prepare the non-flash-writing core to be paused, using `multicore_lockout_victim_init`, then pause and restore this core either side of flash writing functions, with `multicore_lockout_start_blocking` and `multicore_lockout_end_blocking`, all from the `pico_multicore` Pico SDK library.

When using ComputerCard, the writes to flash will almost always be made on the core that is *not* executing `ProcessSample()`. If the entire code is copied to RAM, this allows `ProcessSample()` to continue executing without interruption and audio processing continues during the flash write. But if for whatever reason the code cannot be copied to RAM, the audio will glitch for at least 5ms as flash is erased. This happens, for example, in Reverb+ when settings are saved to the program card. 

### Flash lifespan and wear levelling
Flash memory degrades very slowly as each time it is erased. The memory chips on the Workshop System program cards are rated for at least 100,000 erase/program cycles per sector. For any purpose where the user is manually taking an action (clicking a button, etc.) to save data to flash, this is not likely to be an issue. But in principle, a program that repeated erased and wrote to a single sector of flash, as fast as possible, could reach this in about ten minutes.

When recording audio to flash, the data rate might be as high as 48kHz × 12-bit × 2 channels = 144kB/s. If this audio is recorded in a loop across (say) 1.5MB of a 2MB program card, the loop length will be 1.5MB / 144kB/s = 11s. This part of the program card will then reach its rated life after 11s × 100000 = ~300 hours of continuous recording. For comparison, that's exactly the quoted tape life of a Roland RE-201 Space Echo. The rated life of a 16MB program card (with nearly 2-minute loops) will be ~10× longer. Even if the desired loop length is much shorter than this, it makes sense to continue to record audio onto across all of the free space on the card, to distribute the wear evenly. 


# Algorithms
## (Pseudo-) random numbers

The pseudo-random numbers required for musical purposes do not usually need to be of particularly high statistical quality, but the generator does need to be fast, so that it imposes minimal computational load even if random numbers are generated every audio sample.

I have favoured writing custom random number generators rather than using the C library function `rand()` (from `<stdlib.h>`) or the C++11 generators in `<random>`. This is firstly because the library generators are typically higher quality (and therefore slower) than is necessary, and secondly because details such as the worst-case (as opposed to average) execution time are not clear.

Two fast algorithms on the RP2040 are a 32-bit linear congruential generator:
```c++
uint32_t lcg_u32()
{
	static uint32_t lcg_seed = 1;
	lcg_seed = 1664525 * lcg_seed + 1013904223;
	return lcg_seed;
}
```
and 32-bit xorshift:
```c++
uint32_t xorshift32()
{
	static uint32_t x = 556381194;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}
```
These are of similar speed, about 95ns per call (at 200MHz clock). By comparison, the C library `rand()` and C++ library `std::minstd_rand` both of take around 300ns, and the time available for one call of `ProcessSample()` is around 20,000ns.

I have used variations of `lcg_u32` in Utility Pair and Reverb+, but this algorithm suffers from the problem that the low bits are not very random.
Specifically, the least-significant bit of successive `lgc_u32` calls alternates between `0` and `1`, and the maximum period of each successive bit is only twice that than the last. This can be seen in the binary output of the first 16 calls to `lcg_u32`, where the last, penultimate and antepenultimate bits repeat the patterns `01`, `0110`, and `10110100` (paradiddle!), respectively:
```
00111100100010000101100101101100
01011110100010001000010111011011
10000001000101100000000101111110
10110100011100110011101011000101
00001100111100000110110101100000
01011110100110001100000100111111
11000110010101101101110110010010
10001110011000100101111111001001
00000100001110001110011010010100
10100011101001011010000011100011
01000000000111011001000011100110
01101100001000001111001100001101
10010111001101110111100100001000
11010110010000010100100011000111
00111100001011011110111101111010
11111011000110001011100010010001
```
More significant bits have much better random properties. To generate a 12-bit random number, using the most significant 12 bits (with `lcg_u32() >> 20`) is therefore much more random than using the least significant bits (with `lcg_u32 ( )&0xFFF`).

The `xorshift32` function doesn't suffer from a comparable problem, though as a very simple generator it still fails more sophisticated statistical tests.

`lcg_u32()` is a *full-period* generator, meaning that successive calls will generate all $2^{32}$ values that can be stored in a 32-bit integer, after which the sequence of values repeats. A quirk of `xorshift32` is that it will never generate the value zero, and so has period $2^{32}-1$. In either case, the period (over 4 billion) is likely to be sufficient in a musical context.

Either generator can generate signed random numbers simply by changing the return type to `int32_t`.

### Seeding
In the examples above, the initial *seed* for the random number generation is fixed and arbitrarily chosen.
On the Workshop System, it may often make sense to seed the generator either from 32 bits of the unique flash card identifier (`UniqueCardID()` in ComputerCard), or from a sources of true random 'entropy'. (In either case, the `static` variable definition in the functions above must be moved outside the generation function.)

The Pico SDK provides random number generation functions through the [`pico_rand`](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#group_pico_rand) header. These are too slow for most applications in ComputerCard, but are seeded with random entropy sourced from various parts of the RP2040 hardware. A call to one of the functions in this header (e.g. `get_rand_32`) is a good source of seed for one of the faster pseudo-random generators above, but must be called outside of the `ProcessSample` function, as generating the random entropy takes ~1ms, much longer than the ~20us allowed by `ProcessSample`.

## Euclidean rhythms and sigma-delta modulation

Curiously, the algorithm used to create Euclidean rhythms in Utility Pair is exactly the same as the algorithm used to generate precise 19-bit CV outputs (ComputerCard `CVOutPrecise` functions) from 11 bits of PWM resolution. More to come here...

## Antiderivative antialiasing
When an audio signal is sampled at some sample rate $f_s$, all frequencies in the signal up to the half the sample rate ($f_N=f_s/2$, the Nyquist frequency) are represented in the sampled signal. Any frequencies in the original signal above $f_N$ are reflected into the frequency range $0$ to $f_N$, a phenomenon called [aliasing](https://en.wikipedia.org/wiki/Aliasing), and are mixed together with the signals originally in this frequency band. Once aliasing occurs, it is usually impossible to remove the spurious aliased frequencies from the sampled signal, and so quite a lot of effort is expended in DSP programming to remove or reduce the amplitude of frequencies that would alias, before such aliasing occurs.

Within a DSP algorithm, aliasing can occur only when frequencies greater than $f_N$ are generated. 
Many common transforms of an audio signals (including amplification/attenuatuation, filtering and delay with fixed parameters) only manipulate the amplitude and phase of existing frequencies within a signal, and do so cannot create new frequencies greater that $f_N$. 
However, a number of common processes do generate frequencies greater than $f_N$ and so are at risk of aliasing, including:
1. Applying nonlinear functions (such as soft or hard clipping, wavefolders, etc.) to waveforms
2. Synthesising waveforms containing frequencies greater than $f_N$ -- particularly waveforms with discontinuities such as square and sawtooth
3. Downsampling (e.g. playing back a 48kHz sample faster than realtime, on a system with 48kHz sample rate)

A common technique for minimising aliasing is to run the entire audio system at a high sample rate to increase the Nyquist frequency. This is quite effective if the frequency content of the signal decays with frequency quite quickly above audible range, but requires extremely high sample rates if thi sis not the case.
Alternatively (or in conjunction with this), specific mathematical techniques can be used to reduce aliasing.
One of these is antiderivative antialiasing, which has the advantages of being quite generally applicable (including to all three examples above), and relatively simple and efficient to implement on the RP2040. The Utility Pair Wavefolder, Max/rectifier, and oscillators (VCO, Chords, Supersaw) use this technique.

### Theory
The overall principle behind antiderivative antialiasing is very simple. We start with a contiuous-time representation of our desired signal $f(t)$, which will have frequenciy components greater than $f_N$. We want to sample this at times $t_i = i/f_s$. Simply evaluating $f$ at the sample times, $f(t_i)$ would result in significant aliasing, 

<img width="382" height="285" alt="aa1" src="https://github.com/user-attachments/assets/7ad52250-f4aa-4488-85fd-bc6a3f3d54ff" />

So, we instead evaluate the average of $f$ between the time of this sample and the previous one,

$$ f_i = \frac{1}{t_i - t_{i-1}} \int_{t_{i-1}}^{t_i} f(t) dt.$$

That's it!

Why does this help reduce aliasing? There are at least two ways of thinking about this. One is to think of the averaging process as applying a 'boxcar' averaging filter to $f(t)$, which reduces high-frequency components, before sampling that filtered function.

$$ \hat{f}(t) := f_s \int_{t-1/f_s}^t f(t) dt, $$

$$ f_i = \hat{f}(t_i).$$

Another way is to think of the average as being a combination of a continuous-time integration and a discrete-time differentiation. If we integrate $f(t)$ in the continuous domain to obtain $F(t) = \int f(t) dt$ then the average becomes 

$$ f_i = \frac{F(t_i) - F(t_{i-1})}{t_i - t_{i-1}}, $$

which is a discrete finite-difference derivative of $F$.

This combination of continuous-time integration and discrete-time differentiation reduces aliasing because of the relationship between the Fourier transforms a function and its integral, namely:

$$\tilde{F}(\omega) \sim \frac{\tilde{f}(\omega)}{\omega}.$$

The integrated signal $F(t)$ has frequencies that decay more quickly with amplitude than the original. Sampling the integrated function $F(t)$ to obtain $F(t_i)$ introduces aliased tones, but at lower amplitude than if we had sampled $f(t)$ directly.

<img width="382" height="285" alt="aa2" src="https://github.com/user-attachments/assets/ab9a0265-ac83-4b5a-a9fa-35d66de137bd" />

Finally differentiating the signal (in discrete-time) has the effect of increasing signal amplitudes by a factor proportional to frequency. This transforms the original signal back to $f(t)$, but multiplies the amplitudes of the aliased tones only _by the frequency they have been aliased to_, not the (above-Nyquist) frequency they originated from. The result is diminshed amplitude of aliased frequencies, particularly at frequencies much lower than $f_N$.

<img width="382" height="285" alt="aa3" src="https://github.com/user-attachments/assets/4121890a-7bd4-4a8f-873c-23689bd0b380" />



### Application to nonlinear functions
Application of antiderative antialiasing to nonlinear functions originated with [Parker _et al._ "Reducing the aliasing of nonlienar waveshaping using continuous-time convolution", DAFx-16](https://dafx16.vutbr.cz/dafxpapers/20-DAFx-16_paper_41-PN.pdf).

In the continuous (analogue) domain, have some continuous time-dependent signal $x(t)$ which is going into a waveshaper (wavefolder, distortion or other nonlinearity), which applies the function $f(x)$ to the signal $x$.

In a digital system our input signal is not the continuous function $x(t)$, but rather is a set of samples is sampled at discrete times $t_1, t_2, t_3, \ldots$, taking values $x_1 = x(t_1)$, $x_2 = x(t_2)$, etc. at these times.

A naive digital implementation of the waveshaper would apply our nonlinear function $f$ directly to each input sample $x_i$, and return $f(x_i)$ at sample $i$.
But since a nonlinear function will add harmonics above the frequencies in the original signal $x(t)$, this is likely to cause aliasing.

The idea of antiderivative aliasing is to filter the wavefolded signal $f(x(t))$ by:
* Modelling the original signal $x(t)$ as straight-line segments between the sampled points $(t_1, x(t_1))$, $(t_2, x(t_2))$, etc.
* Filtering the waveshaped signal by, at time $t_i$, returning the average of the continuous signal $f(x(t))$ between the current time $t_i$ and the time of the previous sample $t_{i-1}$. We denote this averaged (filtered) value by $\bar{f}_i$, and note as an aside that using $\bar{f}_i$ rather than the naive $f(x_i)$ introduces half a sample of delay.

The average of any continuous function $y(t)$ between two times $t_{i-1}$ and $t_i$ is given by the integral,

$$\bar{y} = \frac{1}{t_{i} - t_{i-1}}\int_{t_{i-1}}^{t_i} y(t) dt.$$

Our filtered signal $\bar{f}_i$ is therefore

$$\bar{f}\_i = \frac{1}{t_{i} - t_{i-1}}\int_{t_{i-1}}^{t_i} f(x(t)) dt.$$

We can now do some manipulation of this:

$$ \bar{f}_i =\frac{1}{t_{i} - t_{i-1}}\int_{t_{i-1}}^{t_i} f(x(t)) dt$$
$$= \frac{1}{t_{i} - t_{i-1}} \int_{x_{i-1}}^{x_i} f(x(t)) \frac{dt}{dx}  dx$$
$$= \frac{1}{x_{i} - x_{i-1}} \int_{x_{i-1}}^{x_i} f(x(t))  dx$$
$$= \frac{1}{x_{i} - x_{i-1}}\left(F(x_i) - F(x_{i-1}) \right),$$

where we exploited the assumption that $x(t)$ is composed of straight-line segments to allow us to evaluate
$$\frac{dt}{dx} = \frac{t_i - t_{i-1}}{x_i - x_{i-1}}$$
in the interval $t_{i-1} < t < t_i$.

This gives our result,

$$\bar{f}_i  = \frac{F(x_i) - F(x_{i-1})}{x_{i} - x_{i-1}}.$$

where the function $F$ is the integral of the nonlinearity $f$,

$$ F(x) = \int f(x) dx.$$

Note that we cannot actually evaluate this if the current sample is the same as the last one ($x_i = x_{i-1}$), and so in that case we would instead use 

$$\frac{ f(x_i) + f(x_{i-1})}{2}$$

rather than $\bar{f}_i$.

#### Coding

Let's use as an example based around a half-wave rectifier function

$$ f(x) = \begin{cases} x & \mbox{if $x>0$,}\\\\ 0 & \mbox{otherwise.} \end{cases}$$

represented in code as
```c++
int32_t fn(int32_t n)
{
	if (n > 0)
		return n;
	else
		return 0;
}
```

The integral of $f(x)$ is 

$$ F(x) = \begin{cases} \frac{x^2}{2} & \mbox{if $x>0$,}\\\\ 0 & \mbox{otherwise.} \end{cases}$$

represented in code as:
```c++
int32_t intfn(int32_t n)
{
	if (n > 0)
		return (n * n) >> 1;
	else
		return 0;
}
```
Then, our implementation of the half-wave rectifier is:
```c++
void ProcessSample()
{
	static int32_t lastAudio = 0;
	int32_t audio = AudioIn1();
	int32_t result;

	if (audio != lastAudio)
	{
		result = (intfn(audio) - intfn(lastAudio)) / (audio - lastAudio);
	}
	else
	{
		result = (fn(audio) + fn(lastAudio)) >> 1;
	}
	AudioOut1(result);
    lastAudio = audio;
}
```

Note that if `audio` and `lastAudio` are both positive, or both negative, then the result calculated by
```c++
result = (intfn(audio) - intfn(lastAudio)) / (audio - lastAudio);
```
is the same as
```c++
result = (fn(audio) + fn(lastAudio)) >> 1;
```
That is, in both the linear regions of the half-rectifier function, the antialiasing does nothing (other than introduce the half-sample delay inherent in the method). This is reasonable becasue, in these linear regions, no extra frequencies are generated. But when the waveform crosses the nonlinearity at $x=0$, the antialiasing then takes effect.

#### What does it sound like?
With an LFO-modulated sine wave as input, first with, then without the antiderivative antialiasing. 
In the spectrograms and audio, some aliasing can be seen and heard even with the antialiasing, but at a considerably lower volume than the naive algorithm.
<img width="770" height="335" alt="aa_nonlin" src="https://github.com/user-attachments/assets/2dc5c531-9cdc-4ac0-985e-f63de6ba899a" />

https://github.com/user-attachments/assets/dfa8cdb7-a61a-4de7-946e-699a70486ffc

### Application to synthesis of waveforms
The antiderivative antialising technique originated in digital synthesis of sawtooth waves ([Välimäki, "Discrete-time synthesis of the sawtooth waveform with reduced aliasing", 2005](https://doi.org/10.1109/LSP.2004.842271)).

The waveform to be represented is a sawtooth of frequency $f_0=1/T$. In terms of a phase $\phi = t/f_0$, one period of this sawtooth is described by

$$ f(t) = 2 f_0 t \qquad \mbox{for} \quad -T/2 \leq t < T/2 $$

Following the general procedure of antiderivative antialiasing, this is integrated in the continous-time domain to give

$$ F(t) = \int f(t) dt = f_0 t^2 \qquad \mbox{for} \quad -T/2 \leq t < T/2 $$

and $F$ (or specifically, its periodic extension) is then sampled to give

$$ F_i = F(t_i) = F(i / f_s).$$

The samples are then differentiated numerically to give

$$ f_i = f_s (F_i - F_{i-1}). $$

As with the nonlinear function example, we can verify that, within the linear part of the sawtooth, the effect of this scheme is to reproduce the sawtooth wave with a delay of half a sample:

$$ f_i = f_s \left(f_0 \frac{i^2}{f_s^2} - f_0 \frac{(i-1)^2}{f_s^2} \right) = \frac{f_0}{f_s} (2 i - 1) = f(t_i - 1/2) $$


#### Coding
A fixed-point version of this algorithm is implemented in Utility Pair's `Saw.h`, a condensed version of which is as follows:
```c++
class Saw
{
public:
	void SetFreq(int32_t f) // f is roughly 89478 per Hz at sr=48kHz
	{
		phase_incr = f; 
		invc = phase_incr>>15;
	}

	int32_t Tick()
	{
		dphase += phase_incr; // -2147483648 to 2147483647
		int32_t dphase2 = (dphase>>16); // -32768 to 32767
		dphase2 *= dphase2; // 0 to 1073741824
		int32_t retval = dphase2 - last_dphase2; // -1073741824 to 1073741824 = ±2^30
		last_dphase2 = dphase2;
		return retval/invc; 
	}
private:
	int32_t phase_incr, dphase, last_dphase2, invc;
};
```
* As in Välimäki (2005) the oscillator is described as a function of its unit phase (range 0 to 1), which here is stored in the entire 32-bit range of `dphase`. This is therefore incremented by $2^32 f_0 / f_s \approx 89478 f_0$ every sample, at $f_s =$48kHz.
* $F(t)$ is proportional to the square of this phase, which is stored in `dphase2`. Bit shifting is used to ensure that this, and the subsequent numerical differentiation (in `retval`) do not overflow 32-bits.
* `invc` is a scale factor that sets the output to signed 16-bit amplitude (±32768).
