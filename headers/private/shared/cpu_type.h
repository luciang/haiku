/*
 * Copyright 2004, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/* Taken from the Pulse application, and extended.
 * It's used by Pulse and sysinfo.
 */

#ifdef __cplusplus
extern "C" {
#endif

const char *get_cpu_vendor_string(enum cpu_types type);
const char *get_cpu_model_string(enum cpu_types type);
void get_cpu_type(char *vendorBuffer, size_t vendorSize, char *modelBuffer, size_t modelSize);

#ifdef __cplusplus
}
#endif


const char *
get_cpu_vendor_string(enum cpu_types type)
{
#if __POWERPC__
	// We're not that nice here
	return "IBM/Motorola";
#endif
#if __INTEL__
	// Determine x86 vendor name
	switch (type & B_CPU_x86_VENDOR_MASK) {
		case B_CPU_INTEL_x86:
			return "Intel";
		case B_CPU_AMD_x86:
			return "AMD";
		case B_CPU_CYRIX_x86:
			return "Cyrix";
		case B_CPU_IDT_x86:
			// IDT was bought by VIA
			if (((type >> 8) & 0xf) >= 6)
				return "VIA";
			return "IDT";
		case B_CPU_RISE_x86:
			return "Rise";
		case B_CPU_TRANSMETA_x86:
			return "Transmeta";

		default:
			return NULL;
	}
#endif
}


const char *
get_cpu_model_string(enum cpu_types type)
{
	// Determine CPU type
	switch (type) {
#if __POWERPC__
		case B_CPU_PPC_603:
			return "603";
		case B_CPU_PPC_603e:
			return "603e";
		case B_CPU_PPC_750:
			return "750";
		case B_CPU_PPC_604:
			return "604";
		case B_CPU_PPC_604e:
			return "604e";
#endif	// __POWERPC__
#if __INTEL__
		case B_CPU_x86:
			return "Unknown x86";

		/* Intel */
		case B_CPU_INTEL_PENTIUM:
		case B_CPU_INTEL_PENTIUM75:
			return "Pentium";
		case B_CPU_INTEL_PENTIUM_486_OVERDRIVE:
		case B_CPU_INTEL_PENTIUM75_486_OVERDRIVE:
			return "Pentium OD";
		case B_CPU_INTEL_PENTIUM_MMX:
		case B_CPU_INTEL_PENTIUM_MMX_MODEL_8:
			return "Pentium MMX";
		case B_CPU_INTEL_PENTIUM_PRO:
			return "Pentium Pro";
		case B_CPU_INTEL_PENTIUM_II_MODEL_3:
		case B_CPU_INTEL_PENTIUM_II_MODEL_5:
			return "Pentium II";
		case B_CPU_INTEL_CELERON:
			return "Celeron";
		case B_CPU_INTEL_PENTIUM_III:
		case B_CPU_INTEL_PENTIUM_III_MODEL_8:
		case B_CPU_INTEL_PENTIUM_III_MODEL_11:
		case B_CPU_INTEL_PENTIUM_III_XEON:
			return "Pentium III";
		case B_CPU_INTEL_PENTIUM_M:
		case B_CPU_INTEL_PENTIUM_M_MODEL_13:
			return "Pentium M";
		case B_CPU_INTEL_PENTIUM_IV:
		case B_CPU_INTEL_PENTIUM_IV_MODEL_1:
		case B_CPU_INTEL_PENTIUM_IV_MODEL_2:
		case B_CPU_INTEL_PENTIUM_IV_MODEL_3:		
		case B_CPU_INTEL_PENTIUM_IV_MODEL_4:
			return "Pentium 4";

		/* AMD */
		case B_CPU_AMD_K5_MODEL_0:
		case B_CPU_AMD_K5_MODEL_1:
		case B_CPU_AMD_K5_MODEL_2:
		case B_CPU_AMD_K5_MODEL_3:
			return "K5";
		case B_CPU_AMD_K6_MODEL_6:
		case B_CPU_AMD_K6_MODEL_7:
			return "K6";
		case B_CPU_AMD_K6_2:
			return "K6-2";
		case B_CPU_AMD_K6_III:
		case B_CPU_AMD_K6_III_MODEL_13:
			return "K6-III";
		case B_CPU_AMD_ATHLON_MODEL_1:
		case B_CPU_AMD_ATHLON_MODEL_2:
		case B_CPU_AMD_ATHLON_THUNDERBIRD:
			return "Athlon";
		case B_CPU_AMD_ATHLON_XP:
		case B_CPU_AMD_ATHLON_XP_MODEL_7:
		case B_CPU_AMD_ATHLON_XP_MODEL_8:
		case B_CPU_AMD_ATHLON_XP_MODEL_10:
			return "Athlon XP";
		case B_CPU_AMD_DURON:
			return "Duron";
		case B_CPU_AMD_ATHLON_64_MODEL_4:
		case B_CPU_AMD_ATHLON_64_MODEL_7:
		case B_CPU_AMD_ATHLON_64_MODEL_8:
		case B_CPU_AMD_ATHLON_64_MODEL_11:
		case B_CPU_AMD_ATHLON_64_MODEL_12:
		case B_CPU_AMD_ATHLON_64_MODEL_14:
		case B_CPU_AMD_ATHLON_64_MODEL_15:
			return "Athlon 64";
		case B_CPU_AMD_OPTERON:
			return "Opteron";

		/* Transmeta */
		case B_CPU_TRANSMETA_CRUSOE:
			return "Crusoe";

		/* IDT/VIA */
		case B_CPU_IDT_WINCHIP_C6:
			return "WinChip C6";
		case B_CPU_IDT_WINCHIP_2:
			return "WinChip 2";
		case B_CPU_VIA_EDEN:
		case B_CPU_VIA_EDEN_EZRA_T:
			return "Eden";

		/* Cyrix/VIA */
		case B_CPU_CYRIX_GXm:
			return "GXm";
		case B_CPU_CYRIX_6x86MX:
			return "6x86MX";

		/* Rise */
		case B_CPU_RISE_mP6:
			return "mP6";

		/* National Semiconductor */
		case B_CPU_NATIONAL_GEODE_GX1:
			return "Geode GX1";
#endif	// __INTEL__

		default:
			return NULL;
	}
}


void
get_cpu_type(char *vendorBuffer, size_t vendorSize, char *modelBuffer, size_t modelSize)
{
	const char *vendor, *model;
	system_info info;

	get_system_info(&info);

	vendor = get_cpu_vendor_string(info.cpu_type);
	if (vendor == NULL)
		vendor = "Unknown";

	model = get_cpu_model_string(info.cpu_type);
	if (model == NULL)
		model = "Unknown";

#ifdef R5_COMPATIBLE
	strncpy(vendorBuffer, vendor, vendorSize - 1);
	vendorBuffer[vendorSize - 1] = '\0';
	strncpy(modelBuffer, model, modelSize - 1);
	modelBuffer[modelSize - 1] = '\0';
#else
	strlcpy(vendorBuffer, vendor, vendorSize);
	strlcpy(modelBuffer, model, modelSize);
#endif
}
