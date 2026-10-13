// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include "config.h"
#include <cstring>
namespace spatial {
constexpr uint8_t kProtocolVersion=3;
struct Placement {
    uint16_t value[6]={2048,0,4095,0,0,4095}; // A then B: position, distance, level.
    bool Valid()const{for(auto v:value)if(v>4095)return false;return true;}
};
struct Settings {
    Config config[3];Placement mixer;
    uint16_t movement=0,reserved=0; // 0 Fig8, 1 Pendulum, 2 Wander.
    bool Valid()const{return config[0].Valid()&&config[1].Valid()&&config[2].Valid()&&mixer.Valid()&&movement<3&&reserved==0;}
};
static_assert(sizeof(Settings)==52,"Stable flash layout");
inline bool DecodeSettings(const uint8_t* data,size_t n,Settings& settings,uint32_t& mode){
    if(!n||data[0]>2)return false;
    uint32_t m=data[0];size_t fields=m==1?12:(m==2?7:6);
    if(n!=1+fields*3)return false;
    Settings candidate=settings;Config cfg;Placement place;uint16_t movement=0;
    uint32_t seen=0;
    for(size_t i=1;i<n;i+=3){
        uint32_t id=data[i];if(id<1||(m==2?(id>6&&id!=13):id>fields)||data[i+1]>127||data[i+2]>127)return false;
        uint32_t bit=1u<<(id-1);if(seen&bit)return false;seen|=bit;
        uint16_t v=data[i+1]|(uint16_t(data[i+2])<<7);
        if(id<=6)cfg.value[id-1]=v;else if(id==13)movement=v;else place.value[id-7]=v;
    }
    if(!cfg.Valid()||(m==1&&!place.Valid())||(m==2&&movement>2))return false;
    candidate.config[m]=cfg;if(m==1)candidate.mixer=place;else if(m==2)candidate.movement=movement;
    settings=candidate;mode=m;return true;
}
inline size_t EncodeSettings(const Settings& settings,uint32_t mode,uint8_t* data){
    data[0]=mode;EncodeConfig(settings.config[mode],data+1);
    if(mode==1)for(int i=0;i<6;++i){data[19+i*3]=i+7;data[20+i*3]=settings.mixer.value[i]&127;data[21+i*3]=settings.mixer.value[i]>>7;}
    if(mode==2){data[19]=13;data[20]=settings.movement;data[21]=0;}
    return mode==1?37:(mode==2?22:19);
}
constexpr uint32_t kRecordMagic=0x314f4453;
struct Record {uint32_t magic,version,checksum;Settings settings;};
struct V3Settings {Config config[3];Placement mixer;};
struct V3Record {uint32_t magic,version,checksum;V3Settings settings;};
struct PreviousSettings {Config config[2];Placement mixer;};
struct PreviousRecord {uint32_t magic,version,checksum;PreviousSettings settings;};
struct LegacyRecord {uint32_t magic,version,checksum;Config config;};
static_assert(sizeof(Record)==64&&sizeof(V3Record)==60&&sizeof(PreviousRecord)==48&&sizeof(LegacyRecord)==24,"Stable record layouts");
inline Record MakeRecord(const Settings& settings){return {kRecordMagic,4,Checksum(reinterpret_cast<const uint8_t*>(&settings),sizeof(settings)),settings};}
inline Settings LoadRecord(const uint8_t* data,size_t n){
    Settings result;
    if(n<sizeof(LegacyRecord))return result;
    LegacyRecord old;std::memcpy(&old,data,sizeof(old));
    if(old.magic!=kRecordMagic)return result;
    if(old.version==1&&old.config.Valid()&&old.checksum==Checksum(reinterpret_cast<const uint8_t*>(&old.config),sizeof(Config))){
        result.config[0]=result.config[1]=old.config;return result;
    }
    if(old.version==2&&n>=sizeof(PreviousRecord)){
        PreviousRecord previous;std::memcpy(&previous,data,sizeof(previous));
        auto& s=previous.settings;
        if(s.config[0].Valid()&&s.config[1].Valid()&&s.mixer.Valid()&&previous.checksum==Checksum(reinterpret_cast<const uint8_t*>(&s),sizeof(s))){
            result.config[0]=s.config[0];result.config[1]=s.config[1];result.mixer=s.mixer;return result;
        }
    }
    if(old.version==3&&n>=sizeof(V3Record)){
        V3Record previous;std::memcpy(&previous,data,sizeof(previous));auto& s=previous.settings;
        if(s.config[0].Valid()&&s.config[1].Valid()&&s.config[2].Valid()&&s.mixer.Valid()&&previous.checksum==Checksum(reinterpret_cast<const uint8_t*>(&s),sizeof(s))){
            for(int i=0;i<3;++i)result.config[i]=s.config[i];
            result.mixer=s.mixer;
        }
        return result;
    }
    if(old.version==4&&n>=sizeof(Record)){
        Record current;std::memcpy(&current,data,sizeof(current));
        if(current.settings.Valid()&&current.checksum==Checksum(reinterpret_cast<const uint8_t*>(&current.settings),sizeof(Settings)))return current.settings;
    }
    return result;
}
}
