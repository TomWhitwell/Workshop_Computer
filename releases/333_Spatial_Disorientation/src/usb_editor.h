// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Transport structure follows Chris Johnson's ComputerCard web_interface example.
// Fixed buffers, bounded parser and timeouts replace its dynamic allocations.
#pragma once
#include "shared.h"
#include "storage.h"
#include "midi_tx.h"
#include "dsp/hrtf_table.h"
#include "pico/stdlib.h"
#include "tusb.h"
namespace spatial {
class UsbEditor {
public:
    UsbEditor(Shared& shared,Storage& storage,const Settings& cfg,void (*worker)()):shared_(shared),storage_(storage),current_(cfg),worker_(worker) {}
    void Run() {
        tud_init(0); // Only the selected role starts on the single USB controller.
        __dmb();shared_.usb_ready=1;
        while (true) {
            worker_();
            tud_task();
            Pump();
            uint8_t chunk[64];
            if (tud_midi_available()) {
                uint32_t count=tud_midi_stream_read(chunk,sizeof(chunk));
                for (uint32_t i=0;i<count;++i) Byte(chunk[i]);
            }
            if (pending_ && !shared_.ready) {
                __dmb(); current_=pending_cfg_;pending_=false;
                Ack(2,pending_seq_,0);
            }
            uint32_t now=time_us_32();
            if (now-last_telemetry_>=50000) {
                last_telemetry_=now;
                uint8_t data[13];
                Pack(shared_.angle_a,data);Pack(shared_.angle_b,data+2);
                Pack(shared_.distance,data+4);data[6]=shared_.flags&127;data[7]=shared_.mode;Pack(shared_.distance_b,data+8);data[10]=shared_.mixer_state;data[11]=shared_.frozen;
                if(tx_.Empty()) {
                    data[12]=shared_.movement;
                    Send(0x43,0,data,sizeof(data));
                    uint8_t timing[5];
                    Pack(shared_.callback_peak_us>16383?16383:shared_.callback_peak_us,timing);
                    Pack(shared_.block_peak_us>16383?16383:shared_.block_peak_us,timing+2);
                    timing[4]=kHrtfTaps;
                    Send(0x44,0,timing,sizeof(timing));
                }
            }
            tight_loop_contents();
        }
    }
private:
    static void Pack(uint32_t v,uint8_t* dest) { dest[0]=v&127;dest[1]=(v>>7)&127; }
    bool Send(uint8_t command,uint8_t seq,const uint8_t* data,size_t n) {
        uint8_t msg[64]={0xf0,0x7d,0x53,0x44,kProtocolVersion,command,seq};
        if (n>55 || !tud_midi_mounted()) return false;
        for (size_t i=0;i<n;++i) msg[7+i]=data[i];
        msg[7+n]=0xf7;
        return tx_.Enqueue(msg,n+8);
    }
    void Pump() {
        if(!tud_midi_mounted()){tx_.Clear();return;}
        size_t count;const uint8_t* data=tx_.Front(count);
        if(!count)return;
        // One non-blocking FIFO write; defer remaining bytes to another pass.
        tx_.Consume(tud_midi_stream_write(0,data,count));
    }
    void Ack(uint8_t cmd,uint8_t seq,uint8_t status) { const uint8_t data[]={cmd,status};Send(0x42,seq,data,2); }
    bool Capture(){
        shared_.snapshot_request=1;uint32_t start=time_us_32();
        while(shared_.snapshot_request!=2&&time_us_32()-start<500000){worker_();tud_task();Pump();}
        bool okay=shared_.snapshot_request==2;
        if(okay){__dmb();current_=shared_.snapshot;}
        __dmb();shared_.snapshot_request=0;return okay;
    }
    void Snapshot(uint8_t seq) {
        if(shared_.mode>2||!Capture()){Ack(1,seq,4);return;}
        uint8_t data[37];size_t n=EncodeSettings(current_,shared_.mode,data);Send(0x41,seq,data,n);
    }
    void Message() {
        if (length_<6 || rx_[0]!=0x7d || rx_[1]!=0x53 || rx_[2]!=0x44) return;
        uint8_t command=rx_[4],seq=rx_[5];
        if (rx_[3]!=kProtocolVersion) { Ack(command,seq,2);return; }
        if (pending_) { Ack(command,seq,4);return; }
        if (command==1 && length_==6) Snapshot(seq);
        else if (command==2) {
            if(!Capture()){Ack(command,seq,4);return;}
            Settings cfg=current_;uint32_t mode;
            if (!DecodeSettings(rx_+6,length_-6,cfg,mode)||mode!=shared_.mode) { Ack(command,seq,3);return; }
            pending_cfg_=cfg;pending_seq_=seq;pending_=true;shared_.Publish(cfg);
        } else if (command==3 && length_==6) {
            // Explicit Save creates a brief audio pause, never a flash write in
            // the ISR. Zero reaches the DAC before core 0 is locked out.
            if(shared_.mode>2||!Capture()){Ack(command,seq,4);return;}
            __dmb();shared_.save=1;
            uint32_t start=time_us_32();
            while (shared_.save!=2 && time_us_32()-start<500000) { worker_();tud_task();Pump(); }
            bool okay=false;
            if (shared_.save==2) { __dmb();okay=storage_.Save(current_); }
            __dmb();shared_.save=0;
            Ack(command,seq,okay ? 0 : 5);
        } else Ack(command,seq,1);
    }
    void Byte(uint8_t value) {
        if (value>=0xf8) return; // MIDI realtime may occur inside SysEx.
        if (value==0xf0) { active_=true;length_=0;return; }
        if (!active_) return;
        if (value==0xf7) { active_=false;Message();return; }
        if (value>127 || length_>=sizeof(rx_)) { active_=false;length_=0;return; }
        rx_[length_++]=value;
    }
    Shared& shared_;
    Storage& storage_;
    Settings current_,pending_cfg_;
    uint8_t rx_[64]={},pending_seq_=0;
    size_t length_=0;
    bool active_=false,pending_=false;
    uint32_t last_telemetry_=0;
    void (*worker_)();
    MidiTx tx_;
};
}
