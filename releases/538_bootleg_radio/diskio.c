// FatFs disk I/O glue for a single TinyUSB host mass storage device.
// FatFs (fatfs/ff.c) handles the FAT/exFAT filesystem itself, and calls the
// disk_*() functions below whenever it needs to read or write sectors.
// Physical drive 0 is USB device address 1, LUN 0 (no hub support).
// Transfers are blocking: tuh_task() is pumped until the SCSI command completes.

#include "tusb.h"
#include "ff.h"
#include "diskio.h"

static volatile bool diskBusy = false;
static volatile bool diskError = false;

static bool DiskIoComplete(uint8_t devAddr, tuh_msc_complete_data_t const *cbData)
{
	(void)devAddr;
	diskError = (cbData->csw->status != MSC_CSW_STATUS_PASSED);
	diskBusy = false;
	return true;
}

// Returns false if the device was unplugged while waiting
static bool WaitForDiskIo(uint8_t devAddr)
{
	while (diskBusy)
	{
		tuh_task();
		if (!tuh_msc_mounted(devAddr))
		{
			diskBusy = false;
			return false;
		}
	}
	return !diskError;
}

DSTATUS disk_status(BYTE pdrv)
{
	return (pdrv == 0 && tuh_msc_mounted(1)) ? 0 : STA_NODISK;
}

DSTATUS disk_initialize(BYTE pdrv)
{
	return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
	uint8_t const devAddr = pdrv + 1;
	if (!tuh_msc_mounted(devAddr))
	{
		return RES_NOTRDY;
	}

	// READ10 takes a 16-bit block count
	while (count > 0)
	{
		uint16_t n = (count > 0xFFFF) ? 0xFFFF : (uint16_t)count;
		diskBusy = true;
		if (!tuh_msc_read10(devAddr, 0, buff, (uint32_t)sector, n, DiskIoComplete, 0))
		{
			diskBusy = false;
			return RES_ERROR;
		}
		if (!WaitForDiskIo(devAddr))
		{
			return RES_ERROR;
		}
		buff += (uint32_t)n * tuh_msc_get_block_size(devAddr, 0);
		sector += n;
		count -= n;
	}
	return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
	uint8_t const devAddr = pdrv + 1;
	if (!tuh_msc_mounted(devAddr))
	{
		return RES_NOTRDY;
	}

	// WRITE10 takes a 16-bit block count
	while (count > 0)
	{
		uint16_t n = (count > 0xFFFF) ? 0xFFFF : (uint16_t)count;
		diskBusy = true;
		if (!tuh_msc_write10(devAddr, 0, buff, (uint32_t)sector, n, DiskIoComplete, 0))
		{
			diskBusy = false;
			return RES_ERROR;
		}
		if (!WaitForDiskIo(devAddr))
		{
			return RES_ERROR;
		}
		buff += (uint32_t)n * tuh_msc_get_block_size(devAddr, 0);
		sector += n;
		count -= n;
	}
	return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
	uint8_t const devAddr = pdrv + 1;
	switch (cmd)
	{
	case CTRL_SYNC:
		return RES_OK;
	case GET_SECTOR_COUNT:
		*((LBA_t *)buff) = tuh_msc_get_block_count(devAddr, 0);
		return RES_OK;
	case GET_SECTOR_SIZE:
		*((WORD *)buff) = (WORD)tuh_msc_get_block_size(devAddr, 0);
		return RES_OK;
	case GET_BLOCK_SIZE:
		*((DWORD *)buff) = 1;
		return RES_OK;
	default:
		return RES_PARERR;
	}
}
