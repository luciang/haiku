/*
 *	Copyright 2010, Michael Lotz, mmlr@mlotz.ch. All Rights Reserved.
 *	Distributed under the terms of the MIT License.
 */

#include <PCI_x86.h>
#include "pci_msi.h"


static int32
pci_arch_module_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;
	}

	return B_BAD_VALUE;
}


pci_x86_module_info gPCIArchModule = {
	{
		B_PCI_X86_MODULE_NAME,
		0,
		pci_arch_module_std_ops
	},

	&pci_get_msi_count,
	&pci_configure_msi,
	&pci_unconfigure_msi,
	&pci_enable_msi,
	&pci_disable_msi
};
