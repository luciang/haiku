# Clean PCI header script
#
# Copyright 2006, Haiku.
# Distributed under the terms of the MIT License.
#
# Authors:
#		John Drinkwater, john@nextraweb.com
#
# Use with http://pciids.sourceforge.net/pci.ids
# run as: awk -v HEADERFILE=pcihdr.h -f pci-header.awk pci.ids

BEGIN {
	FS = " "
	ofile = HEADERFILE
	print "#if 0" > ofile
	print "#\tPCIHDR.H: PCI Vendors, Devices, and Class Type information\n#" > ofile
	print "#\tGenerated by pci-clean, sourced from the web at the following URL:\n#\thttp://pciids.sourceforge.net/pci.ids\n#" > ofile
	print "#\tHeader created on " strftime( "%A, %d %b %Y %H:%M:%S %Z", systime() ) > ofile
	print "#endif" > ofile

	print "\ntypedef struct _PCI_VENTABLE\n{\n\tunsigned short\tVenId ;\n\tchar *\tVenFull ;\n\tchar *\tVenShort ;\n}  PCI_VENTABLE, *PPCI_VENTABLE ;\n" > ofile
	print "PCI_VENTABLE\tPciVenTable [] =\n{" > ofile

}

# matches vendor - starts with an id as first thing on the line
/^[[:xdigit:]][[:xdigit:]][[:xdigit:]][[:xdigit:]] / { 
	if ( vendor++ > 0 ) { a = ",\n" } else { a = "" }
	vend = substr($0, 7); gsub( /\"/, "\\\"", vend )
	printf a "\t{ 0x" $1 ", \"" vend "\" }" > ofile
	# store vendor ID for possible devices afterwards
	currentVendor = $1
}

# matches device 
/^\t[[:xdigit:]][[:xdigit:]][[:xdigit:]][[:xdigit:]] / { 

	device = substr($0, 8); gsub( /\"/, "\\\"", device )
	devicecount++
	devices[devicecount, 1] = currentVendor 
	devices[devicecount, 2] = $1
	devices[devicecount, 3] = device 
}

# matches subvendor device
/^\t\t[[:xdigit:]][[:xdigit:]][[:xdigit:]][[:xdigit:]] / { 

	device = substr($0, 14); gsub( /\"/, "\\\"", device )
	devicecount++
	devices[devicecount, 1] = $1 
	devices[devicecount, 2] = $2
	devices[devicecount, 3] = device 
}

# match device class - store data for later
/^C [[:xdigit:]][[:xdigit:]]  / { 
	class = $2
	classdes = substr($0, 7); gsub( /\"/, "\\\"", classdes )
}

# match subclass, use device class data captured earlier, and output
/^\t[[:xdigit:]][[:xdigit:]]  / {
	# if ( currentclass != class)  { classes = classes "\n"; currentclass = class }
	subclassdes = substr($0, 6);	gsub( /\"/, "\\\"", subclassdes )
	subclass = $1
	classcount++;
	classes[classcount, 1] = class
	classes[classcount, 2] = subclass
	classes[classcount, 3] = "00"
	classes[classcount, 4] = classdes
	classes[classcount, 5] = subclassdes
	classes[classcount, 6] = ""
} 

# match programming interface
/^\t\t[[:xdigit:]][[:xdigit:]]  / {
	#if ( $1 != "00" ) {
		#if ( length(classes) > 0 ) classes = classes ",\n"
		progif = substr($0, 7); gsub( /\"/, "\\\"", progif )		

		classcount++;
		classes[classcount, 1] = class
		classes[classcount, 2] = subclass
		classes[classcount, 3] = $1
		classes[classcount, 4] = classdes
		classes[classcount, 5] = subclassdes
		classes[classcount, 6] = progif
	#}
} 

# We've processed the file, now output.
END {

	print "\n};\n\n// Use this value for loop control during searching:\n#define\tPCI_VENTABLE_LEN\t(sizeof(PciVenTable)/sizeof(PCI_VENTABLE))\n" > ofile

	if ( devicecount > 0 ) {

		print "typedef struct _PCI_DEVTABLE\n{\n\tunsigned short	VenId ;\n\tunsigned short	DevId ;\n\tchar *\tChipDesc ;\n\tchar *\tChip ;\n}  PCI_DEVTABLE, *PPCI_DEVTABLE ;\n"  > ofile
		print "PCI_DEVTABLE\tPciDevTable [] =\n{" > ofile
		for (i = 1; i <= devicecount; i++) {
			if (i != 1) { a = ",\n" } else { a = "" }
			printf a "\t{ 0x" devices[i, 1] ", 0x" devices[i, 2] ", \"" devices[i, 3] "\" }" > ofile
		}
		print "\n} ;\n\n// Use this value for loop control during searching:\n#define	PCI_DEVTABLE_LEN	(sizeof(PciDevTable)/sizeof(PCI_DEVTABLE))\n" > ofile

	}
	
	if ( classcount > 0 ) {
		print "typedef struct _PCI_CLASSCODETABLE\n{\n\tunsigned char	BaseClass ;\n\tunsigned char	SubClass ;\n\tunsigned char	ProgIf ;\n\tchar *\t\tBaseDesc ;\n\tchar *\t\tSubDesc ;\n\tchar *\t\tProgDesc ;\n}  PCI_CLASSCODETABLE, *PPCI_CLASSCODETABLE ;\n" > ofile
		print "PCI_CLASSCODETABLE PciClassCodeTable [] =\n{" > ofile
		for (i = 1; i <= classcount; i++) {
			if (i != 1) { a = ",\n" } else { a = "" }
			if ( ( classes[i, 1] == classes[i+1, 1] ) && ( classes[i, 2] == classes[i+1, 2] ) && ( classes[i, 3] == classes[i+1, 3] ) ) {
			} else {
				printf a "\t{ 0x" classes[i, 1] ", 0x" classes[i, 2] ", 0x" classes[i, 3] ", \"" classes[i, 4] "\", \"" classes[i, 5]  "\", \"" classes[i, 6] "\" }" > ofile
			}
		}
		print "\n} ;\n\n// Use this value for loop control during searching:\n#define	PCI_CLASSCODETABLE_LEN	(sizeof(PciClassCodeTable)/sizeof(PCI_CLASSCODETABLE))\n" > ofile

	}

	print "char *\tPciCommandFlags [] =\n{\n\t\"I/O Access\",\n\t\"Memory Access\",\n\t\"Bus Mastering\",\n\t\"Special Cycles\",\n\t\"Memory Write & Invalidate\",\n\t\"Palette Snoop\",\n\t\"Parity Errors\",\n\t\"Wait Cycles\",\n\t\"System Errors\",\n\t\"Fast Back-To-Back\",\n\t\"Reserved 10\",\n\t\"Reserved 11\",\n\t\"Reserved 12\",\n\t\"Reserved 13\",\n\t\"Reserved 14\",\n\t\"Reserved 15\"\n} ;\n" > ofile
	print "// Use this value for loop control during searching:\n#define	PCI_COMMANDFLAGS_LEN	(sizeof(PciCommandFlags)/sizeof(char *))\n" > ofile
	print "char *\tPciStatusFlags [] =\n{\n\t\"Reserved 0\",\n\t\"Reserved 1\",\n\t\"Reserved 2\",\n\t\"Reserved 3\",\n\t\"Reserved 4\",\n\t\"66 MHz Capable\",\n\t\"User-Defined Features\",\n\t\"Fast Back-To-Back\",\n\t\"Data Parity Reported\",\n\t\"\",\n\t\"\",\n\t\"Signalled Target Abort\",\n\t\"Received Target Abort\",\n\t\"Received Master Abort\",\n\t\"Signalled System Error\",\n\t\"Detected Parity Error\"\n} ;\n" > ofile
	print "// Use this value for loop control during searching:\n#define	PCI_STATUSFLAGS_LEN	(sizeof(PciStatusFlags)/sizeof(char *))\n" > ofile
	print "char *\tPciDevSelFlags [] =\n{\n\t\"Fast Devsel Speed\",\n\t\"Medium Devsel Speed\",\n\t\"Slow Devsel Speed\",\n\t\"Reserved 9&10\"\n} ;\n" > ofile
	print "// Use this value for loop control during searching:\n#define	PCI_DEVSELFLAGS_LEN	(sizeof(PciDevSelFlags)/sizeof(char *))\n\n" > ofile

}


