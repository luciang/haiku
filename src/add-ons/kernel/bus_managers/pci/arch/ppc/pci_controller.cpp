/*
 * Copyright 2006, Ingo Weinhold. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "pci_controller.h"

#include <arch_platform.h>

#include "pci_private.h"

#include "openfirmware/pci_openfirmware.h"


status_t
pci_controller_init(void)
{
	switch (PPCPlatform::Default()->PlatformType()) {
		case PPC_PLATFORM_OPEN_FIRMWARE:
			return ppc_openfirmware_pci_controller_init();
			break;
	}
	return B_OK;
}


void *
pci_ram_address(const void *physical_address_in_system_memory)
{
	return (void *)physical_address_in_system_memory;
}
