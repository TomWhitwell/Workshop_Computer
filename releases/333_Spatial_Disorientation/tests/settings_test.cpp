// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "settings.h"
#include "modes.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace spatial;
int main(){
    Settings bank;bank.config[0].value[1]=100;bank.config[1].value[1]=3000;
    bank.mixer={{3072,1000,2300,1024,2700,1800}};
    Record r=MakeRecord(bank);auto loaded=LoadRecord(reinterpret_cast<uint8_t*>(&r),sizeof(r));assert(!std::memcmp(&loaded,&bank,sizeof(bank)));
    r.checksum^=1;assert(LoadRecord(reinterpret_cast<uint8_t*>(&r),sizeof(r)).config[0].value[1]==1200);
    Config old;old.value[1]=2800;
    LegacyRecord legacy{kRecordMagic,1,Checksum(reinterpret_cast<uint8_t*>(&old),sizeof(old)),old};
    auto migrated=LoadRecord(reinterpret_cast<uint8_t*>(&legacy),sizeof(legacy));assert(migrated.config[0].value[1]==2800&&migrated.config[1].value[1]==2800&&migrated.mixer.value[0]==2048&&migrated.mixer.value[3]==0);
    legacy.checksum^=1;assert(LoadRecord(reinterpret_cast<uint8_t*>(&legacy),sizeof(legacy)).config[0].value[1]==1200);
    PreviousSettings previous{{bank.config[0],bank.config[1]},bank.mixer};
    PreviousRecord v2{kRecordMagic,2,Checksum(reinterpret_cast<uint8_t*>(&previous),sizeof(previous)),previous};
    auto upgraded=LoadRecord(reinterpret_cast<uint8_t*>(&v2),sizeof(v2));
    assert(upgraded.config[0].value[1]==100&&upgraded.config[1].value[1]==3000&&upgraded.mixer.value[4]==2700&&upgraded.config[2].value[1]==1200);
    uint8_t data[37];uint32_t mode=9;Settings target;
    auto n=EncodeSettings(bank,1,data);assert(n==37&&DecodeSettings(data,n,target,mode)&&mode==1);
    assert(target.config[0].value[1]==1200&&target.config[1].value[1]==3000&&target.mixer.value[4]==2700);
    Settings before=target;data[19]=1;assert(!DecodeSettings(data,n,target,mode));assert(!std::memcmp(&before,&target,sizeof(target)));
    EncodeSettings(bank,1,data);data[21]=127;assert(!DecodeSettings(data,n,target,mode));
    n=EncodeSettings(bank,0,data);assert(n==19&&DecodeSettings(data,n,target,mode)&&mode==0&&target.config[1].value[1]==3000);
    data[0]=3;assert(!DecodeSettings(data,n,target,mode));
    bank.config[2].value[1]=700;n=EncodeSettings(bank,2,data);
    assert(n==22&&DecodeSettings(data,n,target,mode)&&mode==2&&target.config[2].value[1]==700&&target.config[1].value[1]==3000);
    bank.movement=2;n=EncodeSettings(bank,2,data);assert(data[19]==13&&DecodeSettings(data,n,target,mode)&&target.movement==2);
    data[20]=3;assert(!DecodeSettings(data,n,target,mode)&&target.movement==2);
    data[20]=1;data[19]=7;assert(!DecodeSettings(data,n,target,mode));
    V3Settings prior{{bank.config[0],bank.config[1],bank.config[2]},bank.mixer};
    V3Record v3{kRecordMagic,3,Checksum(reinterpret_cast<uint8_t*>(&prior),sizeof(prior)),prior};
    auto third=LoadRecord(reinterpret_cast<uint8_t*>(&v3),sizeof(v3));assert(third.movement==0&&third.config[2].value[1]==700&&third.mixer.value[4]==2700);
    r=MakeRecord(bank);assert(LoadRecord(reinterpret_cast<uint8_t*>(&r),sizeof(r)).movement==2);
    auto invalid=bank;invalid.mixer.value[1]=5000;r=MakeRecord(invalid);assert(LoadRecord(reinterpret_cast<uint8_t*>(&r),sizeof(r)).mixer.value[1]==0);
    Modes modes;modes.RestoreMixer(bank.mixer,2048,0,4095);modes.Controls(Mode::Mixer,2048,0,4095,0,0,false,bank.config[1]);
    auto scene=modes.Advance();assert(scene.angle[0]==0x40000000u&&scene.angle[1]==0xc0000000u&&scene.distance==1000&&scene.distance_b==2700&&scene.level_a==2300&&modes.Pickup()==0);
    modes.Controls(Mode::Mixer,3072,1000,2300,0,0,false,bank.config[1]);assert(modes.Pickup()==7);
    std::puts("PASS: separate-mode records, v1 migration, checksums/corruption, transactional tagged decode, inactive-bank retention and restored-placement pickup");
}
