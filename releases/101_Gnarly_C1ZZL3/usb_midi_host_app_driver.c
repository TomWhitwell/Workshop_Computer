/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 rppicomidi
 */

#include "tusb_option.h"

#if (TUSB_OPT_HOST_ENABLED)

#include "host/usbh.h"
#include "host/usbh_pvt.h"

#include "usb_midi_host.h"

usbh_class_driver_t const* usbh_app_driver_get_cb(uint8_t* driver_count)
{
    static usbh_class_driver_t host_driver = {
    #if CFG_TUSB_DEBUG >= 2
        .name = "MIDIH",
    #endif
        .init = midih_init,
        .deinit = midih_deinit,
        .open = midih_open,
        .set_config = midih_set_config,
        .xfer_cb = midih_xfer_cb,
        .close = midih_close
    };
    *driver_count = 1;
    return &host_driver;
}

#endif
