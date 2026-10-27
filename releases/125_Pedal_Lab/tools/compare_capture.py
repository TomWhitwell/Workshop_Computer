#!/usr/bin/env python3
"""Align two 16-bit PCM WAV captures and report residual, gain and correlation.
Requires numpy. Compare identical deterministic stimuli and static pot/clock
settings. This tool measures a supplied capture; it does not certify FV-1 fidelity.
"""
import argparse,json,wave
import numpy as np

def read(p):
    with wave.open(p,'rb') as f:
        if f.getsampwidth()!=2:raise ValueError('16-bit PCM WAV required')
        return f.getframerate(),np.frombuffer(f.readframes(f.getnframes()),dtype='<i2').reshape(-1,f.getnchannels()).astype(float)/32768
p=argparse.ArgumentParser(description=__doc__);p.add_argument('reference');p.add_argument('capture');p.add_argument('--max-lag',type=int,default=4800)
a=p.parse_args();rate,x=read(a.reference);rate2,y=read(a.capture)
if rate!=rate2 or x.shape[1]!=y.shape[1]:p.error('Sample rates and channel counts must match')
if a.max_lag<0:p.error('--max-lag must be >=0')
n=min(len(x),len(y));x=x[:n];y=y[:n]
if n<=a.max_lag*2+10:p.error('Capture too short for lag window')
# FFT correlation, jointly using both channels. Positive lag means capture lags.
nfft=1<<(2*n-1).bit_length();corr=np.fft.irfft(np.sum(np.conj(np.fft.rfft(x,nfft,axis=0))*np.fft.rfft(y,nfft,axis=0),axis=1),nfft)
lags=np.arange(-a.max_lag,a.max_lag+1);lag=int(lags[np.argmax(np.abs(corr[lags%nfft]))])
if lag>0:x=x[:-lag];y=y[lag:]
elif lag<0:x=x[-lag:];y=y[:lag]
x=x-x.mean(axis=0);y=y-y.mean(axis=0)
power=float(np.sum(x*x))
if power<1e-15:p.error('Reference is silent')
gain=float(np.sum(x*y)/power);res=y-gain*x
rms=lambda z:float(np.sqrt(np.mean(z*z)))
rms_y=rms(y);res_rms=rms(res)
result={'rate_hz':rate,'lag_samples':lag,'capture_gain':gain,'gain_db':float(20*np.log10(abs(gain)+1e-30)),
        'correlation':float(np.sum(x*y)/(np.sqrt(np.sum(x*x)*np.sum(y*y))+1e-30)),
        'residual_db_relative_to_capture':float(20*np.log10((res_rms+1e-30)/(rms_y+1e-30))),
        'uncompensated_rms_difference':rms(y-x),'note':'Alignment and constant gain correction do not correct clock drift, frequency response, or nonlinear distortion.'}
print(json.dumps(result,indent=2))
