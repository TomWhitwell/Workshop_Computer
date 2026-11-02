#!/usr/bin/env python3
"""Read local USB CDC telemetry with no third-party Python dependencies.
Commands: s = snapshot, b = reboot BOOTSEL, d = direct bypass, e = emulator, m = mute (DSP still running), 1–6 = slot, c = sample capture.
"""
import argparse,os,select,termios,time,tty
p=argparse.ArgumentParser(description=__doc__);p.add_argument('port');p.add_argument('--seconds',type=float,default=8);p.add_argument('--command',default='s');a=p.parse_args()
fd=os.open(a.port,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
try:
 tty.setraw(fd);attr=termios.tcgetattr(fd);attr[4]=termios.B115200;attr[5]=termios.B115200;termios.tcsetattr(fd,termios.TCSANOW,attr)
 if a.command:os.write(fd,a.command.encode('ascii'))
 pending=b'';end=time.monotonic()+a.seconds
 while time.monotonic()<end:
  if select.select([fd],[],[],min(.25,max(0,end-time.monotonic())))[0]:
   try:b=os.read(fd,65536)
   except OSError:break
   if not b:break
   pending+=b
   while b'\n' in pending:
    line,pending=pending.split(b'\n',1)
    print(line.decode('utf8','replace').rstrip('\r'),flush=True)
finally:os.close(fd)
