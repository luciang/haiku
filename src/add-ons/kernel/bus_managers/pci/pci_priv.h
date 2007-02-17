/*
 * Copyright 2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Copyright 2003-2006, Marcus Overhagen. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 */
#ifndef __PCI_PRIV_H__
#define __PCI_PRIV_H__


#include <KernelExport.h>
#include <device_manager.h>
#include <bus/PCI.h>


// name of PCI device modules
#define PCI_DEVICE_MODULE_NAME "bus_managers/pci/device_v1"

extern device_manager_info *gDeviceManager;


// PCI root.
// apart from being the common parent of all PCI devices, it
// manages access to PCI config space
typedef struct pci_root_info {
	bus_module_info info;

	// read PCI config space
	uint32 (*read_pci_config)(uint8 bus, uint8 device, uint8 function,
				uint8 offset, uint8 size);

	// write PCI config space
	void (*write_pci_config)(uint8 bus, uint8 device, uint8 function,
				uint8 offset, uint8 size, uint32 value);
} pci_root_info;

extern pci_device_module_info gPCIDeviceModule;


#ifdef __cplusplus
extern "C" {
#endif

void *		pci_ram_address(const void *physical_address_in_system_memory);

status_t 	pci_find_capability(uchar bus, uchar device, uchar function, uchar cap_id, uchar *offset);

status_t 	pci_io_init(void);
uint8		pci_read_io_8(int mapped_io_addr);
void		pci_write_io_8(int mapped_io_addr, uint8 value);
uint16		pci_read_io_16(int mapped_io_addr);
void		pci_write_io_16(int mapped_io_addr, uint16 value);
uint32		pci_read_io_32(int mapped_io_addr);
void		pci_write_io_32(int mapped_io_addr, uint32 value);

#ifdef __cplusplus
}
#endif

#endif	/* __PCI_PRIV_H__ */
