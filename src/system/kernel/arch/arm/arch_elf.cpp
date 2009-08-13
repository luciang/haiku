/*
 * Copyright 2009, Johannes Wischert, johanneswi@gmail.com.
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Copyright 2005, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Copyright 2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

#ifdef _BOOT_MODE
#include <boot/arch.h>
#endif

#include <KernelExport.h>

#include <elf_priv.h>
#include <arch/elf.h>


#define TRACE_ARCH_ELF
#ifdef TRACE_ARCH_ELF
#	define TRACE(x) dprintf x
#	define CHATTY 1
#else
#	define TRACE(x) ;
#	define CHATTY 0
#endif


#ifdef TRACE_ARCH_ELF
static const char *kRelocations[] = {
"R_ARM_NONE",		//0	Static	Miscellaneous	
"R_ARM_PC24",		//1	Deprecated	ARM	((S + A) | T) ? P
"R_ARM_ABS32",		//2	Static	Data	(S + A) | T
"R_ARM_REL32",		//3	Static	Data	((S + A) | T) ? P
"R_ARM_LDR_PC_G0",	//4	Static	ARM	S + A ? P
"R_ARM_ABS16",		//5	Static	Data	S + A
"R_ARM_ABS12",		//6	Static	ARM	S + A
"R_ARM_THM_ABS5",	//7	Static	Thumb16	S + A
"R_ARM_ABS8",		//8	Static	Data	S + A
"R_ARM_SBREL32",	//9	Static	Data	((S + A) | T) ? B(S)
"R_ARM_THM_CALL",	//10	Static	Thumb32	((S + A) | T) ? P
"R_ARM_THM_PC8",	//11	Static	Thumb16	S + A ? Pa
"R_ARM_BREL_ADJ",	//12	Dynamic	Data	?B(S) + A
"R_ARM_TLS_DESC",	//13	Dynamic	Data	
"R_ARM_THM_SWI8",	//14	Obsolete		
"R_ARM_XPC25",		//15	Obsolete		
"R_ARM_THM_XPC22",	//16	Obsolete	Encodings reserved for future Dynamic relocations	
"R_ARM_TLS_DTPMOD32",	//17	Dynamic	Data	Module[S]
"R_ARM_TLS_DTPOFF32",	//18	Dynamic	Data	S + A ? TLS
"R_ARM_TLS_TPOFF32",	//19	Dynamic	Data	S + A ? tp
"R_ARM_COPY",		//20	Dynamic	Miscellaneous	
"R_ARM_GLOB_DAT",	//21	Dynamic	Data	(S + A) | T
"R_ARM_JUMP_SLOT",	//22	Dynamic	Data	(S + A) | T
"R_ARM_RELATIVE",	//23	Dynamic	Data	B(S) + A [Note: see Table 4-16]
"R_ARM_GOTOFF32",	//24	Static	Data	((S + A) | T) ? GOT_ORG
"R_ARM_BASE_PREL",	//25	Static	Data	B(S) + A ? P
"R_ARM_GOT_BREL",	//26	Static	Data	GOT(S) + A ? GOT_ORG
"R_ARM_PLT32",		//27	Deprecated	ARM	((S + A) | T) ? P
"R_ARM_CALL",		//28	Static	ARM	((S + A) | T) ? P
"R_ARM_JUMP24",		//29	Static	ARM	((S + A) | T) ? P
"R_ARM_THM_JUMP24",	//30	Static	Thumb32	((S + A) | T) ? P
"R_ARM_BASE_ABS",	//31	Static	Data	B(S) + A
"R_ARM_ALU_PCREL_7_0",	//32	Obsolete		
"R_ARM_ALU_PCREL_15_8",	//33	Obsolete		
"R_ARM_ALU_PCREL_23_15",	//34	Obsolete	Note ? Legacy (ARM ELF B02) names have been retained for these obsolete relocations.	
"R_ARM_LDR_SBREL_11_0_NC",	//35	Deprecated	ARM	S + A ? B(S)
"R_ARM_ALU_SBREL_19_12_NC",	//36	Deprecated	ARM	S + A ? B(S)
"R_ARM_ALU_SBREL_27_20_CK",	//37	Deprecated	ARM	S + A ? B(S)
"R_ARM_TARGET1",	//38	Static	Miscellaneous	(S + A) | T or ((S + A) | T) ? P
"R_ARM_SBREL31",	//39	Deprecated	Data	((S + A) | T) ? B(S)
"R_ARM_V4BX",		//40	Static	Miscellaneous	
"R_ARM_TARGET2",	//41	Static	Miscellaneous	
"R_ARM_PREL31",		//42	Static	Data	((S + A) | T) ? P
"R_ARM_MOVW_ABS_NC",	//43	Static	ARM	(S + A) | T
"R_ARM_MOVT_ABS",	//44	Static	ARM	S + A
"R_ARM_MOVW_PREL_NC",	//45	Static	ARM	((S + A) | T) ? P
"R_ARM_MOVT_PREL",	//46	Static	ARM	S + A ? P
"R_ARM_THM_MOVW_ABS_NC",	//47	Static	Thumb32	(S + A) | T
"R_ARM_THM_MOVT_ABS",	//48	Static	Thumb32	S + A
"R_ARM_THM_MOVW_PREL_NC",	//49	Static	Thumb32	((S + A) | T) ? P
"R_ARM_THM_MOVT_PREL",	//50	Static	Thumb32	S + A ? P
"R_ARM_THM_JUMP19",	//51	Static	Thumb32	((S + A) | T) ? P
"R_ARM_THM_JUMP6",	//52	Static	Thumb16	S + A ? P
"R_ARM_THM_ALU_PREL_11_0",	//53	Static	Thumb32	((S + A) | T) ? Pa
"R_ARM_THM_PC12",	//54	Static	Thumb32	S + A ? Pa
"R_ARM_ABS32_NOI",	//55	Static	Data	S + A
"R_ARM_REL32_NOI",	//56	Static	Data	S + A ? P
"R_ARM_ALU_PC_G0_NC",	//57	Static	ARM	((S + A) | T) ? P
"R_ARM_ALU_PC_G0",	//58	Static	ARM	((S + A) | T) ? P
"R_ARM_ALU_PC_G1_NC",	//59	Static	ARM	((S + A) | T) ? P
"R_ARM_ALU_PC_G1",	//60	Static	ARM	((S + A) | T) ? P
"R_ARM_ALU_PC_G2",	//61	Static	ARM	((S + A) | T) ? P
"R_ARM_LDR_PC_G1",	//62	Static	ARM	S + A ? P
"R_ARM_LDR_PC_G2",	//63	Static	ARM	S + A ? P
"R_ARM_LDRS_PC_G0",	//64	Static	ARM	S + A ? P
"R_ARM_LDRS_PC_G1",	//65	Static	ARM	S + A ? P
"R_ARM_LDRS_PC_G2",	//66	Static	ARM	S + A ? P
"R_ARM_LDC_PC_G0",	//67	Static	ARM	S + A ? P
"R_ARM_LDC_PC_G1",	//68	Static	ARM	S + A ? P
"R_ARM_LDC_PC_G2",	//69	Static	ARM	S + A ? P
"R_ARM_ALU_SB_G0_NC",	//70	Static	ARM	((S + A) | T) ? B(S)
"R_ARM_ALU_SB_G0",	//71	Static	ARM	((S + A) | T) ? B(S)
"R_ARM_ALU_SB_G1_NC",	//72	Static	ARM	((S + A) | T) ? B(S)
"R_ARM_ALU_SB_G1",	//73	Static	ARM	((S + A) | T) ? B(S)
"R_ARM_ALU_SB_G2",	//74	Static	ARM	((S + A) | T) ? B(S)
"R_ARM_LDR_SB_G0",	//75	Static	ARM	S + A ? B(S)
"R_ARM_LDR_SB_G1",	//76	Static	ARM	S + A ? B(S)
"R_ARM_LDR_SB_G2",	//77	Static	ARM	S + A ? B(S)
"R_ARM_LDRS_SB_G0",	//78	Static	ARM	S + A ? B(S)
"R_ARM_LDRS_SB_G1",	//79	Static	ARM	S + A ? B(S)
"R_ARM_LDRS_SB_G2",	//80	Static	ARM	S + A ? B(S)
"R_ARM_LDC_SB_G0",	//81	Static	ARM	S + A ? B(S)
"R_ARM_LDC_SB_G1",	//82	Static	ARM	S + A ? B(S)
"R_ARM_LDC_SB_G2",	//83	Static	ARM	S + A ? B(S)
"R_ARM_MOVW_BREL_NC",	//84	Static	ARM	((S + A) | T) ? B(S)
"R_ARM_MOVT_BREL",	//85	Static	ARM	S + A ? B(S)
"R_ARM_MOVW_BREL",	//86	Static	ARM	((S + A) | T) ? B(S)
"R_ARM_THM_MOVW_BREL_NC",	//87	Static	Thumb32	((S + A) | T) ? B(S)
"R_ARM_THM_MOVT_BREL",	//88	Static	Thumb32	S + A ? B(S)
"R_ARM_THM_MOVW_BREL",	//89	Static	Thumb32	((S + A) | T) ? B(S)
"R_ARM_TLS_GOTDESC",	//90	Static	Data	
"R_ARM_TLS_CALL",	//91	Static	ARM	
"R_ARM_TLS_DESCSEQ",	//92	Static	ARM	TLS relaxation
"R_ARM_THM_TLS_CALL",	//93	Static	Thumb32	
"R_ARM_PLT32_ABS",	//94	Static	Data	PLT(S) + A
"R_ARM_GOT_ABS",	//95	Static	Data	GOT(S) + A
"R_ARM_GOT_PREL",	//96	Static	Data	GOT(S) + A ? P
"R_ARM_GOT_BREL12",	//97	Static	ARM	GOT(S) + A ? GOT_ORG
"R_ARM_GOTOFF12",	//98	Static	ARM	S + A ? GOT_ORG
"R_ARM_GOTRELAX",	//99	Static	Miscellaneous	
"R_ARM_GNU_VTENTRY",	//100	Deprecated	Data	???
"R_ARM_GNU_VTINHERIT",	//101	Deprecated	Data	???
"R_ARM_THM_JUMP11",	//102	Static	Thumb16	S + A ? P
"R_ARM_THM_JUMP8",	//103	Static	Thumb16	S + A ? P
"R_ARM_TLS_GD32",	//104	Static	Data	GOT(S) + A ? P
"R_ARM_TLS_LDM32",	//105	Static	Data	GOT(S) + A ? P
"R_ARM_TLS_LDO32",	//106	Static	Data	S + A ? TLS
"R_ARM_TLS_IE32",	//107	Static	Data	GOT(S) + A ? P
"R_ARM_TLS_LE32",	//108	Static	Data	S + A ? tp
"R_ARM_TLS_LDO12",	//109	Static	ARM	S + A ? TLS
"R_ARM_TLS_LE12",	//110	Static	ARM	S + A ? tp
"R_ARM_TLS_IE12GP",	//111	Static	ARM	GOT(S) + A ? GOT_ORG
};
#endif

#ifdef _BOOT_MODE
status_t
boot_arch_elf_relocate_rel(struct preloaded_image *image,
	struct Elf32_Rel *rel, int rel_len)
#else
int
arch_elf_relocate_rel(struct elf_image_info *image,
	struct elf_image_info *resolve_image, struct Elf32_Rel *rel, int rel_len)
#endif
{
	// there are no rel entries in M68K elf
	return B_NO_ERROR;
}


static inline void
write_32(addr_t P, Elf32_Word value)
{
	*(Elf32_Word*)P = value;
}


static inline void
write_16(addr_t P, Elf32_Word value)
{
	// bits 16:29
	*(Elf32_Half*)P = (Elf32_Half)value;
}


static inline bool
write_16_check(addr_t P, Elf32_Word value)
{
	// bits 15:0
	if ((value & 0xffff0000) && (~value & 0xffff8000))
		return false;
	*(Elf32_Half*)P = (Elf32_Half)value;
	return true;
}


static inline bool
write_8(addr_t P, Elf32_Word value)
{
	// bits 7:0
	*(uint8 *)P = (uint8)value;
	return true;
}


static inline bool
write_8_check(addr_t P, Elf32_Word value)
{
	// bits 7:0
	if ((value & 0xffffff00) && (~value & 0xffffff80))
		return false;
	*(uint8 *)P = (uint8)value;
	return true;
}


#ifdef _BOOT_MODE
status_t
boot_arch_elf_relocate_rela(struct preloaded_image *image,
	struct Elf32_Rela *rel, int rel_len)
#else
int
arch_elf_relocate_rela(struct elf_image_info *image,
	struct elf_image_info *resolve_image, struct Elf32_Rela *rel, int rel_len)
#endif
{
        int i;
        struct Elf32_Sym *sym;
        int vlErr;
        addr_t S = 0;   // symbol address
        addr_t R = 0;   // section relative symbol address

        addr_t G = 0;   // GOT address
        addr_t L = 0;   // PLT address

        #define P       ((addr_t)(image->text_region.delta + rel[i].r_offset))
        #define A       ((addr_t)rel[i].r_addend)
        #define B       (image->text_region.delta)
#warning ARM:define T correctly for thumb!!!
	#define	T	0

        // TODO: Get the GOT address!
        #define REQUIRE_GOT     \
                if (G == 0) {   \
                        dprintf("arch_elf_relocate_rela(): Failed to get GOT address!\n"); \
                        return B_ERROR; \
                }

        // TODO: Get the PLT address!
        #define REQUIRE_PLT     \
                if (L == 0) {   \
                        dprintf("arch_elf_relocate_rela(): Failed to get PLT address!\n"); \
                        return B_ERROR; \
                }

        for (i = 0; i * (int)sizeof(struct Elf32_Rela) < rel_len; i++) {
#if CHATTY
                dprintf("looking at rel type %d, offset 0x%lx, sym 0x%lx, addend 0x%lx\n",
                        ELF32_R_TYPE(rel[i].r_info), rel[i].r_offset, ELF32_R_SYM(rel[i].r_info), rel[i].r_addend);
#endif
                switch (ELF32_R_TYPE(rel[i].r_info)) {
#warning ARM:ADDOTHERREL
                        case R_ARM_GLOB_DAT:
                                sym = SYMBOL(image, ELF32_R_SYM(rel[i].r_info));

#ifdef _BOOT_MODE
                                vlErr = boot_elf_resolve_symbol(image, sym, &S);
#else
                                vlErr = elf_resolve_symbol(image, sym, resolve_image, &S);
#endif
                                if (vlErr < 0) {
                                        dprintf("%s(): Failed to relocate "
                                                "entry index %d, rel type %d, offset 0x%lx, sym 0x%lx, "
                                                "addend 0x%lx\n", __FUNCTION__, i, ELF32_R_TYPE(rel[i].r_info),
                                                rel[i].r_offset, ELF32_R_SYM(rel[i].r_info),
                                                rel[i].r_addend);
                                        return vlErr;
                                }
                                break;
                }


#warning ARM:ADDOTHERREL


                switch (ELF32_R_TYPE(rel[i].r_info)) {
//			case R_ARM_BREL_ADJ:
//				?B(S) + A
/*			case R_ARM_TLS_DESC:

			case R_ARM_THM_XPC22 reserved for future Dynamic relocations:

			case R_ARM_TLS_DTPMOD32:
Module[S]
			case R_ARM_TLS_DTPOFF32:
S + A ? TLS
			case R_ARM_TLS_TPOFF32:
S + A ? tp
*/
			case R_ARM_GLOB_DAT:
				write_32(P,(S + A) | T);
				break;
/*			case R_ARM_JUMP_SLOT:
(S + A) | T
			case R_ARM_RELATIVE:
B(S) + A [Note: see Table 4-16]

*/







                        case R_ARM_NONE:
                                break;

                        case R_ARM_COPY:
                                // TODO: Implement!
                                dprintf("arch_elf_relocate_rela(): R_68K_COPY not yet "
                                        "supported!\n");
                                return B_ERROR;
/*
                        case R_68K_32:
                        case R_68K_GLOB_DAT:
                                write_32(P, S + A);
                                break;

                        case R_68K_16:
                                if (write_16_check(P, S + A))
                                        break;
dprintf("R_68K_16 overflow\n");
                                return B_BAD_DATA;

                        case R_68K_8:
                                if (write_8_check(P, S + A))
                                        break;
dprintf("R_68K_8 overflow\n");
                                return B_BAD_DATA;

                        case R_68K_PC32:
                                write_32(P, (S + A - P));
                                break;

                        case R_68K_PC16:
                                if (write_16_check(P, (S + A - P)))
                                        break;
dprintf("R_68K_PC16 overflow\n");
                                return B_BAD_DATA;

                        case R_68K_PC8:
                                if (write_8(P, (S + A - P)))
                                        break;
dprintf("R_68K_PC8 overflow\n");
                                return B_BAD_DATA;

                        case R_68K_GOT32:
                                REQUIRE_GOT;
                                write_32(P, (G + A - P));
                                break;
*/


		}
}
#warning ARM: FIXME!!!!!!!
	return B_NO_ERROR;
}

