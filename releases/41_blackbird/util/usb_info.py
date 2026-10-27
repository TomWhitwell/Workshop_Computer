#!/usr/bin/env python3
"""
Quick utility: find USB devices matching a substring and print descriptor details,
including MIDI class-specific descriptors (Audio class, MIDI streaming subclass).
Requires pyusb and a libusb backend.
"""
import argparse
import sys
import usb.core
import usb.util


def str_or_dash(dev, field):
    try:
        return usb.util.get_string(dev, getattr(dev, field)) or "-"
    except Exception:
        return "-"


def matches(dev, needle):
    for field in ("iManufacturer", "iProduct", "iSerialNumber"):
        if needle in str_or_dash(dev, field).lower():
            return True
    return False


def coerce_extra_bytes(extra):
    if extra is None:
        return b""
    if isinstance(extra, (bytes, bytearray, memoryview)):
        return bytes(extra)
    if isinstance(extra, (list, tuple)):
        if all(isinstance(x, int) for x in extra):
            return bytes(extra)
        chunks = []
        for x in extra:
            try:
                chunks.append(bytes(x))
            except Exception:
                return b""
        return b"".join(chunks)
    try:
        return bytes(extra)
    except Exception:
        return b""


def iter_extra(extra):
    data = coerce_extra_bytes(extra)
    if not data:
        return []
    parts = []
    offset = 0
    while offset + 2 <= len(data):
        length = data[offset]
        if length < 2:
            break
        chunk = data[offset : offset + length]
        parts.append(chunk)
        offset += length
    return parts


def describe_midi_chunk(chunk):
    if len(chunk) < 3:
        return None
    dtype = chunk[1]
    subtype = chunk[2]
    if dtype not in (0x24, 0x25):  # CS_INTERFACE or CS_ENDPOINT
        return None

    if dtype == 0x24:  # CS_INTERFACE
        if subtype == 0x01 and len(chunk) >= 7:  # MS Header
            bcd_msc = chunk[3] | (chunk[4] << 8)
            total = chunk[5] | (chunk[6] << 8)
            return f"CS_INTERFACE Header: bcdMSC=0x{bcd_msc:04x}, wTotalLength={total}"
        if subtype == 0x02 and len(chunk) >= 6:  # MIDI IN Jack
            jtype = {1: "Embedded", 2: "External"}.get(chunk[3], f"0x{chunk[3]:02x}")
            jack_id = chunk[4]
            ij = chunk[5]
            return f"MIDI IN Jack: type={jtype}, id={jack_id}, iJack={ij}"
        if subtype == 0x03 and len(chunk) >= 7:  # MIDI OUT Jack
            jtype = {1: "Embedded", 2: "External"}.get(chunk[3], f"0x{chunk[3]:02x}")
            jack_id = chunk[4]
            pins = chunk[5]
            sources = []
            idx = 6
            for _ in range(pins):
                if idx + 1 >= len(chunk):
                    break
                sources.append((chunk[idx], chunk[idx + 1]))
                idx += 2
            ij = chunk[-1] if len(chunk) >= idx + 1 else 0
            sources_str = ", ".join([f"srcJack={s[0]} pin={s[1]}" for s in sources]) or "none"
            return (
                f"MIDI OUT Jack: type={jtype}, id={jack_id}, inputs={pins}, "
                f"sources=[{sources_str}], iJack={ij}"
            )
        if subtype == 0x04:
            return "Element descriptor"

    if dtype == 0x25 and subtype == 0x01:  # CS_ENDPOINT, MS General
        if len(chunk) >= 4:
            num = chunk[3]
            jacks = chunk[4 : 4 + num]
            jstr = ",".join(str(j) for j in jacks) if jacks else "none"
            return f"CS_ENDPOINT (MS General): AssocJacks={num} [{jstr}]"

    return None


def print_midi_descriptors(prefix, extra):
    printed = False
    for chunk in iter_extra(extra):
        desc = describe_midi_chunk(chunk)
        if desc:
            if not printed:
                print(f"{prefix}MIDI class-specific descriptors:")
                printed = True
            print(f"{prefix}  {desc}")


def print_device(dev):
    mfg = str_or_dash(dev, "iManufacturer")
    prod = str_or_dash(dev, "iProduct")
    serial = str_or_dash(dev, "iSerialNumber")
    vend = f"0x{dev.idVendor:04x}"
    prod_id = f"0x{dev.idProduct:04x}"
    bus = getattr(dev, "bus", "?")
    addr = getattr(dev, "address", "?")

    print(f"Device {vend}:{prod_id} (Bus {bus} Address {addr})")
    print(f"  Manufacturer: {mfg}")
    print(f"  Product     : {prod}")
    print(f"  Serial      : {serial}")

    for cfg in dev:
        print(
            f"  Config {cfg.bConfigurationValue}: MaxPower={cfg.bMaxPower * 2}mA, "
            f"Attributes=0x{cfg.bmAttributes:02x}"
        )
        for intf in cfg:
            print(
                f"    Interface {intf.bInterfaceNumber}, Alt {intf.bAlternateSetting}, "
                f"Class=0x{intf.bInterfaceClass:02x}, SubClass=0x{intf.bInterfaceSubClass:02x}, "
                f"Protocol=0x{intf.bInterfaceProtocol:02x}"
            )
            if intf.bInterfaceClass == 0x01 and intf.bInterfaceSubClass == 0x03:
                print_midi_descriptors("      ", getattr(intf, "extra_descriptors", None))
            for ep in intf:
                print(
                    f"      Endpoint 0x{ep.bEndpointAddress:02x}: Attr=0x{ep.bmAttributes:02x}, "
                    f"MaxPacket={ep.wMaxPacketSize}, Interval={ep.bInterval}"
                )
                if intf.bInterfaceClass == 0x01 and intf.bInterfaceSubClass == 0x03:
                    print_midi_descriptors("        ", getattr(ep, "extra_descriptors", None))



def main():
    parser = argparse.ArgumentParser(
        description="Find USB devices by substring and print descriptors (including MIDI)."
    )
    parser.add_argument(
        "needle", help="Case-insensitive substring in manufacturer/product/serial to match"
    )
    args = parser.parse_args()
    needle = args.needle.lower()

    devices = [d for d in usb.core.find(find_all=True) or [] if matches(d, needle)]
    if not devices:
        print("No matching devices found.")
        sys.exit(1)

    for dev in devices:
        print_device(dev)
        print("-" * 60)


if __name__ == "__main__":
    main()
