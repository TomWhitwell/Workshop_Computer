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
    bank.height[0]=731;bank.height[1]=3555;Record r=MakeRecord(bank);auto loaded=LoadRecord(reinterpret_cast<uint8_t*>(&r),sizeof(r));assert(!std::memcmp(&loaded,&bank,sizeof(bank)));
    r.checksum^=1;assert(LoadRecord(reinterpret_cast<uint8_t*>(&r),sizeof(r)).config[0].value[1]==1200);
    Config old;old.value[1]=2800;
    LegacyRecord legacy{kRecordMagic,1,Checksum(reinterpret_cast<uint8_t*>(&old),sizeof(old)),old};
    auto migrated=LoadRecord(reinterpret_cast<uint8_t*>(&legacy),sizeof(legacy));assert(migrated.config[0].value[1]==2800&&migrated.config[1].value[1]==2800&&migrated.mixer.value[0]==2048&&migrated.mixer.value[3]==0);
    legacy.checksum^=1;assert(LoadRecord(reinterpret_cast<uint8_t*>(&legacy),sizeof(legacy)).config[0].value[1]==1200);
    PreviousSettings previous{{bank.config[0],bank.config[1]},bank.mixer};
    PreviousRecord v2{kRecordMagic,2,Checksum(reinterpret_cast<uint8_t*>(&previous),sizeof(previous)),previous};
    auto upgraded=LoadRecord(reinterpret_cast<uint8_t*>(&v2),sizeof(v2));
    assert(upgraded.config[0].value[1]==100&&upgraded.config[1].value[1]==3000&&upgraded.mixer.value[4]==2700&&upgraded.config[2].value[1]==1200);
    uint8_t data[43];uint32_t mode=9;Settings target;
    auto n=EncodeSettings(bank,1,data);assert(n==43&&DecodeSettings(data,n,target,mode)&&mode==1);
    assert(target.config[0].value[1]==1200&&target.config[1].value[1]==3000&&target.mixer.value[4]==2700);
    Settings before=target;data[19]=1;assert(!DecodeSettings(data,n,target,mode));assert(!std::memcmp(&before,&target,sizeof(target)));
    EncodeSettings(bank,1,data);data[21]=127;assert(!DecodeSettings(data,n,target,mode));
    n=EncodeSettings(bank,0,data);assert(n==28&&DecodeSettings(data,n,target,mode)&&mode==0&&target.config[1].value[1]==3000);
    data[0]=3;assert(!DecodeSettings(data,n,target,mode));
    bank.config[2].value[1]=700;n=EncodeSettings(bank,2,data);
    assert(n==34&&DecodeSettings(data,n,target,mode)&&mode==2&&target.config[2].value[1]==700&&target.config[1].value[1]==3000);
    bank.movement=2;n=EncodeSettings(bank,2,data);assert(data[19]==13&&DecodeSettings(data,n,target,mode)&&target.movement==2);
    data[20]=3;assert(!DecodeSettings(data,n,target,mode)&&target.movement==2);
    data[20]=1;data[19]=7;assert(!DecodeSettings(data,n,target,mode));
    V3Settings prior{{bank.config[0],bank.config[1],bank.config[2]},bank.mixer};
    V3Record v3{kRecordMagic,3,Checksum(reinterpret_cast<uint8_t*>(&prior),sizeof(prior)),prior};
    auto third=LoadRecord(reinterpret_cast<uint8_t*>(&v3),sizeof(v3));assert(third.movement==0&&third.config[2].value[1]==700&&third.mixer.value[4]==2700);
    r=MakeRecord(bank);assert(LoadRecord(reinterpret_cast<uint8_t*>(&r),sizeof(r)).movement==2);
    auto invalid=bank;invalid.mixer.value[1]=5000;r=MakeRecord(invalid);assert(LoadRecord(reinterpret_cast<uint8_t*>(&r),sizeof(r)).mixer.value[1]==0);
    assert(loaded.height[0]==731&&loaded.height[1]==3555);
    assert(migrated.height[0]==2048&&upgraded.height[1]==2048&&third.height[0]==2048);
    V4Settings fourth{{bank.config[0],bank.config[1],bank.config[2]},bank.mixer,2,0};
    V4Record v4{kRecordMagic,4,Checksum(reinterpret_cast<uint8_t*>(&fourth),sizeof(fourth)),fourth};
    auto fourthLoaded=LoadRecord(reinterpret_cast<uint8_t*>(&v4),sizeof(v4));
    assert(fourthLoaded.movement==2&&fourthLoaded.height[0]==2048&&fourthLoaded.height[1]==2048&&fourthLoaded.mixer.value[4]==2700);
    bank.height[0]=0;bank.height[1]=4096;n=EncodeSettings(bank,1,data);
    assert(DecodeSettings(data,n,target,mode)&&target.height[0]==0&&target.height[1]==4096);
    before=target;data[41]=1;assert(!DecodeSettings(data,n,target,mode)&&!std::memcmp(&before,&target,sizeof(target)));
    EncodeSettings(bank,1,data);data[40]=14;assert(!DecodeSettings(data,n,target,mode));
    EncodeSettings(bank,1,data);assert(!DecodeSettings(data,n-3,target,mode));
    invalid=bank;invalid.height[0]=4097;r=MakeRecord(invalid);assert(LoadRecord(reinterpret_cast<uint8_t*>(&r),sizeof(r)).height[0]==2048);
    bank.vertical_centre=3072;bank.vertical_depth=1777;r=MakeRecord(bank);
    auto sixth=LoadRecord(reinterpret_cast<uint8_t*>(&r),sizeof(r));assert(sixth.vertical_centre==3072&&sixth.vertical_depth==1777&&sixth.height[1]==4096);
    V5Settings fifth{{bank.config[0],bank.config[1],bank.config[2]},bank.mixer,bank.movement,0,{bank.height[0],bank.height[1]}};
    V5Record v5{kRecordMagic,5,Checksum(reinterpret_cast<uint8_t*>(&fifth),sizeof(fifth)),fifth};
    auto fifthLoaded=LoadRecord(reinterpret_cast<uint8_t*>(&v5),sizeof(v5));assert(fifthLoaded.height[1]==4096&&fifthLoaded.vertical_centre==2048&&fifthLoaded.vertical_depth==0&&fifthLoaded.movement==2);
    n=EncodeSettings(bank,2,data);assert(n==34&&DecodeSettings(data,n,target,mode)&&target.vertical_centre==3072&&target.vertical_depth==1777);
    before=target;data[27]=17;assert(!DecodeSettings(data,n,target,mode)&&!std::memcmp(&before,&target,sizeof(target)));
    EncodeSettings(bank,2,data);data[22]=14;assert(!DecodeSettings(data,n,target,mode));
    EncodeSettings(bank,2,data);assert(!DecodeSettings(data,n-3,target,mode));
    invalid=bank;invalid.vertical_depth=2049;r=MakeRecord(invalid);assert(LoadRecord(reinterpret_cast<uint8_t*>(&r),sizeof(r)).vertical_depth==0);
    assert(migrated.vertical_depth==0&&upgraded.vertical_depth==0&&third.vertical_depth==0&&fourthLoaded.vertical_depth==0);
    Modes modes;modes.RestoreMixer(bank.mixer,2048,0,4095);modes.Controls(Mode::Mixer,2048,0,4095,0,0,false,bank.config[1]);
    auto scene=modes.Advance();assert(scene.angle[0]==0x40000000u&&scene.angle[1]==0xc0000000u&&scene.distance==1000&&scene.distance_b==2700&&scene.level_a==2300&&modes.Pickup()==0);
    modes.Controls(Mode::Mixer,3072,1000,2300,0,0,false,bank.config[1]);assert(modes.Pickup()==7);
    std::puts("PASS: separate-mode records, v1 migration, checksums/corruption, transactional tagged decode, inactive-bank retention and restored-placement pickup");
}
