#!/usr/bin/env python3
"""Exercise the firmware's actual serialization/save methods with simulated flash.

This checks data and readback behaviour; ARM stack use must be checked separately.
"""
from pathlib import Path
import re,subprocess,tempfile
s=(Path(__file__).resolve().parents[1]/'C1ZZL3.cpp').read_text()
def method(name):
 m=re.search(r'^    [\w &:]+ '+name+r'\([^;{}]*\)\s*\n    \{',s,re.M);assert m,name
 i=s.index('{',m.start());j=i+1;depth=1
 while depth:
  depth+=(s[j]=='{')-(s[j]=='}');j+=1
 return s[m.start():j]
start=s.index('    enum class EnvelopePreset');end=s.index('    // =========================================================\n    // CZ OSCILLATOR',start)
types=s[start:end]
methods=['activeCustomEnvelopeSlot','save8muEnvelope','currentCustomEnvelopeState','savedEnvelopeFromRuntime','runtimeEnvelopeFromSaved','sanitizeSustainStage','customEnvelopeMask','copyEnvelopeName','checksumCustomEnvelopeState','flashCustomEnvelopeState','isValidCustomEnvelopeState','customEnvelopeStateMatches','saveCustomEnvelopeState']
code='''#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#define PICO_FLASH_SIZE_BYTES 2097152u
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
alignas(8) uint8_t fakeFlash[PICO_FLASH_SIZE_BYTES];
#define XIP_BASE reinterpret_cast<uintptr_t>(fakeFlash)
int erases=0; bool corruptWrite=false;
uint32_t save_and_disable_interrupts(){return 0;}
void restore_interrupts(uint32_t){}
void flash_range_erase(uint32_t offset,uint32_t n){assert(n==4096);std::memset(fakeFlash+offset,255,n);++erases;}
void flash_range_program(uint32_t offset,const uint8_t* data,uint32_t n){if (!corruptWrite) std::memcpy(fakeFlash+offset,data,n);}
class Card {public:
'''+types+'''
uint8_t envelopePreset=0;
EnvelopeProgram customEnvelopes[8]={},customEnvelopeSaved[8]={};
bool customEnvelopePersist[8]={};
uint8_t customEnvelopeNames[8][16]={};
SavedSlotPerformanceState customEnvelopeSavedPerformances[8]={},customEnvelopePerformances[8]={};
volatile uint32_t saveConfirmSamples=0;
SavedSlotPerformanceState currentSlotPerformanceState(){SavedSlotPerformanceState p={};p.pdControl=1234;return p;}
'''+ '\n'.join(method(n) for n in methods)+'''
};
int main(){
 std::memset(fakeFlash,255,sizeof fakeFlash);
 Card c;c.customEnvelopePersist[2]=true;c.envelopePreset=Card::CustomEnvelopePreset+2;
 auto& e=c.customEnvelopes[2];
 for(int i=0;i<8;++i){e.amp[i]={uint16_t(100+i),uint32_t(1000+i)};e.amp2[i]={uint16_t(200+i),uint32_t(2000+i)};e.pd[i]={uint16_t(300+i),uint32_t(3000+i)};e.pd2[i]={uint16_t(400+i),uint32_t(4000+i)};e.pitch[i]={uint16_t(500+i),uint32_t(5000+i)};e.pitch2[i]={uint16_t(600+i),uint32_t(6000+i)};}
 c.save8muEnvelope();assert(erases==1);
 assert(c.saveConfirmSamples==Card::SaveConfirmSamples);
 assert(c.customEnvelopePerformances[2].pdControl==1234);
 Card restart;const auto& stored=restart.flashCustomEnvelopeState();
 assert(restart.isValidCustomEnvelopeState(stored));assert(stored.loadedMask==4);
 auto loaded=restart.runtimeEnvelopeFromSaved(stored.slots[2]);
 assert(std::memcmp(&e,&loaded,sizeof e)==0);assert(stored.performances[2].pdControl==1234);
 c.save8muEnvelope();assert(erases==1);
 c.saveConfirmSamples=0;c.envelopePreset=0;c.customEnvelopes[2].amp[0].level=999;c.save8muEnvelope();assert(erases==1);assert(c.saveConfirmSamples==0);
 c.envelopePreset=Card::CustomEnvelopePreset+2;corruptWrite=true;
 c.save8muEnvelope();assert(erases==2);assert(c.saveConfirmSamples==0);
 assert(!c.isValidCustomEnvelopeState(c.flashCustomEnvelopeState()));
 puts("C save serializes all six edited lanes and performance to valid flash state; a new runtime reads the same envelope. Factory selection does not save; failed flash readback does not confirm success.");
}
'''
with tempfile.TemporaryDirectory(prefix='gnarly-save-test-') as directory:
    cpp=Path(directory)/'save_test.cpp'
    binary=Path(directory)/'save_test'
    cpp.write_text(code)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
