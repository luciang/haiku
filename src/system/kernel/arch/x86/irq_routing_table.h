#ifndef IRQ_ROUTING_TABLE_H
#define IRQ_ROUTING_TABLE_H


#include <ACPI.h>
#include <PCI.h>


#include "util/Vector.h"


struct irq_routing_entry
{
	int				device_address;
	int8			pin;

	acpi_handle		source;
	int				source_index;

	// pci busmanager connection
	uchar			pci_bus;
	uchar			pci_device;
};


typedef Vector<irq_routing_entry> IRQRoutingTable;


struct irq_descriptor
{
	irq_descriptor();
	// bit 0 is interrupt 0, bit 2 is interrupt 2, and so on
	int16			irq;
	bool			shareable;
	// B_LOW_ACTIVE_POLARITY or B_HIGH_ACTIVE_POLARITY
	int8			polarity;
	// B_LEVEL_TRIGGERED or B_EDGE_TRIGGERED
	int8			interrupt_mode;
};


void print_irq_descriptor(irq_descriptor* descriptor);
void print_irq_routing_table(IRQRoutingTable* table);


status_t read_irq_routing_table(pci_module_info *pci, acpi_module_info* acpi,
			IRQRoutingTable* table);
status_t read_irq_descriptor(acpi_module_info* acpi, acpi_handle device,
			const char* method, irq_descriptor* descriptor);

status_t read_current_irq(acpi_module_info* acpi, acpi_handle device,
			irq_descriptor* descriptor);
status_t read_possible_irq(acpi_module_info* acpi, acpi_handle device,
			irq_descriptor* descriptor);

status_t set_acpi_irq(acpi_module_info* acpi, acpi_handle device,
			irq_descriptor* descriptor);

#endif
