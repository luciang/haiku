/*
 * BeOS Driver for Intel ICH AC'97 Link interface
 *
 * Copyright (c) 2002, 2003 Marcus Overhagen <marcus@overhagen.de>
 *
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR 
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS 
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <KernelExport.h>
#include <config_manager.h>
#include <PCI.h>
#include <OS.h>
#include <malloc.h>

#include "debug.h"
#include "config.h"
#include "ich.h"

device_config c;
device_config *config = &c;

status_t find_pci_pin_irq(uint8 pin, uint8 *irq);

/* 
 * search for the ICH AC97 controller, and initialize the global config 
 * XXX multiple controllers not supported
 */

status_t probe_device(void)
{
	pci_module_info *pcimodule;
	struct pci_info info; 
	struct pci_info *pciinfo = &info;
	int index;
	status_t result;
	uint32 value;

	if (get_module(B_PCI_MODULE_NAME,(module_info **)&pcimodule) < 0) {
		PRINT(("ERROR: couldn't load pci module\n"));
		return B_ERROR; 
	}

	config->name = NULL;
	config->nambar = 0;
	config->nabmbar = 0;
	config->mmbar = 0;
	config->mbbar = 0;
	config->irq = 0;
	config->sample_size = 2;
	config->swap_reg = false;
	config->type = 0;
	config->log_mmbar = 0;
	config->log_mbbar = 0;
	config->area_mmbar = -1;
	config->area_mbbar = -1;
	config->codecoffset = 0;

	for (index = 0; B_OK == pcimodule->get_nth_pci_info(index, pciinfo); index++) { 
		LOG(("Checking PCI device, vendor 0x%04x, id 0x%04x, bus 0x%02x, dev 0x%02x, func 0x%02x, rev 0x%02x, api 0x%02x, sub 0x%02x, base 0x%02x\n",
			pciinfo->vendor_id, pciinfo->device_id, pciinfo->bus, pciinfo->device, pciinfo->function,
			pciinfo->revision, pciinfo->class_api, pciinfo->class_sub, pciinfo->class_base));

		if (pciinfo->vendor_id == 0x8086 && pciinfo->device_id == 0x7195) {
			config->name = "Intel 82443MX"; 
		} else if (pciinfo->vendor_id == 0x8086 && pciinfo->device_id == 0x2415) { /* verified */
			config->name = "Intel 82801AA (ICH)";
		} else if (pciinfo->vendor_id == 0x8086 && pciinfo->device_id == 0x2425) { /* verified */
			config->name = "Intel 82801AB (ICH0)";
		} else if (pciinfo->vendor_id == 0x8086 && pciinfo->device_id == 0x2445) { /* verified */
			config->name = "Intel 82801BA (ICH2), Intel 82801BAM (ICH2-M)";
		} else if (pciinfo->vendor_id == 0x8086 && pciinfo->device_id == 0x2485) { /* verified */
			config->name = "Intel 82801CA (ICH3-S), Intel 82801CAM (ICH3-M)";
		} else if (pciinfo->vendor_id == 0x8086 && pciinfo->device_id == 0x24C5) { /* verified */
			config->name = "Intel 82801DB (ICH4)";
			config->type = TYPE_ICH4;
		} else if (pciinfo->vendor_id == 0x1039 && pciinfo->device_id == 0x7012) { /* verified */
			config->name = "SiS SI7012";
			config->swap_reg = true;
			config->sample_size = 1;
		} else if (pciinfo->vendor_id == 0x10DE && pciinfo->device_id == 0x01B1) {
			config->name = "NVIDIA nForce (MCP)";
		} else if (pciinfo->vendor_id == 0x10DE && pciinfo->device_id == 0x006A) {
			config->name = "NVIDIA nForce 2 (MCP2)";
		} else if (pciinfo->vendor_id == 0x10DE && pciinfo->device_id == 0x00DA) {
			config->name = "NVIDIA nForce 3 (MCP3)";
		} else if (pciinfo->vendor_id == 0x1022 && pciinfo->device_id == 0x764d) {
			config->name = "AMD AMD8111";
		} else if (pciinfo->vendor_id == 0x1022 && pciinfo->device_id == 0x7445) {
			config->name = "AMD AMD768";
		} else {
			continue;
		}
		break;
	}
	if (config->name == NULL) {
		LOG(("probe_device() No compatible hardware found\n"));
		put_module(B_PCI_MODULE_NAME);
		return B_ERROR;
	}

	LOG(("found %s\n",config->name));
	LOG(("revision = %d\n",pciinfo->revision));

	#if DEBUG
		LOG(("bus = %#x, device = %#x, function = %#x\n", pciinfo->bus, pciinfo->device, pciinfo->function));
		value = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x00, 2);
		LOG(("VID = %#04x\n",value));
		value = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x02, 2);
		LOG(("DID = %#04x\n",value));
		value = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x08, 1);
		LOG(("RID = %#02x\n",value));
		value = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x04, 2);
		LOG(("PCICMD = %#04x\n",value));
		value = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x06, 2);
		LOG(("PCISTS = %#04x\n",value));
		value = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x10, 4);
		LOG(("NAMBAR = %#08x\n",value));
		value = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x14, 4);
		LOG(("NABMBAR = %#08x\n",value));
		value = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x18, 4);
		LOG(("MMBAR = %#08x\n",value));
		value = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x1C, 4);
		LOG(("MBBAR = %#08x\n",value));
		value = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x3c, 1);
		LOG(("INTR_LN = %#02x\n",value));
		value = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x3d, 1);
		LOG(("INTR_PN = %#02x\n",value));
	#endif

	/*
	 * for ICH4 enable memory mapped IO and busmaster access,
	 * for old ICHs enable programmed IO and busmaster access
	 */
	value = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, PCI_PCICMD, 2);
	if (config->type & TYPE_ICH4)
		value |= PCI_PCICMD_MSE | PCI_PCICMD_BME;
	else
		value |= PCI_PCICMD_IOS | PCI_PCICMD_BME;
	pcimodule->write_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, PCI_PCICMD, 2, value);
		
	#if DEBUG
		value = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, PCI_PCICMD, 2);
		LOG(("PCICMD = %#04x\n",value));
	#endif
		
	config->irq = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x3C, 1);
	if (config->irq == 0 || config->irq == 0xff) {
		// workaround: even if no irq is configured, we may be able to find the correct one
		uint8 pin;
		uint8 irq;
		pin = pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x3d, 1);
		LOG(("IRQ not assigned to pin %d\n",pin));
		LOG(("Searching for IRQ...\n"));
		if (B_OK == find_pci_pin_irq(pin, &irq)) {
			LOG(("Assigning IRQ %d to pin %d\n",irq,pin));
			config->irq = irq;
		} else {
			config->irq = 0; // always 0, not 0xff if no irq assigned
		}
	}
		
	if (config->type & TYPE_ICH4) {
		// memory mapped access
		config->mmbar = 0xfffffffe & pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x18, 4);
		config->mbbar = 0xfffffffe & pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x1C, 4);
	} else {
		// pio access
		config->nambar = 0xfffffffe & pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x10, 4);
		config->nabmbar = 0xfffffffe & pcimodule->read_pci_config(pciinfo->bus, pciinfo->device, pciinfo->function, 0x14, 4);
	}

	LOG(("irq     = %d\n", config->irq));
	LOG(("nambar  = %#08x\n", config->nambar));
	LOG(("nabmbar = %#08x\n", config->nabmbar));
	LOG(("mmbar   = %#08x\n", config->mmbar));
	LOG(("mbbar   = %#08x\n", config->mbbar));

	result = B_OK;

	if (config->irq == 0) {
		PRINT(("WARNING: no interrupt configured\n"));
		/*
		 * we can continue without an interrupt, as another 
		 * workaround to handle this is also implemented
		 */
	}
	/* the ICH4 uses memory mapped IO */
	if ((config->type & TYPE_ICH4) != 0 && ((config->mmbar == 0) || (config->mbbar == 0))) {
		PRINT(("ERROR: memory mapped IO not configured\n"));
		result = B_ERROR;
	}
	/* all other ICHs use programmed IO */
	if ((config->type & TYPE_ICH4) == 0 && ((config->nambar == 0) || (config->nabmbar == 0))) {
		PRINT(("ERROR: IO space not configured\n"));
		result = B_ERROR;
	}

	put_module(B_PCI_MODULE_NAME);
	return result;
}


/*
 * This is another ugly workaround. If no irq has been assigned
 * to our card, we try to find another card that uses the same
 * interrupt pin, but has an irq assigned, and use it.
 */
status_t find_pci_pin_irq(uint8 pin, uint8 *irq)
{
	pci_module_info *module;
	struct pci_info info; 
	status_t result;
	long index;

	if (get_module(B_PCI_MODULE_NAME,(module_info **)&module) < 0) {
		PRINT(("ERROR: couldn't load pci module\n"));
		return B_ERROR; 
	}

	result = B_ERROR;
	for (index = 0; B_OK == module->get_nth_pci_info(index, &info); index++) {
		uint8 pciirq = module->read_pci_config(info.bus, info.device, info.function, PCI_interrupt_line, 1);
		uint8 pcipin = module->read_pci_config(info.bus, info.device, info.function, PCI_interrupt_pin, 1);
		LOG(("pin %d, irq %d\n",pcipin,pciirq));
		if (pcipin == pin && pciirq != 0 && pciirq != 0xff) {
			*irq = pciirq;
			result = B_OK;
			break;
		}
	}

	#if DEBUG 
		if (result != B_OK) {
			LOG(("Couldn't find IRQ for pin %d\n",pin));
		}
	#endif

	put_module(B_PCI_MODULE_NAME);
	return result;
}

