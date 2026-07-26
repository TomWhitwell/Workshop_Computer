ORG 32768

START:
        DI              ; Disable interrupts for clean 1-bit timing

MAIN_LOOP:
        ; --- SCAN KEYBOARD ---
        LD BC, $FBFE    ; Row Q-T
        IN A, (C)
        BIT 0, A
        JR Z, PLAY_Q    ; Q

        LD BC, $FDFE    ; Row A-G
        IN A, (C)
        BIT 0, A
        JR Z, PLAY_A    ; A

        LD BC, $DFFE    ; Row P-Y
        IN A, (C)
        BIT 0, A
        JR Z, PLAY_P    ; P
        BIT 1, A
        JR Z, PLAY_O    ; O

        LD BC, $7FFE    ; Row SPACE-B
        IN A, (C)
        BIT 0, A
        JR Z, PLAY_SPACE; SPACE

        LD BC, $BFFE    ; Row ENTER-H
        IN A, (C)
        BIT 0, A
        JP Z, PLAY_ENTER; ENTER

        JP MAIN_LOOP

; ---------------------------------------------------------
; [Q] FAST PEW-PEW (LASER)
; Classic pitch sweep down. Port 95 modulates the base delay.
; ---------------------------------------------------------
PLAY_Q:
        LD L, 0         ; L = Delay counter (starts at 0 for highest pitch)
        LD B, 0         ; Duration (256 cycles)
Q_LOOP:
        LD A, 16        ; Speaker high
        OUT (254), A
        
        IN A, (95)      
        ADD A, L        ; Combine sweep phase with Port 95
        LD C, A         ; C = actual delay
Q_DEL1: DEC C
        JR NZ, Q_DEL1

        XOR A           ; Speaker low
        OUT (254), A
        
        IN A, (95)
        ADD A, L
        LD C, A
Q_DEL2: DEC C
        JR NZ, Q_DEL2

        INC L           ; Increase base delay (sweep pitch down)
        DJNZ Q_LOOP
        JR MAIN_LOOP

; ---------------------------------------------------------
; [A] BOMB DROP (PITCH-DROPPING NOISE)
; Sweeps down like a laser, but uses the R register for noise.
; ---------------------------------------------------------
PLAY_A:
        LD L, 0         ; Delay counter
        LD DE, $0800    ; Long duration
A_LOOP:
        LD A, R         ; Read R register for pseudo-randomness
        AND 16          ; Mask speaker bit
        OUT (254), A

        IN A, (95)
        ADD A, L
        LD C, A
A_DEL:  DEC C
        JR NZ, A_DEL

        ; Only increase delay occasionally to make the drop slower/heavier
        LD A, E
        AND 7
        JR NZ, A_SKIP
        INC L           ; Drop pitch
A_SKIP:
        DEC DE
        LD A, D
        OR E
        JR NZ, A_LOOP
        JR MAIN_LOOP

; ---------------------------------------------------------
; [O] WHITE NOISE CRASH
; Pure R-register noise. Port 95 acts as a variable low-pass
; filter by stretching the delay between random samples.
; ---------------------------------------------------------
PLAY_O:
        LD BC, $0100    ; Crash duration
O_LOOP:
        LD A, R         ; Rapid pseudo-random byte
        AND 16          
        OUT (254), A

        IN A, (95)      ; Port 95 controls noise density
        SRL A           ; Scale it down slightly so it doesn't stall
        INC A           ; Ensure at least 1 delay cycle
        LD D, A
O_DEL:  DEC D
        JR NZ, O_DEL

        DEC BC
        LD A, B
        OR C
        JR NZ, O_LOOP
        JP MAIN_LOOP

; ---------------------------------------------------------
; [P] GRANULAR ROM SCATTER (METALLIC CLANG)
; Uses Port 95 to determine how far the pointer jumps through
; the Spectrum's ROM ($0000+) on every cycle, outputting the data.
; ---------------------------------------------------------
PLAY_P:
        LD HL, $0000    ; Start reading at ROM address 0
        LD DE, $1000    ; Duration
P_LOOP:
        LD A, (HL)      ; Read data byte from ROM
        AND 16          
        OUT (254), A

        IN A, (95)      ; Read external CV
        LD C, A
        LD B, 0
        ADD HL, BC      ; Jump forward in ROM by Port 95's value!

        DEC DE
        LD A, D
        OR E
        JR NZ, P_LOOP
        JP MAIN_LOOP

; ---------------------------------------------------------
; [SPACE] UFO SIREN (WRAPPED RISER)
; Decreases delay to sweep pitch up, but lets the 8-bit register
; overflow (wrap to 255), creating an LFO/Siren effect.
; ---------------------------------------------------------
PLAY_SPACE:
        LD L, 255       ; Start slow
        LD DE, $0300    ; Duration (long enough to hear several wraps)
SPC_LOOP:
        LD A, 16
        OUT (254), A

        IN A, (95)
        ADD A, L
        LD C, A
SPC_DEL1: DEC C
        JR NZ, SPC_DEL1

        XOR A
        OUT (254), A
        
        IN A, (95)
        ADD A, L
        LD C, A
SPC_DEL2: DEC C
        JR NZ, SPC_DEL2

        DEC L           ; Sweep pitch UP (decrease delay)
                        ; Note: No bounds checking, so it wraps naturally
        DEC DE
        LD A, D
        OR E
        JR NZ, SPC_LOOP
        JP MAIN_LOOP

; ---------------------------------------------------------
; [ENTER] BYTEBEAT GLITCH (BITWISE FRACTAL NOISE)
; A 1-bit take on bytebeat music. XORs the high and low bytes
; of a counter, masking it dynamically with Port 95.
; ---------------------------------------------------------
; ---------------------------------------------------------
; [ENTER] CV-PITCHED BYTEBEAT (FRACTAL NOISE)
; Uses Port 95 to dynamically stretch the time between 
; samples, acting as a direct V/Oct style pitch control 
; over the bitwise fractal sequence.
; ---------------------------------------------------------
PLAY_ENTER:
        LD HL, 0        ; Setup our fractal time counter
        LD DE, $100    ; Duration (shorter base, as the delay will extend it)
        
ENT_LOOP:
        ; 1. Generate the bytebeat sample
        LD A, H
        XOR L           ; Standard bitwise fractal: (t ^ (t >> 8))
        AND 16          ; Isolate the speaker bit
        OUT (254), A    ; Output to ULA

        ; 2. Read Port 95 for pitch control
        IN A, (95)      ; Read external CV (0-255)
        
        ; Optional: Scale the input if the lowest pitch is too slow.
        ; SRL A         ; Uncomment to halve the maximum delay (higher overall pitch)
        
        INC A           ; Ensure B is never 0 (which would wrap to 256 and cause a huge delay)
        LD B, A         ; Set our delay counter based on the CV

        ; 3. Variable delay (Clock Divider)
ENT_DELAY:
        DJNZ ENT_DELAY  ; Loop B times. Higher Port 95 = longer delay = lower pitch

        ; 4. Advance time and loop
        INC HL          ; Advance fractal time

        DEC DE
        LD A, D
        OR E

        JR NZ, ENT_LOOP
        JP MAIN_LOOP
        
           end 32768