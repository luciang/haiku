#include <BeBuild.h>
#include <SupportDefs.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

// This one comes from Thomas Kurschel's Open IDE bus manager.
#include <ide_device_infoblock.h>

extern const char *__progname;

/* why isn't that documented ? */
#define IDE_GET_INFO 0x2710
#define IDE_GET_STATUS 0x2711

typedef struct {
	uint8 dummy;
	uint8 dma_status;
	uint8 pio_mode;
	uint8 dma_mode;
} _PACKED ide_status_t;


#define kNotSupported	"not supported"
#define kSupported		"supported"


int
main(int argc, char **argv)
{
	int fd;
	ide_device_infoblock ide_info;
	float capacity = 0;
	bool use_GB_units = true;
	bool removable;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <device>\n", __progname);
		return 1;
	}

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "could not open %s: %s\n", argv[1], strerror(errno));
		return 2;
	}

	if (ioctl(fd, IDE_GET_INFO, &ide_info, sizeof(ide_device_infoblock)) < 0) {
		fprintf(stderr, "could not get ide info for %s\n", argv[1]);
		return 3;
	}

	printf("Model Number:     %.40s\n", ide_info.model_number);
	printf("Serial Number:    %.20s\n", ide_info.serial_number);
	printf("Firmware Version: %.8s\n", ide_info.firmware_version);
	printf("\n");

 	if (ide_info._48_bit_addresses_supported) {
		capacity = (float) ide_info.LBA48_total_sectors *
					512 / (1024 * 1024 * 1024);
	} else if (ide_info.LBA_supported) {
		capacity = (float) ide_info.LBA_total_sectors *
					512 / (1024 * 1024 * 1024);
	} else {
		use_GB_units = false;

		capacity = (float) (ide_info.cylinders * ide_info.heads *
					ide_info.sectors) * 512 / (1024 * 1024);
	}

	printf("Capacity: %.2f %s\n", capacity, use_GB_units ? "GB" : "MB");
	printf("LBA supported: %s\n", ide_info.LBA_supported ? "yes" : "no");
	printf("\n");

	printf("DMA supported: %s\n", ide_info.DMA_supported ? "yes" : "no");

	if (ide_info.DMA_supported) {
		ide_status_t st = {0x01, 0x00, 0x00, 0x80}; /* God knows... */

		if (ioctl(fd, IDE_GET_STATUS, &st, sizeof(ide_status_t *)) < 0) {
			fprintf(stderr, "could not get ide status for %s\n", argv[1]);
			return 3;
		}

		if (st.dma_status > 6)
			printf("Bad dma_status field\n");
		else
			printf("DMA mode: 0x%02x\n", st.dma_mode);
	}

	printf("READ/WRITE DMA QUEUED: %s\n",
			ide_info.DMA_QUEUED_supported ? kSupported : kNotSupported);
	printf("\n");

	printf("Write Cache: %s\n",
			ide_info.write_cache_supported ? kSupported : kNotSupported);
	printf("Look Ahead: %s\n",
			ide_info.look_ahead_supported ? kSupported : kNotSupported);
	printf("Bus Release IRQ: %s\n",
			ide_info.RELEASE_irq_supported ? kSupported : kNotSupported);
	printf("Service start transfer IRQ: %s\n",
			ide_info.SERVICE_irq_supported ? kSupported : kNotSupported);
	printf("NOP: %s\n",
			ide_info.NOP_supported ? kSupported : kNotSupported);
	printf("FLUSH CACHE: %s\n",
			ide_info.FLUSH_CACHE_supported ? kSupported : kNotSupported);
	printf("FLUSH CACHE EXT: %s\n",
			ide_info.FLUSH_CACHE_EXT_supported ? kSupported : kNotSupported);
	printf("\n");

	printf("CFA features: %s\n",
			ide_info.CFA_supported ? kSupported : kNotSupported);

	removable = ide_info._0.ata.removable_media ||
				ide_info._0.atapi.removable_media;

	printf("Removable: %s\n", removable ? "yes" : "no");

	printf("Removable Media State Notification features: %s\n",
			(ide_info._127_RMSN_support == 1) ? kSupported : kNotSupported);
	printf("\n");

	// Not in original ideinfo:
	printf("Microcode download: %s\n",
			ide_info.DOWNLOAD_MICROCODE_supported ? kSupported : kNotSupported);
	printf("S.M.A.R.T: %s\n",
			ide_info.SMART_supported ? kSupported : kNotSupported);
	printf("Power Management: %s\n",
			ide_info.PM_supported ? kSupported : kNotSupported);
	printf("\n");

	// TODO: where do we find these fields?
/*
	printf("Statistics (for entire IDE bus)\n");
	printf("Overlappable commands: %d\n", ide_info.);
	printf("Started as overlapped: %d\n", ide_info.);
	printf("Started as non-overlapped: %d\n", ide_info.);
	printf("Average queue depth (of overlapped commands): %d\n", ide_info.);
	printf("Non-Overlappable commands: %d\n", ide_info.);
	printf("Bus released by device: %d\n", ide_info.);
*/

	close(fd);
	return 0;
}
