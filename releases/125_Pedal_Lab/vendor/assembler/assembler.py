#
# asfv1: Alternate FV-1 Assembler
# Copyright (C) 2017-2019 Nathan Fraser
#
# Python 2 compatibility
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals
from __future__ import absolute_import

# Imports
import argparse
import sys
import shlex
import struct

# Constants
VERSION = '1.2.7'
PROGLEN = 128
DELAYSIZE = 32767
MAXERR = 10	# abort assembly if too many errors found

# Fixed point reals SN_D with one sign bit (S),
# N integer bits and D fractional bits:
#
#	REF_... reference value at +1.0: 2**D
#	MIN_... smallest real number: -2**(N+D)/2**D == -2**N
#	MAX_... largest real number: (2**(N+D)-1)/REF
#
REF_S1_14 = 2.0**14			# 16384.0
MIN_S1_14 = -2.0**1			# -2.0
MAX_S1_14 = (2.0**(1+14)-1.0)/REF_S1_14	# 1.99993896484375

REF_S1_9 = 2.0**9			# 512.0
MIN_S1_9 = -2.0**1			# -2.0
MAX_S1_9 = (2.0**(1+9)-1.0)/REF_S1_9	# 1.998046875

REF_S_10 = 2.0**10			# 1024.0
MIN_S_10 = -2.0**0			# -1.0
MAX_S_10 = (2.0**(0+10)-1.0)/REF_S_10	# 0.9990234375

REF_S_15 = 2.0**15			# 32768.0
MIN_S_15 = -2.0**0			# -1.0
MAX_S_15 = (2.0**(0+15)-1.0)/REF_S_15	# 0.999969482421875

REF_S4_6 = 2.0**6			# 64.0
MIN_S4_6 = -2.0**4			# -16.0
MAX_S4_6 = (2.0**(4+6)-1.0)/REF_S4_6	# 15.984375

REF_S_23 = 2.0**23			# 8388608.0
MIN_S_23 = -2.0**0			# -1.0
MAX_S_23 = (2.0**(0+23)-1.0)/REF_S_23	# 0.9999998807907104

# Bit Masks
M1 = 0x01
M2 = 0x03
M5 = 0x1f
M6 = 0x3f
M8 = 0xff
M9 = 0x1ff
M11 = 0x7ff
M14 = 0x3fff
M15 = 0x7fff
M16 = 0xffff
M24 = 0xffffff
M27 = 0x7ffffff
M32 = 0xffffffff

def quiet(msg):
    pass

def warning(msg):
    print(msg, file=sys.stderr)

def error(msg):
    print(msg, file=sys.stderr)

def bintoihex(buf, spos=0x0000, width=4):
    """Convert binary buffer to ihex and return as string."""
    c = 0
    olen = len(buf)
    ret = ""
    while(c < olen):
        rem = olen-c
        if rem > width:
            rem = width
        sum = rem
        adr = c + spos
        l = ':{0:02X}{1:04X}00'.format(rem,adr)   # rem < 0x10
        sum += ((adr>>8)&M8)+(adr&M8)
        for j in range(0,rem):
            nb = buf[c+j]
            l += '{0:02X}'.format(nb)
            sum = (sum + nb)&M8
        l += '{0:02X}'.format((~sum+1)&M8)
        ret += l + '\n'
        c += rem
    ret += ':00000001FF\n'        # EOF
    return ret

# Machine instruction table
op_tbl = {
        # mnemonic: [opcode, (arglen,left shift), ...]
        'RDA':  [0b00000, (M15,5),(M11,21)],
        'RMPA': [0b00001, (M11,21)],
        'WRA':  [0b00010, (M15,5),(M11,21)],
        'WRAP': [0b00011, (M15,5),(M11,21)],
        'RDAX': [0b00100, (M6,5),(M16,16)],
        'RDFX': [0b00101, (M6,5),(M16,16)],
        'LDAX':	[0b00101, (M6,5)], # psuedo: RDFX REG,0
        'WRAX': [0b00110, (M6,5),(M16,16)],
        'WRHX': [0b00111, (M6,5),(M16,16)],
        'WRLX': [0b01000, (M6,5),(M16,16)],
        'MAXX': [0b01001, (M6,5),(M16,16)],
        'ABSA':	[0b01001, ], # pseudo: MAXX 0,0
        'MULX': [0b01010, (M6,5)],
        'LOG':  [0b01011, (M16,16),(M11,5)],
        'EXP':  [0b01100, (M16,16),(M11,5)],
        'SOF':  [0b01101, (M16,16),(M11,5)],
        'AND':  [0b01110, (M24,8)],
        'CLR':	[0b01110, ], # pseudo: AND $0
        'OR' :  [0b01111, (M24,8)],
        'XOR':  [0b10000, (M24,8)],
        'NOT':	[0b10000, (M24,8)], # pseudo: XOR $ffffff
        'SKP':  [0b10001, (M5,27),(M6,21)],	# note 1
        'JMP':  [0b10001, (M5,27),(M6,21)],	# pseudo skp 0,...
        'NOP':	[0b10001, ], # pseudo: SKP 0,0 note 2
        'WLDS': [0b10010, (M1,29),(M9,20),(M15,5)],
        'WLDR': [0b10010, (M2,29),(M16,13),(M2,5)], # CHECK
        'JAM':  [0b10011, (M2,6)],
        'CHO':  [0b10100, (M2,30),(M2,21),(M6,24),(M16,5)], # CHECK
        'RAW':  [0b00000, (M32,0)],         # direct data insertion
        # Notes:
        # 1. In SpinASM IDE , condition flags expand to shifted values,
        # 2. NOP is not documented, but expands to SKP 0,0 in SpinASM
}

def op_gen(mcode):
    """Generate a machine instruction using the op gen table."""
    gen = op_tbl[mcode[0]]
    ret = gen[0]	# opcode
    nargs = len(gen)
    i = 1
    while i < nargs:
        if i < len(mcode):	# or assume they are same len
            ret |= (mcode[i]&gen[i][0]) << gen[i][1]
        i += 1
    return ret

class fv1parse(object):
    def __init__(self, source=None, clamp=True,
                 spinreals=False, wfunc=None, efunc=None):
        self.program = bytearray(512)
        self.doclamp = clamp
        self.spinreals = spinreals
        self.dowarn = wfunc
        self.doerror = efunc
        self.delaymem = 0
        self.prevline = 0	# line number for last accepted symbol
        self.sline = 0		# line number of current symbol
        self.icnt = 0
        self.sym = None
        self.ecount = 0
        self.source = source.split('\n')
        self.linebuf = []
        self.pl = []	# parse list
        self.mem = {}	# delay memory
        self.jmptbl = { # jump table for skips
        }
        self.symtbl = {	# symbol table
                'SIN0_RATE':	0x00,
                'SIN0_RANGE':	0x01,
                'SIN1_RATE':	0x02,
                'SIN1_RANGE':	0x03,
                'RMP0_RATE':	0x04,
                'RMP0_RANGE':	0x05,
                'RMP1_RATE':	0x06,
                'RMP1_RANGE':	0x07,
                'POT0':		0x10,
                'POT1':		0x11,
                'POT2':		0x12,
                'ADCL':		0x14,
                'ADCR':		0x15,
                'DACL':		0x16,
                'DACR':		0x17,
                'ADDR_PTR':	0x18,
                'REG0':		0x20,
                'REG1':		0x21,
                'REG2':		0x22,
                'REG3':		0x23,
                'REG4':		0x24,
                'REG5':		0x25,
                'REG6':		0x26,
                'REG7':		0x27,
                'REG8':		0x28,
                'REG9':		0x29,
                'REG10':	0x2a,
                'REG11':	0x2b,
                'REG12':	0x2c,
                'REG13':	0x2d,
                'REG14':	0x2e,
                'REG15':	0x2f,
                'REG16':	0x30,
                'REG17':	0x31,
                'REG18':	0x32,
                'REG19':	0x33,
                'REG20':	0x34,
                'REG21':	0x35,
                'REG22':	0x36,
                'REG23':	0x37,
                'REG24':	0x38,
                'REG25':	0x39,
                'REG26':	0x3a,
                'REG27':	0x3b,
                'REG28':	0x3c,
                'REG29':	0x3d,
                'REG30':	0x3e,
                'REG31':	0x3f,
                'SIN0':		0x00,
                'SIN1':		0x01,
                'RMP0':		0x02,
                'RMP1':		0x03,
                'RDA':		0x00,
                'SOF':		0x02,
                'RDAL':		0x03,
                'SIN':		0x00,
                'COS':		0x01,
                'REG':		0x02,
                'COMPC':	0x04,
                'COMPA':	0x08,
                'RPTR2':	0x10,
                'NA':		0x20,
                'RUN':		0x10,
                'ZRC':		0x08,
                'ZRO':		0x04,
                'GEZ':		0x02,
                'NEG':		0x01,
    }

    def __mkopcodes__(self):
        """Convert the parse list into machine code for output."""
        proglen = len(self.pl)
        self.dowarn('info: Read {} instructions from input'.format(
                proglen))

        # pad free space with empty SKP instructions
        icnt = proglen
        while icnt < PROGLEN:
            self.pl.append({'cmd':['SKP',0x00,0x00],
                            'addr':icnt,
                            'target':None})
            icnt += 1
        
        # convert program to machine code and prepare for output
        oft = 0
        for i in self.pl:
            struct.pack_into('>I', self.program, oft, op_gen(i['cmd']))
            oft += 4

    def __register__(self, mnemonic=''):
        """Fetch a register definition."""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        reg = self.__expression__()
        if int(reg) == reg:
            reg = int(reg)
            if reg < 0 or reg > 63:
                self.parseerror('Register {0:#x} out of range'.format(reg)
                                + xtra)
                reg = 0
        else:
            self.parseerror('Invalid register {}'.format(reg)
                            + xtra)
            reg = 0
        return reg

    def __d_15__(self,mnemonic=''):
        """Fetch a 15 bit delay address, preferring integer interpretation"""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        oft = self.__expression__()
        if oft < MIN_S_15 or oft > MAX_S_15:
            oft = int(round(oft))
            if oft < -0x8000 or oft > M15:
                if self.doclamp:
                    if oft < -0x8000:
                        oft = -0x8000
                    elif oft > M15:
                        oft = M15
                    self.parsewarn('Address clamped to {0:#x}'.format(oft)
                                   + xtra)
                else:
                    self.parseerror('Invalid address {0:#x}'.format(oft)
                                    + xtra)
                    oft = 0
        else:
            oft = int(round(oft * REF_S_15))
        return oft

    def __offset__(self, mnemonic=''):
        """Fetch a skip offset definition."""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        oft = self.__expression__()
        if int(oft) == oft:
            oft = int(oft)
            if oft < 0 or oft > M6:
                self.parseerror('Offset {} out of range'.format(oft)
                                + xtra)
                oft = 0
        else:
            self.parseerror('Invalid offset {}'.format(oft)
                            + xtra)
            oft = 0
        return oft

    def __condition__(self, mnemonic=''):
        """Fetch a skip condition code."""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        cond = self.__expression__()
        if int(cond) == cond:
            cond = int(cond)
            if cond < 0 or cond > M5:
                self.parseerror('Condition {0:#x} out of range'.format(
                                cond) + xtra)
                cond = 0
        else:
            self.parseerror('Invalid condition {}'.format(cond) + xtra)
            cond = 0
        return cond

    def __chotype__(self):
        """Fetch CHO type code."""
        # this is not an operand - the symbol text is examined directly
        chotype = self.sym['stxt']
        self.__next__()
        if chotype in ['RDA','SOF','RDAL']:
            chotype = self.symtbl[chotype]
        else:
            self.parseerror('Invalid CHO type {}'.format(chotype))
            chotype = 0
        return chotype

    def __choflags__(self, lfo=None):
        """Fetch CHO condition flags."""
        flags = self.__expression__()
        if int(flags) == flags:
            flags = int(flags)
            if flags < 0 or flags > M6:
                self.parseerror('Invalid flags {0:#x} for CHO'.format(flags))
                flags = 0
        else:
            self.parseerror('Invalid flags {} for CHO'.format(flags))
            flags = 0
        oflags = flags
        if lfo&0x02: # RMP0/RMP1
            flags = oflags & 0x3e
            if oflags != flags:
                self.parsewarn('RMP flags set to {0:#x} for CHO'.format(
                                flags))
        else:
            flags = oflags & 0x0f
            if oflags != flags:
                self.parsewarn('SIN flags set to {0:#x} for CHO'.format(
                                flags))
        return flags

    def __s1_14__(self, mnemonic=''):
        """Fetch a 16 bit real argument."""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        arg = self.__expression__()
        if isinstance(arg, int):
            if arg < 0 or arg > M16:
                if self.doclamp:
                    if arg < 0:
                        arg = 0
                    elif arg > M16:
                        arg = M16
                    self.parsewarn('S1_14 arg clamped to {0:#x}'.format(arg)
                                   + xtra)
                else:
                    self.parseerror('S1_14 arg {0:#x} out of range'.format(
                                    arg) + xtra)
                    arg = 0
        else:
            if arg < MIN_S1_14 or arg > MAX_S1_14:
                if self.doclamp:
                    if arg < MIN_S1_14:
                        arg = MIN_S1_14
                    elif arg > MAX_S1_14:
                        arg = MAX_S1_14
                    self.parsewarn('S1_14 arg clamped to {}'.format(arg)
                                   + xtra)
                else:
                    self.parseerror('S1_14 arg {} out of range'.format(arg)
                                    + xtra)
                    arg = 0
            arg = int(round(arg * REF_S1_14))
        return arg

    def __s_10__(self, mnemonic=''):
        """Fetch an 11 bit S_10 real argument."""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        arg = self.__expression__()
        if isinstance(arg, int):
            if arg < 0 or arg > M11:
                if self.doclamp:
                    if arg < 0:
                        arg = 0
                    elif arg > M11:
                        arg = M11
                    self.parsewarn('S_10 arg clamped to {0:#x}'.format(
                                   arg) + xtra)
                else:
                    self.parseerror('S_10 arg {0:#x} out of range'.format(
                                    arg) + xtra)
                    arg = 0
        else:
            if arg < MIN_S_10 or arg > MAX_S_10:
                if self.doclamp:
                    if arg < MIN_S_10:
                        arg = MIN_S_10
                    elif arg > MAX_S_10:
                        arg = MAX_S_10
                    self.parsewarn('S_10 arg clamped to {}'.format(arg)
                                   + xtra)
                else:
                    self.parseerror('S_10 arg {} out of range'.format(arg)
                                    + xtra)
                    arg = 0
            arg = int(round(arg * REF_S_10))
        return arg

    def __s_15a__(self, mnemonic=''):
        """Fetch a 16 bit S_15 real address argument."""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        arg = self.__expression__()
        if self.spinreals and arg == int(arg):
            arg = int(arg)
        if isinstance(arg, int):
            if arg < 0 or arg > M16:
                if self.doclamp:
                    if arg < 0:
                        arg = 0
                    elif arg > M16:
                        arg = M16
                    self.parsewarn('S_15 arg clamped to {0:#x}'.format(
                                   arg) + xtra)
                else:
                    self.parseerror('S_15 arg {0:#x} out of range'.format(
                                    arg) + xtra)
                    arg = 0
        else:
            if arg < MIN_S_15 or arg > MAX_S_15:
                if self.doclamp:
                    if arg < MIN_S_15:
                        arg = MIN_S_15
                    elif arg > MAX_S_15:
                        arg = MAX_S_15
                    self.parsewarn('S_15 arg clamped to {}'.format(arg)
                                   + xtra)
                else:
                    self.parseerror('S_15 arg {} out of range'.format(arg)
                                    + xtra)
                    arg = 0
            arg = int(round(arg * REF_S_15))
        return arg

    def __u_32__(self, mnemonic=''):
        """Fetch a raw 32 bit data string."""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        arg = self.__expression__()
        if isinstance(arg, int):
            if arg < 0 or arg > M32:
                if self.doclamp:
                    if arg < 0:
                        arg = 0
                    elif arg > M32:
                        arg = M32
                    self.parsewarn('U_32 arg clamped to {0:#x}'.format(arg)
                                   + xtra)
                else:
                    self.parseerror('U_32 arg {0:#x} out of range'.format(
                                    arg) + xtra)
                    arg = 0
        else:
            self.parseerror('Invalid U_32 arg {}'.format(arg) + xtra)
            arg = 0
        return arg

    def __s_23__(self, mnemonic=''):
        """Fetch a 24 bit S_23 real or mask argument."""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        arg = self.__expression__()
        if isinstance(arg, int):
            if arg < 0 or arg > M24:
                if self.doclamp:
                    if arg < 0:
                        arg = 0
                    elif arg > M24:
                        arg = M24
                    self.parsewarn('S_23 arg clamped to {0:#x}'.format(
                                   arg) + xtra)
                else:
                    self.parseerror('S_23 arg {0:#x} out of range'.format(
                                    arg) + xtra)
                    arg = 0
        else:
            if arg < MIN_S_23 or arg > MAX_S_23:
                if self.doclamp:
                    if arg < MIN_S_23:
                        arg = MIN_S_23
                    elif arg > MAX_S_23:
                        arg = MAX_S_23
                    self.parsewarn('S_23 arg clamped to {}'.format(arg)
                                   + xtra)
                else:
                    self.parseerror('S_23 arg {} out of range'.format(arg)
                                    + xtra)
                    arg = 0
            arg = int(round(arg * REF_S_23))
        return arg

    def __s1_9__(self, mnemonic=''):
        """Fetch an 11 bit real argument."""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        arg = self.__expression__()
        if isinstance(arg, int):
            if arg < 0 or arg > M11:
                if self.doclamp:
                    if arg < 0:
                        arg = 0
                    elif arg > M11:
                        arg = M11
                    self.parsewarn('S1_9 arg clamped to {0:#x}'.format(
                                   arg) + xtra)
                else:
                    self.parseerror('S1_9 arg {0:#x} out of range'.format(
                                    arg) + xtra)
                    arg = 0
        else:
            if arg < MIN_S1_9 or arg > MAX_S1_9:
                if self.doclamp:
                    if arg < MIN_S1_9:
                        arg = MIN_S1_9
                    elif arg > MAX_S1_9:
                        arg = MAX_S1_9
                    self.parsewarn('S1_9 arg clamped to {}'.format(arg)
                                   + xtra)
                else:
                    self.parseerror('S1_9 arg {} out of range'.format(arg)
                                    + xtra)
                    arg = 0
            arg = int(round(arg * REF_S1_9))
        return arg

    def __s4_6__(self, mnemonic=''):
        """Fetch an 11 bit S4_6 argument."""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        arg = self.__expression__()
        if isinstance(arg, int):
            if arg < 0 or arg > M11:
                if self.doclamp:
                    if arg < 0:
                        arg = 0
                    elif arg > M11:
                        arg = M11
                    self.parsewarn('S4_6 arg clamped to {0:#x}'.format(
                                   arg) + xtra)
                else:
                    self.parseerror('S4_6 arg {0:#x} out of range'.format(
                                    arg) + xtra)
                    arg = 0
        else:
            if arg < MIN_S4_6 or arg > MAX_S4_6:
                if self.doclamp:
                    if arg < MIN_S4_6:
                        arg = MIN_S4_6
                    elif arg > MAX_S4_6:
                        arg = MAX_S4_6
                    self.parsewarn('S4_6 arg clamped to {}'.format(arg)
                                   + xtra)
                else:
                    self.parseerror('S4_6 arg {} out of range'.format(arg)
                                    + xtra)
                    arg = 0
            arg = int(round(arg * REF_S4_6))
        return arg

    def __lfo__(self, mnemonic=''):
        """Select an LFO."""
        # there is some ambiguity here - but it is resolved in
        # WLDS by clearing the MSB, and in WLDR by ORing with 0x2
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        ##
        ## or check for cos0/cos1 here in the case of CHO
        ##
        lfo = self.__expression__()
        if int(lfo) == lfo:
            lfo = int(lfo)
            if lfo < 0 or lfo > 3:
                self.parseerror('Invalid LFO {0:#x}'.format(lfo) + xtra)
                lfo = 0
        else:
            self.parseerror('Invalid LFO {}'.format(lfo) + xtra)
            lfo = 0
        return lfo

    def __lfo_sinfreq__(self, mnemonic=''):
        """Fetch a sine LFO frequency value."""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        freq = self.__expression__()
        if int(freq) == freq:
            freq = int(freq)
            if freq < 0 or freq > M9:
                if self.doclamp:
                    if freq < 0:
                        freq = 0
                    elif freq > M9:
                        freq = M9
                    self.parsewarn('Frequency clamped to {0:#x}'.format(freq)
                                    + xtra)
                else:
                    self.parseerror('Invalid frequency {0:#x}'.format(freq)
                                    + xtra)
                    freq = 0
        else:
            self.parseerror('Invalid frequency {}'.format(freq)
                            + xtra)
            freq = 0
        return freq

    def __lfo_rampfreq__(self, mnemonic=''):
        """Fetch a RMP LFO frequency value."""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        freq = self.__expression__()
        if freq < -0.5 or freq > MAX_S_15:	# not quite right
            freq = int(round(freq))
            if freq < -0x8000 or freq > M15:
                if self.doclamp:
                    if freq < -0x8000:
                        freq = -0x8000
                    elif freq > M15:
                        freq = M15
                    self.parsewarn('Frequency clamped to {0:#x}'.format(freq)
                                   + xtra)
                else:
                    self.parseerror('Invalid frequency {0:#x}'.format(freq)
                                    + xtra)
                    freq = 0
        else:
            freq = int(round(freq * REF_S_15))
        return freq

    def __lfo_rampamp__(self, mnemonic=''):
        """Fetch a RMP LFO amplitude value."""
        xtra = ''
        if mnemonic:
            xtra = ' for ' + mnemonic
        amp = self.__expression__()
        rampamps = {4096:0, 2048:1, 1024:2, 512:3, 0:0, 1:1, 2:2, 3:3}
        if int(amp) == amp:
            amp = int(amp)
            if amp in rampamps:
                amp = rampamps[amp]
            else:
                self.parseerror('Invalid amplitude {}'.format(amp)
                                + xtra)
                amp = 0
        else:
            self.parseerror('Invalid amplitude {}'.format(amp)
                            + xtra)
            amp = 0
        return amp

    def __next__(self):
        """Fetch next symbol."""
        # Scanner uses shlex to break up the input, then reassembles
        # tokens into one of:
        #
        #  EOF : end of input marker
        #  MNEMONIC : instruction mnemonic text
        #  ASSEMBLER : assembler directive 'EQU' or 'MEM'
        #  OPERATOR : expression operator | ^ & << >> + - * // / ~ + - **
        #  INTEGER : integer numeric literal
        #  FLOAT : real numeric literal
        #  TARGET : target address in assembled program
        #  LABEL : text label
        #  ARGSEP : operand delimiting character ,
        #
        # comments are stripped by shlex.
        self.sym = None
        self.prevline = self.sline	# line of last fetched symbol
        while self.sym is None:
            if len(self.linebuf) == 0:	# nothing in line buf yet
                if len(self.source) > 0:	# still some lines in source
                    self.sline += 1
                    llex = shlex.shlex(self.source.pop(0))
                    llex.commenters = ';'
                    self.linebuf = [t for t in llex]
                else:
                    self.sym = {'type': 'EOF', 'txt':None,
                                'stxt':None, 'val':None}
            if len(self.linebuf) > 0:
                stxt = self.linebuf[0].upper()
                if stxt in op_tbl:	# MNEMONIC
                    self.sym = {'type': 'MNEMONIC',
                                'txt': self.linebuf.pop(0),
                                'stxt': stxt,
                                'val': None}
                elif stxt in ['EQU', 'MEM']:
                    self.sym = {'type': 'ASSEMBLER',
                                'txt': self.linebuf.pop(0),
                                'stxt': stxt,
                                'val': None}
                elif stxt in ['<','>','*','/']:
                    optxt = self.linebuf.pop(0)
                    if len(self.linebuf) > 0:
                        if self.linebuf[0] == optxt: # **, //, <<, >>
                            optxt += self.linebuf.pop(0)
                        if optxt in ['<','>']:
                            self.scanerror('Invalid operator {}'.format(optxt))
                            optxt += optxt
                    self.sym = {'type': 'OPERATOR',
                                'txt': optxt,
                                'stxt': optxt,
                                'val': None}
                elif stxt in ['|','^','&','+','-','~','!','(',')','INT']:
                    self.sym = {'type': 'OPERATOR',
                                'txt': self.linebuf.pop(0),
                                'stxt': stxt,
                                'val': None}
                elif stxt[0] in ['%', '$']:
                    # SpinASM style integers
                    pref = self.linebuf.pop(0)
                    base = 2
                    if pref == '$':
                        base = 16
                    if len(self.linebuf) > 0:
                        ht = self.linebuf.pop(0)
                        ival = 0
                        try:
                            ival = int(ht.replace('_',''),base)
                        except:
                            self.scanerror('Invalid integer literal {}'.format(
                                           pref+ht))
                        self.sym = {'type': 'INTEGER',
                                        'txt': pref+ht,
                                        'stxt': pref+ht,
                                        'val': ival}
                    else:
                        self.scanerror('End of line scanning for integer')
                        self.sym = {'type': 'INTEGER',
                                        'txt': pref,
                                        'stxt': pref,
                                        'val': 0}
                elif stxt[0].isdigit(): # INTEGER or FLOAT
                    intpart = self.linebuf.pop(0).lower()
                    ival = 0.0
                    if len(self.linebuf) > 0 and self.linebuf[0] == '.':
                        self.linebuf.pop(0)
                        if len(self.linebuf) > 0:
                            frac = self.linebuf.pop(0)
                            if frac.endswith('e') and len(self.linebuf) > 0:
                                epart = self.linebuf.pop(0)
                                if epart in ['+','-'] and len(self.linebuf) > 0:
                                    epart += self.linebuf.pop(0)
                                frac = frac+epart
                            try:
                                ival = float(intpart+'.'+frac)
                            except:
                                self.scanerror(
                                 'Invalid numeric literal {}'.format(
                                               intpart+'.'+frac))
                            self.sym = {'type': 'FLOAT',
                                        'txt': intpart+'.'+frac,
                                        'stxt': intpart+'.'+frac,
                                        'val': ival}
                        else:
                            self.scanerror('Invalid numeric literal')
                            self.sym = {'type': 'FLOAT',
                                        'txt': intpart+'.0',
                                        'stxt': intpart+'.0',
                                        'val': ival}
                    elif self.spinreals and intpart in ['2', '1']:
                        ival = float(intpart)
                        self.sym = {'type': 'FLOAT',
                                    'stxt': intpart+'.0',
                                    'txt': intpart,
                                    'val': ival}
                    else:	# assume integer
                        ival = 0
                        base = 10
                        if intpart.startswith('0x'):
                            base = 16
                        elif intpart.startswith('0b'):
                            base = 2
                        try:
                            ival = int(intpart, base)
                        except:
                            self.scanerror('Invalid integer literal {}'.format(
                                           intpart))
                        self.sym = {'type': 'INTEGER',
                                    'txt': intpart,
                                    'stxt': intpart,
                                    'val': ival}

                elif stxt[0].isalpha(): # TARGET or LABEL
                    lbl = self.linebuf.pop(0)
                    if len(self.linebuf) > 0 and self.linebuf[0] == ':':
                        self.sym = {'type': 'TARGET',
                                    'txt': lbl,
                                    'stxt': stxt,
                                    'val': None}
                        self.linebuf.pop(0)
                    else:
                        mod = ''
                        if len(self.linebuf) > 0 and self.linebuf[0] in [
                                               '^','#']:
                            # is the label already defined as MEM?
                            if stxt+self.linebuf[0] in self.symtbl:
                                mod = self.linebuf.pop(0)
                            # else the modifier is ignored
                        self.sym = {'type': 'LABEL',
                                    'txt': lbl+mod,
                                    'stxt': stxt+mod,
                                    'val': None}
                elif stxt == ',':	# ARGSEP
                    self.sym = {'type': 'ARGSEP',
                                'txt': self.linebuf.pop(0),
                                'stxt': stxt,
                                'val': None}
                elif self.linebuf[0] == '\ufeff':
                    self.linebuf.pop(0) # ignore BOM
                else:
                    self.scanerror('Unrecognised input {}'.format(
                                   self.linebuf.pop(0)))

    def scanerror(self, msg):
        """Emit scan error."""
        self.doerror('scan error: ' + msg + ' on line {}'.format(self.sline))
        self.ecount += 1
        if self.ecount > MAXERR:
            self.doerror('too many errors, aborting.')
            sys.exit(-1)

    def parsewarn(self, msg, line=None):
        """Emit parse warning."""
        if line is None:
            line = self.prevline
        self.dowarn('warning: {} on line {}'.format(msg, line))

    def parseerror(self, msg, line=None):
        """Emit parse error."""
        if line is None:
            line = self.prevline
        self.doerror('parse error: {} on line {}'.format(msg, line))
        self.ecount += 1
        if self.ecount > MAXERR:
            self.doerror('too many errors, aborting.')
            sys.exit(-2)

    def __accept__(self,stype,message=None):
        """Accept the next symbol if type matches stype."""
        if self.sym['type'] == stype:
            self.__next__()
        else:
            if message is not None:
                self.parseerror(message)
            else:
                self.parseerror('Expected {} but saw {} {}'.format(
                             stype, self.sym['type'], self.sym['txt']),
                                 self.sline)

    def __instruction__(self):
        """Parse an instruction."""
        mnemonic = self.sym['stxt']
        opmsg = 'Missing required operand for '+mnemonic
        self.__accept__('MNEMONIC')
        if self.icnt >= PROGLEN:
            self.parseerror('Max program exceeded by {}'.format(mnemonic))
        if mnemonic in ['AND', 'OR', 'XOR', ]:
            mask = self.__s_23__(mnemonic)
            self.pl.append({'cmd':[mnemonic, mask],'addr':self.icnt})
            self.icnt += 1
        elif mnemonic in ['SOF', 'EXP', 'LOG', ]:
            mult = self.__s1_14__(mnemonic)
            self.__accept__('ARGSEP',opmsg)
            oft = self.__s_10__(mnemonic)
            self.pl.append({'cmd':[mnemonic, mult, oft], 'addr':self.icnt})
            self.icnt += 1
        elif mnemonic in ['RDAX', 'WRAX', 'MAXX', 'RDFX', 'WRLX', 'WRHX',]:
            reg = self.__register__(mnemonic)
            self.__accept__('ARGSEP',opmsg)
            mult = self.__s1_14__(mnemonic)
            self.pl.append({'cmd':[mnemonic, reg, mult], 'addr':self.icnt})
            self.icnt += 1
        elif mnemonic in ['MULX', ]:
            reg = self.__register__(mnemonic)
            self.pl.append({'cmd':[mnemonic, reg], 'addr':self.icnt})
            self.icnt += 1
        elif mnemonic in ['SKP','JMP']:
            condition = 0
            if mnemonic == 'SKP':
                condition = self.__condition__(mnemonic)
                self.__accept__('ARGSEP',opmsg)
            target = None
            offset = 0x00
            sourceline = self.sline
            if self.sym['type'] in ['LABEL','TARGET']:
                target = self.sym['stxt']
                self.__next__()
            else:
                offset = self.__offset__(mnemonic)
            self.pl.append({'cmd':['SKP', condition, offset],
                            'target':target,
                            'addr':self.icnt,
                            'line':sourceline})
            self.icnt += 1
        elif mnemonic in ['RDA', 'WRA', 'WRAP',] :
            addr = self.__d_15__(mnemonic)
            self.__accept__('ARGSEP',opmsg)
            mult = self.__s1_9__(mnemonic)
            self.pl.append({'cmd':[mnemonic, addr, mult], 'addr':self.icnt})
            self.icnt += 1
        elif mnemonic == 'RMPA':
            mult = self.__s1_9__(mnemonic)
            self.pl.append({'cmd':[mnemonic, mult], 'addr':self.icnt})
            self.icnt += 1
        elif mnemonic == 'WLDS':
            lfo = self.__lfo__(mnemonic)&0x01
            self.__accept__('ARGSEP',opmsg)
            freq = self.__lfo_sinfreq__(mnemonic)
            self.__accept__('ARGSEP',opmsg)
            amp = self.__d_15__(mnemonic)
            self.pl.append({'cmd':[mnemonic, lfo, freq, amp],
                            'addr':self.icnt})
            self.icnt += 1
        elif mnemonic == 'WLDR':
            lfo = self.__lfo__()|0x02
            self.__accept__('ARGSEP',opmsg)
            freq = self.__lfo_rampfreq__(mnemonic)
            self.__accept__('ARGSEP',opmsg)
            amp = self.__lfo_rampamp__(mnemonic)
            self.pl.append({'cmd':[mnemonic, lfo, freq, amp],
                            'addr':self.icnt})
            self.icnt += 1
        elif mnemonic == 'CHO':
            chotype = self.__chotype__()
            self.__accept__('ARGSEP',opmsg)
            lfo = self.__lfo__(mnemonic)
            flags = 0b000010
            arg = 0x00
            if chotype == 0x00:	# cho rda,lfo,flags,address
                self.__accept__('ARGSEP',opmsg)
                flags = self.__choflags__(lfo)
                self.__accept__('ARGSEP',opmsg)
                arg = self.__s_15a__(mnemonic)
            elif chotype == 0x02:	# cho sof,lfo,flags,offset
                self.__accept__('ARGSEP',opmsg)
                flags = self.__choflags__(lfo)
                self.__accept__('ARGSEP',opmsg)
                arg = self.__s_15a__(mnemonic)
            elif chotype == 0x3:	# cho rdal,lfo[,flags]
                if self.sym['type'] == 'ARGSEP':
                    self.__accept__('ARGSEP')
                    flags = self.__choflags__(lfo)
            self.pl.append({'cmd':['CHO', chotype, lfo, flags, arg],
                            'addr':self.icnt})
            self.icnt += 1
        elif mnemonic == 'JAM':
            lfo = self.__lfo__(mnemonic)|0x02
            self.pl.append({'cmd':[mnemonic, lfo], 'addr':self.icnt})
            self.icnt += 1
        elif mnemonic == 'CLR':
            # pseudo command
            self.pl.append({'cmd':['AND', 0x00],'addr':self.icnt})
            self.icnt += 1
        elif mnemonic == 'NOT':
            # pseudo command XOR
            self.pl.append({'cmd':['XOR', 0xffffff],'addr':self.icnt})
            self.icnt += 1
        elif mnemonic == 'NOP':
            # pseudo command SKP 0,0
            self.pl.append({'cmd':['NOP', 0x0],'addr':self.icnt})
            self.icnt += 1
        elif mnemonic == 'ABSA':
            # pseudo command MAXX 0,0
            self.pl.append({'cmd':['MAXX', 0x0, 0x0],'addr':self.icnt})
            self.icnt += 1
        elif mnemonic == 'LDAX':
            # pseudo command RDFX REG,0
            reg = self.__register__(mnemonic)
            self.pl.append({'cmd':['RDFX', reg, 0x0],'addr':self.icnt})
            self.icnt += 1
        elif mnemonic == 'RAW':
            # direct data insertion
            mark = self.__u_32__(mnemonic)
            self.pl.append({'cmd':['RAW', mark],'addr':self.icnt})
            self.icnt += 1
        else:
            self.parseerror('Unexpected instruction {}'.format(
                             self.sym['txt']))
            sys.exit(-4) # this is a major program error
        
        if self.sym['type'] == 'ARGSEP':
            self.parseerror('Excess operands skipped for ' + mnemonic)
            # skip to next checkpoint
            while self.sym['type'] not in ['EOF', 'MNEMONIC',
                                           'ASSEMBLER', 'LABEL']:
                self.__next__()


    def __deref__(self, label):
        """Return a value defined in the symbol table."""
        seen = set()
        look = label
        while True:
            if look in seen:
                self.parseerror('Circular definition of label '
                                 + label)
            if look in self.symtbl:
                look = self.symtbl[look]
                if not isinstance(look, str):
                    break
            else:
                self.parseerror('Value {} undefined for label {}'.format(
                                look,label))
            seen.add(label)
        return look

    def __expression__(self):
        """Parse an operand expression."""
        if self.spinreals and self.sym['type'] in ['ASSEMBLER','ARGSEP',
                                           'MNEMONIC','TARGET','EOF']:
            # assume the operand was omitted and replace with zero
            # but don't consume the token - hack for SpinASM compatibility
            self.parsewarn('Missing argument replaced with 0')
            return 0
        if self.sym['type'] in ['ASSEMBLER','EOF','MNEMONIC','TARGET']:
            self.parseerror('Unexpected {}'.format(self.sym['type']),
                            self.sline)
            return 0

        acc = 0
        try:
            acc = self.__or_expr__()
        except Exception as e:
            self.parseerror(str(e))

        # check type before proceeding
        if not isinstance(acc, (int, float)):
            self.parseerror('Expression result {} invalid type'.format(acc))
            acc = 0	# replace erroneous value with 0 and continue
        return acc

    def __or_expr__(self):
        """Parse an or expression."""
        acc = self.__xor_expr__()
        while self.sym['type'] == 'OPERATOR' and self.sym['stxt'] == '|':
            self.__next__()
            rarg = self.__xor_expr__()
            acc = acc | rarg
        return acc

    def __xor_expr__(self):
        """Parse an xor expression."""
        acc = self.__and_expr__()
        while self.sym['type'] == 'OPERATOR' and self.sym['stxt'] == '^':
            self.__next__()
            rarg = self.__and_expr__()
            acc = acc ^ rarg
        return acc

    def __and_expr__(self):
        """Parse an and expression."""
        acc = self.__shift_expr__()
        while self.sym['type'] == 'OPERATOR' and self.sym['stxt'] == '&':
            self.__next__()
            rarg = self.__shift_expr__()
            acc = acc & rarg
        return acc

    def __shift_expr__(self):
        """Parse a bitwise shift expression."""
        acc = self.__a_expr__()
        while self.sym['type']=='OPERATOR' and self.sym['stxt'] in ['<<','>>']:
            op = self.sym['stxt']
            self.__next__()
            rarg = self.__shift_expr__()
            if op == '<<':
                acc = acc << rarg
            else:
                acc = acc >> rarg
        return acc

    def __a_expr__(self):
        """Parse an addition expression."""
        acc = self.__m_expr__()
        while self.sym['type']=='OPERATOR' and self.sym['stxt'] in ['+','-']:
            op = self.sym['stxt']
            self.__next__()
            if op == '+':
                acc = acc + self.__m_expr__()
            else:
                acc = acc - self.__m_expr__()
        return acc

    def __m_expr__(self):
        """Parse a multiplicative expression."""
        acc = self.__u_expr__()
        while self.sym['type']=='OPERATOR' and self.sym['stxt'] in [
                                                          '*','//','/']:
            op = self.sym['stxt']
            self.__next__()
            rarg = self.__u_expr__()
            if op == '*':
                acc = acc * rarg
            elif op == '//':
                acc = acc // rarg
            else:
                acc = acc / rarg
        return acc

    def __u_expr__(self):
        """Parse a unary operator."""
        acc = 0
        if self.sym['type'] == 'OPERATOR' and self.sym['stxt'] in [
                                                     '+','-','~','!','INT']:
            op = self.sym['stxt']
            self.__next__()
            acc = self.__u_expr__()
            if op == '-':
                acc = -acc
            elif op == '~' or op == '!':
                acc = ~acc
            elif op == 'INT':
                acc = int(round(acc))
        else:
            acc = self.__power__()
        return acc

    def __power__(self):
        """Parse an exponent."""
        acc = self.__atom__()
        if self.sym['type'] == 'OPERATOR' and self.sym['stxt'] == '**':
            self.__next__()
            acc = acc ** self.__u_expr__()
        return acc

    def __atom__(self):
        """Parse an atom or start a new expression."""
        ret = 0
        if self.sym['type'] == 'OPERATOR' and self.sym['stxt'] == '(':
            self.__next__()
            ret = self.__expression__()
            if self.sym['type'] == 'OPERATOR' and self.sym['stxt'] == ')':
                self.__next__()
            else:
                self.parseerror("Expected ')' but saw {} {}".format(
                              self.sym['type'], self.sym['txt']),
                              self.sline)
        elif self.sym['type'] == 'LABEL':
            stxt = self.sym['stxt']
            if stxt in self.symtbl:
                ret = self.__deref__(stxt)
                self.__next__()
            else:
                self.parseerror('Undefined label {}'.format(self.sym['txt']),
                                 self.sline)
                self.__next__() # accept and replace
                ret = 0
        elif self.sym['type'] in ['INTEGER', 'FLOAT']:
            ret = self.sym['val']
            self.__next__()
        else:
            self.parseerror('Unexpected {} {} in expression'.format(
                              self.sym['type'], self.sym['txt']),
                              self.sline)
            if self.sym['type'] not in ['ARGSEP', 'TARGET', 'MNEMONIC',
                                        'EOF', 'ASSEMBLER']:
                self.__next__()	# skip the problem
            ret = 0
        return ret

    def __target__(self):
        """Parse a target assignment."""
        lbl = self.sym['stxt']
        oft = self.icnt
        if lbl in self.jmptbl and oft != self.jmptbl[lbl]:
            self.parseerror('Target {} redefined'.format(lbl),
                            self.sline)
        if lbl in self.symtbl:
            self.parseerror('Target {} already assigned'.format(lbl),
                            self.sline)
        self.jmptbl[lbl] = oft
        self.__next__()

    def __assembler__(self):
        """Parse mem or equ statement."""
        typ = None
        arg1 = None
        arg2 = None
        if self.sym['type'] == 'LABEL':
            arg1 = self.sym['stxt']
            self.__next__()
        if self.sym['type'] == 'ASSEMBLER':
            typ = self.sym['stxt']
            self.__next__()
        else:
            self.parseerror('Expected EQU or MEM but saw {} {}'.format(
                             self.sym['type'], self.sym['txt']),
                             self.sline)
            return

        if arg1 is None:
            if self.sym['type'] == 'LABEL':
                arg1 = self.sym['stxt']
                self.__next__()
            else:
                self.parseerror('Expected LABEL but saw {} {}'.format(
                             self.sym['type'], self.sym['txt']))
                return

        # strip the modifier and check for re-definition
        arg1 = arg1.rstrip('^#')
        if arg1 in ['RDAL','SOF','RDA']:	# disallowed re-assignments
            self.parseerror('Reserved label {} cannot be re-defined.'.format(
                            arg1))
            return
        if arg1 in self.symtbl:
            self.parsewarn('Label ' + arg1 + ' re-defined')

        # then fetch the second argument
        arg2 = self.__expression__()
         
        if typ == 'MEM':
            if int(arg2) == arg2:
                arg2 = int(arg2)
            else:
                self.parseerror('Memory {} length {} not integer'.format(
                                arg1, arg2))
                arg2 = 0
            # check memory and assign the extra labels
            baseval = self.delaymem
            if arg2 < 0 or arg2 > DELAYSIZE:	# not as in datasheet
                if self.doclamp:
                    if arg2 < 0:
                        arg2 = 0
                    elif arg2 > DELAYSIZE:
                        arg2 = DELAYSIZE
                else:
                    self.parseerror('Invalid memory size {}'.format(arg2))
                    arg2 = 0
            top = self.delaymem + arg2	# top ptr goes to largest addr+1
            if self.delaymem > DELAYSIZE:
                self.parseerror('Delay memory exhausted.')
            elif top > DELAYSIZE:
                self.parseerror(
            'Delay exhausted: requested {} exceeds {} available'.format(
                          arg2, DELAYSIZE-self.delaymem))
            self.symtbl[arg1] = self.delaymem
            self.symtbl[arg1+'#'] = top
            self.symtbl[arg1+'^'] = self.delaymem+arg2//2
            self.delaymem = top+1
        else:
            self.symtbl[arg1] = arg2	# re-assign symbol table entry

    def parse(self):
        """Parse input."""
        self.__next__()
        while self.sym['type'] != 'EOF':
            if self.sym['type'] == 'TARGET':
                self.__target__()
            elif self.sym['type'] == 'MNEMONIC':
                self.__instruction__()
            elif self.sym['type'] == 'LABEL' or self.sym['type'] == 'ASSEMBLER':
                self.__assembler__()
            else:
                self.parseerror('Unexpected {} {}'.format(
                                  self.sym['type'], self.sym['txt']),
                                  self.sline)
                while self.sym['type'] not in ['EOF', 'MNEMONIC',
                                           'ASSEMBLER', 'TARGET','LABEL']:
                    self.__next__()    # skip to next checkpoint
        # patch skip targets if required
        for i in self.pl:
            if i['cmd'][0] == 'SKP':
                if i['target'] is not None:
                    if i['target'] in self.jmptbl:
                        iloc = i['addr']
                        dest = self.jmptbl[i['target']]
                        if dest > iloc:
                            oft = dest - iloc - 1
                            if oft > M6:
                                self.parseerror(
                     'Offset from SKP to {0} ({1:#x}) too large'.format(
                                                i['target'],oft), i['line'])
                            else:
                                i['cmd'][2] = oft
                        else:
                            self.parseerror(
                     'Target {0} does not follow SKP'.format(i['target']),
                                                i['line'])
                    else:
                        self.parseerror('Undefined target {} for SKP'.format(
                                        i['target']), i['line'])
                else:
                    pass	# assume offset is immediate
        if self.ecount > 0:
            self.doerror('errors in input, assembly aborted')
            sys.exit(-3)
        self.__mkopcodes__()

def main():
    parser = argparse.ArgumentParser(
                description='Assemble a single FV-1 DSP program.')
    parser.add_argument('infile',
                        type=argparse.FileType('rb'),
                        help='program source file')
    parser.add_argument('outfile',
                        help='assembled output file')
    parser.add_argument('-q', '--quiet',
                        action='store_true',
                        help='suppress warnings')
    parser.add_argument('-v', '--version',
                        action='version',
                        help='print version',
                        version='%(prog)s ' + VERSION)
    parser.add_argument('-c', '--clamp',
                        action='store_true',
                        help='clamp out of range values without error')
    parser.add_argument('-s', '--spinreals',
                        action='store_true',
                        help="read literals 2,1 as float (SpinASM compatibility)")
    parser.add_argument('-p',
                        help='target program number',
                        type=int, choices=range(0,8))
    parser.add_argument('-b', '--binary',
                        action='store_true',
                        help='force binary output file')
    args = parser.parse_args()
    dowarn = warning
    if args.quiet:
        dowarn = quiet
    dowarn('FV-1 Assembler v' + VERSION)
    dowarn('info: Reading input from ' + args.infile.name)
    inbuf = args.infile.read()
    encoding = 'utf-8'
    # check for BOM
    if len(inbuf) > 2 and inbuf[0] == 0xFF and inbuf[1] == 0xFE:
        dowarn('info: Input encoding set to UTF-16LE by BOM')
        encoding = 'utf-16le'
    elif len(inbuf) > 2 and inbuf[0] == 0xFE and inbuf[1] == 0xFF:
        dowarn('info: Input encoding set to UTF-16BE by BOM')
        encoding = 'utf-16be'
    # or assume windows encoded 'ANSI'
    elif len(inbuf) > 7 and inbuf[7] == 0x00:
        dowarn('info: Input encoding set to UTF-16LE')
        encoding = 'utf-16le'

    # warn if spinreal option used
    if args.spinreals:
        dowarn('warning: SpinASM compatibility - literals 2,1 read as 2.0,1.0')

    fp = fv1parse(inbuf.decode(encoding,'replace'),
                  clamp=args.clamp, spinreals=args.spinreals,
                  wfunc=dowarn, efunc=error)
    fp.parse()
    
    ofile = None
    try:
        ofile = open(args.outfile, 'r+b')	# existing file
    except:
        try:
            ofile = open(args.outfile, 'w+b')	# create + truncate
        except Exception as e:
            error('error: writing output: ' + str(e))
            sys.exit(-1)
    ofile.seek(0)
    if not args.outfile.lower().endswith('hex'):
        args.binary = True
    if args.binary and ofile.isatty():	# superfluous now
        args.binary = False
        dowarn('warning: Terminal output forced to hex')
    if args.binary:
        dowarn('info: Writing binary output to ' + ofile.name)
        if args.p is not None:
            baseoft = args.p * 512
            ofile.seek(baseoft)
            dowarn('info: Selected program {0} at offset 0x{1:04X}'.format(
                    args.p, baseoft))
        else:
            ofile.truncate(0)
        ofile.write(fp.program)
    else:
        # HEX output - truncate and encode text 
        ofile.truncate(0)
        baseoft = 0
        if args.p is not None:
            baseoft = args.p * 512
            dowarn('info: Selected program {0} at offset 0x{1:04X}'.format(
                    args.p, baseoft))
        dowarn('info: Writing hex output to ' + ofile.name)
        ofile.write(bintoihex(fp.program, baseoft).encode('ASCII','ignore'))
    ofile.close()

if __name__ == '__main__':
    main()
