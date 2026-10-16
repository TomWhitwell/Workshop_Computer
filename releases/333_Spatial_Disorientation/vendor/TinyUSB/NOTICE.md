# TinyUSB build-source notices

TinyUSB revision `86ad6e56c1700e85f1c5678607a762cfe3aa2f47`, supplied by
Pico SDK 2.3.0 (`98a542c1a62fb549ffb5d66a3e5892b06276b670`).
Source: https://github.com/hathach/tinyusb/tree/86ad6e56c1700e85f1c5678607a762cfe3aa2f47

The unmodified upstream MIT license is in LICENSE. Additional upstream copyright
lines below are preserved from the source files listed by the alpha9 build.
Some classes compile to empty objects because their features are disabled.
These notices accompany the source and firmware; no TinyUSB source is vendored here.
Notice compilation: Adrian Vos (soveda), 2026, MIT; quoted notices retain ownership.

```text
hw/bsp/rp2040/family.c:
Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
Copyright (c) 2021, Ha Thach (tinyusb.org)

src/class/audio/audio_device.c:
Copyright (c) 2020 Reinhard Panhuber, Jerzy Kasenberg
Copyright (c) 2023 HiFiPhile

src/class/cdc/cdc_device.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/class/cdc/cdc_host.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/class/dfu/dfu_device.c:
Copyright (c) 2021 XMOS LIMITED

src/class/dfu/dfu_rt_device.c:
Copyright (c) 2019 Sylvain Munaut <tnt@246tNt.com>

src/class/hid/hid_device.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/class/hid/hid_host.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/class/midi/midi_device.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/class/msc/msc_device.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/class/msc/msc_host.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/class/net/ecm_rndis_device.c:
Copyright (c) 2020 Peter Lawrence
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/class/net/ncm_device.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)
Copyright (c) 2024 Hardy Griech
Copyright (c) 2020 Jacob Berg Potter
Copyright (c) 2020 Peter Lawrence

src/class/usbtmc/usbtmc_device.c:
Copyright (c) 2019 Nathan Conrad

src/class/vendor/vendor_device.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/class/vendor/vendor_host.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/class/video/video_device.c:
Copyright (c) 2021 Koji KITAYAMA
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/common/tusb_fifo.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)
Copyright (c) 2020 Reinhard Panhuber - rework to unmasked pointers

src/device/usbd.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/device/usbd_control.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/host/hub.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/host/usbh.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)

src/portable/raspberrypi/rp2040/dcd_rp2040.c:
Copyright (c) 2020 Raspberry Pi (Trading) Ltd.

src/portable/raspberrypi/rp2040/hcd_rp2040.c:
Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
Copyright (c) 2021 Ha Thach (tinyusb.org) for Double Buffered

src/portable/raspberrypi/rp2040/rp2040_usb.c:
Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
Copyright (c) 2021 Ha Thach (tinyusb.org) for Double Buffered

src/tusb.c:
Copyright (c) 2019 Ha Thach (tinyusb.org)
```
