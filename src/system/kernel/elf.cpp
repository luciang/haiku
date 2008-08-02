/*
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

/*!	Contains the ELF loader */

#include <elf.h>

#include <OS.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <debug.h>
#include <kernel.h>
#include <kimage.h>
#include <syscalls.h>
#include <team.h>
#include <thread.h>
#include <runtime_loader.h>
#include <util/khash.h>
#include <vfs.h>
#include <vm.h>
#include <vm_address_space.h>
#include <vm_types.h>

#include <arch/cpu.h>
#include <arch/elf.h>
#include <elf_priv.h>
#include <boot/elf.h>

//#define TRACE_ELF
#ifdef TRACE_ELF
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


#define IMAGE_HASH_SIZE 16

static hash_table *sImagesHash;

static struct elf_image_info *sKernelImage = NULL;
static mutex sImageMutex = MUTEX_INITIALIZER("kimages_lock");
	// guards sImagesHash
static mutex sImageLoadMutex = MUTEX_INITIALIZER("kimages_load_lock");
	// serializes loading/unloading add-ons locking order
	// sImageLoadMutex -> sImageMutex
static bool sInitialized = false;


/*! Calculates hash for an image using its ID */
static uint32
image_hash(void *_image, const void *_key, uint32 range)
{
	struct elf_image_info *image = (struct elf_image_info *)_image;
	image_id id = (image_id)_key;

	if (image != NULL)
		return image->id % range;

	return (uint32)id % range;
}


/*!	Compares an image to a given ID */
static int
image_compare(void *_image, const void *_key)
{
	struct elf_image_info *image = (struct elf_image_info *)_image;
	image_id id = (image_id)_key;

	return id - image->id;
}


static void
unregister_elf_image(struct elf_image_info *image)
{
	unregister_image(team_get_kernel_team(), image->id);
	hash_remove(sImagesHash, image);
}


static void
register_elf_image(struct elf_image_info *image)
{
	image_info imageInfo;

	memset(&imageInfo, 0, sizeof(image_info));
	imageInfo.id = image->id;
	imageInfo.type = B_SYSTEM_IMAGE;
	strlcpy(imageInfo.name, image->name, sizeof(imageInfo.name));

	imageInfo.text = (void *)image->text_region.start;
	imageInfo.text_size = image->text_region.size;
	imageInfo.data = (void *)image->data_region.start;
	imageInfo.data_size = image->data_region.size;

	image->id = register_image(team_get_kernel_team(), &imageInfo,
		sizeof(image_info));
	hash_insert(sImagesHash, image);
}


/*!	Note, you must lock the image mutex when you call this function. */
static struct elf_image_info *
find_image_at_address(addr_t address)
{
	struct hash_iterator iterator;
	struct elf_image_info *image;

#if KDEBUG
	if (!debug_debugger_running())
		ASSERT_LOCKED_MUTEX(&sImageMutex);
#endif

	hash_open(sImagesHash, &iterator);

	// get image that may contain the address

	while ((image = (elf_image_info *)hash_next(sImagesHash, &iterator))
			!= NULL) {
		if ((address >= image->text_region.start && address
				<= (image->text_region.start + image->text_region.size))
			|| (address >= image->data_region.start
				&& address
					<= (image->data_region.start + image->data_region.size)))
			break;
	}

	hash_close(sImagesHash, &iterator, false);
	return image;
}


static int
dump_address_info(int argc, char **argv)
{
	const char *symbol, *imageName;
	bool exactMatch;
	addr_t address, baseAddress;

	if (argc < 2) {
		kprintf("usage: ls <address>\n");
		return 0;
	}

	address = strtoul(argv[1], NULL, 16);

	if (elf_debug_lookup_symbol_address(address, &baseAddress, &symbol,
			&imageName, &exactMatch) == B_OK) {
		kprintf("%p = %s + 0x%lx (%s)%s\n", (void *)address, symbol,
			address - baseAddress, imageName, exactMatch ? "" : " (nearest)");
	} else
		kprintf("There is no image loaded at this address!\n");

	return 0;
}


static struct elf_image_info *
find_image(image_id id)
{
	return (elf_image_info *)hash_lookup(sImagesHash, (void *)id);
}


static struct elf_image_info *
find_image_by_vnode(void *vnode)
{
	struct hash_iterator iterator;
	struct elf_image_info *image;

	mutex_lock(&sImageMutex);
	hash_open(sImagesHash, &iterator);

	while ((image = (elf_image_info *)hash_next(sImagesHash, &iterator))
			!= NULL) {
		if (image->vnode == vnode)
			break;
	}

	hash_close(sImagesHash, &iterator, false);
	mutex_unlock(&sImageMutex);

	return image;
}


static struct elf_image_info *
create_image_struct()
{
	struct elf_image_info *image
		= (struct elf_image_info *)malloc(sizeof(struct elf_image_info));
	if (image == NULL)
		return NULL;

	memset(image, 0, sizeof(struct elf_image_info));

	image->text_region.id = -1;
	image->data_region.id = -1;
	image->ref_count = 1;

	return image;
}


static uint32
elf_hash(const char *name)
{
	uint32 hash = 0;
	uint32 temp;

	while (*name) {
		hash = (hash << 4) + (uint8)*name++;
		if ((temp = hash & 0xf0000000) != 0)
			hash ^= temp >> 24;
		hash &= ~temp;
	}
	return hash;
}


static const char *
get_symbol_type_string(struct Elf32_Sym *symbol)
{
	switch (ELF32_ST_TYPE(symbol->st_info)) {
		case STT_FUNC:
			return "func";
		case STT_OBJECT:
			return " obj";
		case STT_FILE:
			return "file";
		default:
			return "----";
	}
}


static const char *
get_symbol_bind_string(struct Elf32_Sym *symbol)
{
	switch (ELF32_ST_BIND(symbol->st_info)) {
		case STB_LOCAL:
			return "loc ";
		case STB_GLOBAL:
			return "glob";
		case STB_WEAK:
			return "weak";
		default:
			return "----";
	}
}


/*!	Searches a symbol (pattern) in all kernel images */
static int
dump_symbol(int argc, char **argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <symbol-name>\n", argv[0]);
		return 0;
	}

	struct elf_image_info *image = NULL;
	struct hash_iterator iterator;
	const char *pattern = argv[1];

	hash_open(sImagesHash, &iterator);
	while ((image = (elf_image_info *)hash_next(sImagesHash, &iterator))
			!= NULL) {
		if (image->num_debug_symbols > 0) {
			// search extended debug symbol table (contains static symbols)
			for (uint32 i = 0; i < image->num_debug_symbols; i++) {
				struct Elf32_Sym *symbol = &image->debug_symbols[i];
				const char *name = image->debug_string_table + symbol->st_name;

				if (symbol->st_value > 0 && strstr(name, pattern) != 0) {
					kprintf("%p %5lu %s:%s\n",
						(void *)(symbol->st_value + image->text_region.delta),
						symbol->st_size, image->name, name);
				}
			}
		} else {
			// search standard symbol lookup table
			for (uint32 i = 0; i < HASHTABSIZE(image); i++) {
				for (uint32 j = HASHBUCKETS(image)[i]; j != STN_UNDEF;
						j = HASHCHAINS(image)[j]) {
					struct Elf32_Sym *symbol = &image->syms[j];
					const char *name = SYMNAME(image, symbol);

					if (symbol->st_value > 0 && strstr(name, pattern) != 0) {
						kprintf("%p %5lu %s:%s\n", (void *)(symbol->st_value
								+ image->text_region.delta),
							symbol->st_size, image->name, name);
					}
				}
			}
		}
	}
	hash_close(sImagesHash, &iterator, false);
	return 0;
}


static int
dump_symbols(int argc, char **argv)
{
	struct elf_image_info *image = NULL;
	struct hash_iterator iterator;
	uint32 i;

	// if the argument looks like a hex number, treat it as such
	if (argc > 1) {
		if (isdigit(argv[1][0])) {
			uint32 num = strtoul(argv[1], NULL, 0);

			if (IS_KERNEL_ADDRESS(num)) {
				// find image at address

				hash_open(sImagesHash, &iterator);
				while ((image = (elf_image_info *)hash_next(sImagesHash,
						&iterator)) != NULL) {
					if (image->text_region.start <= num
						&& image->text_region.start + image->text_region.size
							>= num)
						break;
				}
				hash_close(sImagesHash, &iterator, false);

				if (image == NULL)
					kprintf("No image covers 0x%lx in the kernel!\n", num);
			} else {
				image = (elf_image_info *)hash_lookup(sImagesHash, (void *)num);
				if (image == NULL)
					kprintf("image 0x%lx doesn't exist in the kernel!\n", num);
			}
		} else {
			// look for image by name
			hash_open(sImagesHash, &iterator);
			while ((image = (elf_image_info *)hash_next(sImagesHash,
					&iterator)) != NULL) {
				if (!strcmp(image->name, argv[1]))
					break;
			}
			hash_close(sImagesHash, &iterator, false);

			if (image == NULL)
				kprintf("No image \"%s\" found in kernel!\n", argv[1]);
		}
	} else {
		kprintf("usage: %s image_name/image_id/address_in_image\n", argv[0]);
		return 0;
	}

	if (image == NULL)
		return -1;

	// dump symbols

	kprintf("Symbols of image %ld \"%s\":\nAddress  Type       Size Name\n",
		image->id, image->name);

	if (image->num_debug_symbols > 0) {
		// search extended debug symbol table (contains static symbols)
		for (i = 0; i < image->num_debug_symbols; i++) {
			struct Elf32_Sym *symbol = &image->debug_symbols[i];

			if (symbol->st_value == 0 || symbol->st_size
					>= image->text_region.size + image->data_region.size)
				continue;

			kprintf("%08lx %s/%s %5ld %s\n",
				symbol->st_value + image->text_region.delta,
				get_symbol_type_string(symbol), get_symbol_bind_string(symbol),
				symbol->st_size, image->debug_string_table + symbol->st_name);
		}
	} else {
		int32 j;

		// search standard symbol lookup table
		for (i = 0; i < HASHTABSIZE(image); i++) {
			for (j = HASHBUCKETS(image)[i]; j != STN_UNDEF;
					j = HASHCHAINS(image)[j]) {
				struct Elf32_Sym *symbol = &image->syms[j];

				if (symbol->st_value == 0 || symbol->st_size
						>= image->text_region.size + image->data_region.size)
					continue;

				kprintf("%08lx %s/%s %5ld %s\n",
					symbol->st_value + image->text_region.delta,
					get_symbol_type_string(symbol),
					get_symbol_bind_string(symbol),
					symbol->st_size, SYMNAME(image, symbol));
			}
		}
	}

	return 0;
}


static void
dump_elf_region(struct elf_region *region, const char *name)
{
	kprintf("   %s.id %ld\n", name, region->id);
	kprintf("   %s.start 0x%lx\n", name, region->start);
	kprintf("   %s.size 0x%lx\n", name, region->size);
	kprintf("   %s.delta %ld\n", name, region->delta);
}


static void
dump_image_info(struct elf_image_info *image)
{
	kprintf("elf_image_info at %p:\n", image);
	kprintf(" next %p\n", image->next);
	kprintf(" id %ld\n", image->id);
	dump_elf_region(&image->text_region, "text");
	dump_elf_region(&image->data_region, "data");
	kprintf(" dynamic_section 0x%lx\n", image->dynamic_section);
	kprintf(" needed %p\n", image->needed);
	kprintf(" symhash %p\n", image->symhash);
	kprintf(" syms %p\n", image->syms);
	kprintf(" strtab %p\n", image->strtab);
	kprintf(" rel %p\n", image->rel);
	kprintf(" rel_len 0x%x\n", image->rel_len);
	kprintf(" rela %p\n", image->rela);
	kprintf(" rela_len 0x%x\n", image->rela_len);
	kprintf(" pltrel %p\n", image->pltrel);
	kprintf(" pltrel_len 0x%x\n", image->pltrel_len);

	kprintf(" debug_symbols %p (%ld)\n",
		image->debug_symbols, image->num_debug_symbols);
}


static int
dump_image(int argc, char **argv)
{
	struct hash_iterator iterator;
	struct elf_image_info *image;

	// if the argument looks like a hex number, treat it as such
	if (argc > 1) {
		uint32 num = strtoul(argv[1], NULL, 0);

		if (IS_KERNEL_ADDRESS(num)) {
			// semi-hack
			dump_image_info((struct elf_image_info *)num);
		} else {
			image = (elf_image_info *)hash_lookup(sImagesHash, (void *)num);
			if (image == NULL)
				kprintf("image 0x%lx doesn't exist in the kernel!\n", num);
			else
				dump_image_info(image);
		}
		return 0;
	}

	kprintf("loaded kernel images:\n");

	hash_open(sImagesHash, &iterator);

	while ((image = (elf_image_info *)hash_next(sImagesHash, &iterator))
			!= NULL) {
		kprintf("%p (%ld) %s\n", image, image->id, image->name);
	}

	hash_close(sImagesHash, &iterator, false);
	return 0;
}


// Currently unused
#if 0
static
void dump_symbol(struct elf_image_info *image, struct Elf32_Sym *sym)
{

	kprintf("symbol at %p, in image %p\n", sym, image);

	kprintf(" name index %d, '%s'\n", sym->st_name, SYMNAME(image, sym));
	kprintf(" st_value 0x%x\n", sym->st_value);
	kprintf(" st_size %d\n", sym->st_size);
	kprintf(" st_info 0x%x\n", sym->st_info);
	kprintf(" st_other 0x%x\n", sym->st_other);
	kprintf(" st_shndx %d\n", sym->st_shndx);
}
#endif


static struct Elf32_Sym *
elf_find_symbol(struct elf_image_info *image, const char *name)
{
	uint32 hash;
	uint32 i;

	if (!image->dynamic_section)
		return NULL;

	hash = elf_hash(name) % HASHTABSIZE(image);
	for (i = HASHBUCKETS(image)[hash]; i != STN_UNDEF;
			i = HASHCHAINS(image)[i]) {
		if (!strcmp(SYMNAME(image, &image->syms[i]), name))
			return &image->syms[i];
	}

	return NULL;
}


static status_t
elf_parse_dynamic_section(struct elf_image_info *image)
{
	struct Elf32_Dyn *d;
	int32 neededOffset = -1;

	TRACE(("top of elf_parse_dynamic_section\n"));

	image->symhash = 0;
	image->syms = 0;
	image->strtab = 0;

	d = (struct Elf32_Dyn *)image->dynamic_section;
	if (!d)
		return B_ERROR;

	for (int32 i = 0; d[i].d_tag != DT_NULL; i++) {
		switch (d[i].d_tag) {
			case DT_NEEDED:
				neededOffset = d[i].d_un.d_ptr + image->text_region.delta;
				break;
			case DT_HASH:
				image->symhash = (uint32 *)(d[i].d_un.d_ptr
					+ image->text_region.delta);
				break;
			case DT_STRTAB:
				image->strtab = (char *)(d[i].d_un.d_ptr
					+ image->text_region.delta);
				break;
			case DT_SYMTAB:
				image->syms = (struct Elf32_Sym *)(d[i].d_un.d_ptr
					+ image->text_region.delta);
				break;
			case DT_REL:
				image->rel = (struct Elf32_Rel *)(d[i].d_un.d_ptr
					+ image->text_region.delta);
				break;
			case DT_RELSZ:
				image->rel_len = d[i].d_un.d_val;
				break;
			case DT_RELA:
				image->rela = (struct Elf32_Rela *)(d[i].d_un.d_ptr
					+ image->text_region.delta);
				break;
			case DT_RELASZ:
				image->rela_len = d[i].d_un.d_val;
				break;
			case DT_JMPREL:
				image->pltrel = (struct Elf32_Rel *)(d[i].d_un.d_ptr
					+ image->text_region.delta);
				break;
			case DT_PLTRELSZ:
				image->pltrel_len = d[i].d_un.d_val;
				break;
			case DT_PLTREL:
				image->pltrel_type = d[i].d_un.d_val;
				break;

			default:
				continue;
		}
	}

	// lets make sure we found all the required sections
	if (!image->symhash || !image->syms || !image->strtab)
		return B_ERROR;

	TRACE(("needed_offset = %ld\n", neededOffset));

	if (neededOffset >= 0)
		image->needed = STRING(image, neededOffset);

	return B_OK;
}


/*!	Resolves the \a symbol by linking against \a sharedImage if necessary.
	Returns the resolved symbol's address in \a _symbolAddress.
	TODO: eventually get rid of "symbolPrepend"
*/
status_t
elf_resolve_symbol(struct elf_image_info *image, struct Elf32_Sym *symbol,
	struct elf_image_info *sharedImage, const char *symbolPrepend,
	addr_t *_symbolAddress)
{
	switch (symbol->st_shndx) {
		case SHN_UNDEF:
		{
			struct Elf32_Sym *newSymbol;
			char newNameBuffer[368];
			char *newName;

			// patch the symbol name
			if (symbolPrepend) {
				newName = newNameBuffer;
				strlcpy(newName, symbolPrepend, sizeof(newName));
				strlcat(newName, SYMNAME(image, symbol), sizeof(newName));
			} else
				newName = SYMNAME(image, symbol);

			// it's undefined, must be outside this image, try the other image
			newSymbol = elf_find_symbol(sharedImage, newName);
			if (newSymbol == NULL) {
				dprintf("\"%s\": could not resolve symbol '%s'\n",
					image->name, newName);
				return B_MISSING_SYMBOL;
			}

			// make sure they're the same type
			if (ELF32_ST_TYPE(symbol->st_info)
					!= ELF32_ST_TYPE(newSymbol->st_info)) {
				dprintf("elf_resolve_symbol: found symbol '%s' in shared image "
					"but wrong type\n", newName);
				return B_MISSING_SYMBOL;
			}

			if (ELF32_ST_BIND(newSymbol->st_info) != STB_GLOBAL
				&& ELF32_ST_BIND(newSymbol->st_info) != STB_WEAK) {
				TRACE(("elf_resolve_symbol: found symbol '%s' but not "
					"exported\n", newName));
				return B_MISSING_SYMBOL;
			}

			*_symbolAddress = newSymbol->st_value
				+ sharedImage->text_region.delta;
			return B_OK;
		}
		case SHN_ABS:
			*_symbolAddress = symbol->st_value;
			return B_OK;
		case SHN_COMMON:
			// ToDo: finish this
			TRACE(("elf_resolve_symbol: COMMON symbol, finish me!\n"));
			return B_ERROR;

		default:
			// standard symbol
			*_symbolAddress = symbol->st_value + image->text_region.delta;
			return B_OK;
	}
}


/*! Until we have shared library support, just this links against the kernel */
static int
elf_relocate(struct elf_image_info *image, const char *symbolPrepend)
{
	int status = B_NO_ERROR;

	TRACE(("top of elf_relocate\n"));

	// deal with the rels first
	if (image->rel) {
		TRACE(("total %i relocs\n",
			image->rel_len / (int)sizeof(struct Elf32_Rel)));

		status = arch_elf_relocate_rel(image, symbolPrepend, sKernelImage,
			image->rel, image->rel_len);
		if (status < B_OK)
			return status;
	}

	if (image->pltrel) {
		TRACE(("total %i plt-relocs\n",
			image->pltrel_len / (int)sizeof(struct Elf32_Rel)));

		if (image->pltrel_type == DT_REL) {
			status = arch_elf_relocate_rel(image, symbolPrepend, sKernelImage,
				image->pltrel, image->pltrel_len);
		} else {
			status = arch_elf_relocate_rela(image, symbolPrepend, sKernelImage,
				(struct Elf32_Rela *)image->pltrel, image->pltrel_len);
		}
		if (status < B_OK)
			return status;
	}

	if (image->rela) {
		status = arch_elf_relocate_rela(image, symbolPrepend, sKernelImage,
			image->rela, image->rela_len);
		if (status < B_OK)
			return status;
	}

	return status;
}


static int
verify_eheader(struct Elf32_Ehdr *elfHeader)
{
	if (memcmp(elfHeader->e_ident, ELF_MAGIC, 4) != 0)
		return B_NOT_AN_EXECUTABLE;

	if (elfHeader->e_ident[4] != ELFCLASS32)
		return B_NOT_AN_EXECUTABLE;

	if (elfHeader->e_phoff == 0)
		return B_NOT_AN_EXECUTABLE;

	if (elfHeader->e_phentsize < sizeof(struct Elf32_Phdr))
		return B_NOT_AN_EXECUTABLE;

	return 0;
}


static status_t
unload_elf_image(struct elf_image_info *image)
{
	if (atomic_add(&image->ref_count, -1) > 1)
		return B_OK;

	TRACE(("unload image %ld, %s\n", image->id, image->name));

	delete_area(image->text_region.id);
	delete_area(image->data_region.id);

	if (image->vnode)
		vfs_put_vnode(image->vnode);

	unregister_elf_image(image);

	free(image->debug_symbols);
	free((void*)image->debug_string_table);
	free(image->elf_header);
	free(image->name);
	free(image);

	return B_NO_ERROR;
}


static status_t
load_elf_symbol_table(int fd, struct elf_image_info *image)
{
	struct Elf32_Ehdr *elfHeader = image->elf_header;
	struct Elf32_Sym *symbolTable = NULL;
	struct Elf32_Shdr *stringHeader = NULL;
	uint32 numSymbols = 0;
	char *stringTable;
	status_t status;
	ssize_t length;
	int32 i;

	// get section headers

	ssize_t size = elfHeader->e_shnum * elfHeader->e_shentsize;
	struct Elf32_Shdr *sectionHeaders = (struct Elf32_Shdr *)malloc(size);
	if (sectionHeaders == NULL) {
		dprintf("error allocating space for section headers\n");
		return B_NO_MEMORY;
	}

	length = read_pos(fd, elfHeader->e_shoff, sectionHeaders, size);
	if (length < size) {
		TRACE(("error reading in program headers\n"));
		status = B_ERROR;
		goto error1;
	}

	// find symbol table in section headers

	for (i = 0; i < elfHeader->e_shnum; i++) {
		if (sectionHeaders[i].sh_type == SHT_SYMTAB) {
			stringHeader = &sectionHeaders[sectionHeaders[i].sh_link];

			if (stringHeader->sh_type != SHT_STRTAB) {
				TRACE(("doesn't link to string table\n"));
				status = B_BAD_DATA;
				goto error1;
			}

			// read in symbol table
			symbolTable
				= (struct Elf32_Sym *)malloc(size = sectionHeaders[i].sh_size);
			if (symbolTable == NULL) {
				status = B_NO_MEMORY;
				goto error1;
			}

			length
				= read_pos(fd, sectionHeaders[i].sh_offset, symbolTable, size);
			if (length < size) {
				TRACE(("error reading in symbol table\n"));
				status = B_ERROR;
				goto error1;
			}

			numSymbols = size / sizeof(struct Elf32_Sym);
			break;
		}
	}

	if (symbolTable == NULL) {
		TRACE(("no symbol table\n"));
		status = B_BAD_VALUE;
		goto error1;
	}

	// read in string table

	stringTable = (char *)malloc(size = stringHeader->sh_size);
	if (stringTable == NULL) {
		status = B_NO_MEMORY;
		goto error2;
	}

	length = read_pos(fd, stringHeader->sh_offset, stringTable, size);
	if (length < size) {
		TRACE(("error reading in string table\n"));
		status = B_ERROR;
		goto error3;
	}

	TRACE(("loaded debug %ld symbols\n", numSymbols));

	// insert tables into image
	image->debug_symbols = symbolTable;
	image->num_debug_symbols = numSymbols;
	image->debug_string_table = stringTable;

	free(sectionHeaders);
	return B_OK;

error3:
	free(stringTable);
error2:
	free(symbolTable);
error1:
	free(sectionHeaders);

	return status;
}


static status_t
insert_preloaded_image(struct preloaded_image *preloadedImage, bool kernel)
{
	struct elf_image_info *image;
	status_t status;

	status = verify_eheader(&preloadedImage->elf_header);
	if (status < B_OK)
		return status;

	image = create_image_struct();
	if (image == NULL)
		return B_NO_MEMORY;

	image->name = strdup(preloadedImage->name);
	image->dynamic_section = preloadedImage->dynamic_section.start;

	image->text_region = preloadedImage->text_region;
	image->data_region = preloadedImage->data_region;

	status = elf_parse_dynamic_section(image);
	if (status < B_OK)
		goto error1;

	if (!kernel) {
		status = elf_relocate(image, NULL);
		if (status < B_OK)
			goto error1;
	} else
		sKernelImage = image;

	// copy debug symbols to the kernel heap
	if (preloadedImage->debug_symbols != NULL) {
		int32 debugSymbolsSize = sizeof(Elf32_Sym)
			* preloadedImage->num_debug_symbols;
		image->debug_symbols = (Elf32_Sym*)malloc(debugSymbolsSize);
		if (image->debug_symbols != NULL) {
			memcpy(image->debug_symbols, preloadedImage->debug_symbols,
				debugSymbolsSize);
		}
	}
	image->num_debug_symbols = preloadedImage->num_debug_symbols;

	// copy debug string table to the kernel heap
	if (preloadedImage->debug_string_table != NULL) {
		image->debug_string_table = (char*)malloc(
			preloadedImage->debug_string_table_size);
		if (image->debug_string_table != NULL) {
			memcpy((void*)image->debug_string_table,
				preloadedImage->debug_string_table,
				preloadedImage->debug_string_table_size);
		}
	}

	register_elf_image(image);
	preloadedImage->id = image->id;
		// modules_init() uses this information to get the preloaded images

	// we now no longer need to write to the text area anymore
	set_area_protection(image->text_region.id,
		B_KERNEL_READ_AREA | B_KERNEL_EXECUTE_AREA);

	return B_OK;

error1:
	free(image);

	// clean up preloaded image resources (this image won't be used anymore)
	delete_area(preloadedImage->text_region.id);
	delete_area(preloadedImage->data_region.id);
	preloadedImage->id = -1;

	return status;
}


//	#pragma mark - userland symbol lookup


class UserSymbolLookup {
public:
	static UserSymbolLookup& Default()
	{
		return sLookup;
	}

	status_t Init(struct team* team)
	{
		// find the runtime loader debug area
		vm_area* area = team->address_space->areas;
		while (area != NULL) {
			if (strcmp(area->name, RUNTIME_LOADER_DEBUG_AREA_NAME) == 0)
				break;
			area = area->address_space_next;
		}

		if (area == NULL)
			return B_ERROR;

		// copy the runtime loader data structure
		if (!_Read((runtime_loader_debug_area*)area->base, fDebugArea))
			return B_BAD_ADDRESS;

		return B_OK;
	}

	status_t LookupSymbolAddress(addr_t address, addr_t *_baseAddress,
		const char **_symbolName, const char **_imageName, bool *_exactMatch)
	{
		// Note, that this function doesn't find all symbols that we would like
		// to find. E.g. static functions do not appear in the symbol table
		// as function symbols, but as sections without name and size. The
		// .symtab section together with the .strtab section, which apparently
		// differ from the tables referred to by the .dynamic section, also
		// contain proper names and sizes for those symbols. Therefore, to get
		// completely satisfying results, we would need to read those tables
		// from the shared object.

		// get the image for the address
		image_t image;
		status_t error = _FindImageAtAddress(address, image);
		if (error != B_OK)
			return error;

		strlcpy(fImageName, image.name, sizeof(fImageName));

		// symbol hash table size
		uint32 hashTabSize;
		if (!_Read(image.symhash, hashTabSize))
			return B_BAD_ADDRESS;

		// remote pointers to hash buckets and chains
		const uint32* hashBuckets = image.symhash + 2;
		const uint32* hashChains = image.symhash + 2 + hashTabSize;

		const elf_region_t& textRegion = image.regions[0];

		// search the image for the symbol
		Elf32_Sym symbolFound;
		addr_t deltaFound = INT_MAX;
		bool exactMatch = false;

		for (uint32 i = 0; i < hashTabSize; i++) {
			uint32 bucket;
			if (!_Read(&hashBuckets[i], bucket))
				return B_BAD_ADDRESS;

			for (uint32 j = bucket; j != STN_UNDEF;
					_Read(&hashChains[j], j) ? 0 : j = STN_UNDEF) {

				Elf32_Sym symbol;
				if (!_Read(image.syms + j, symbol))
					continue;

				// The symbol table contains not only symbols referring to
				// functions and data symbols within the shared object, but also
				// referenced symbols of other shared objects, as well as
				// section and file references. We ignore everything but
				// function and data symbols that have an st_value != 0 (0
				// seems to be an indication for a symbol defined elsewhere
				// -- couldn't verify that in the specs though).
				if ((ELF32_ST_TYPE(symbol.st_info) != STT_FUNC
						&& ELF32_ST_TYPE(symbol.st_info) != STT_OBJECT)
					|| symbol.st_value == 0
					|| symbol.st_value + symbol.st_size + textRegion.delta
						> textRegion.vmstart + textRegion.size) {
					continue;
				}

				// skip symbols starting after the given address
				addr_t symbolAddress = symbol.st_value + textRegion.delta;
				if (symbolAddress > address)
					continue;
				addr_t symbolDelta = address - symbolAddress;

				if (symbolDelta < deltaFound) {
					deltaFound = symbolDelta;
					symbolFound = symbol;

					if (symbolDelta >= 0 && symbolDelta < symbol.st_size) {
						// exact match
						exactMatch = true;
						break;
					}
				}
			}
		}

		if (_imageName)
			*_imageName = fImageName;

		if (_symbolName) {
			*_symbolName = NULL;

			if (deltaFound < INT_MAX) {
				if (_ReadString(image, symbolFound.st_name, fSymbolName,
						sizeof(fSymbolName))) {
					*_symbolName = fSymbolName;
				} else {
					// we can't get its name, so forget the symbol
					deltaFound = INT_MAX;
				}
			}
		}

		if (_baseAddress) {
			if (deltaFound < INT_MAX)
				*_baseAddress = symbolFound.st_value + textRegion.delta;
			else
				*_baseAddress = textRegion.vmstart;
		}

		if (_exactMatch)
			*_exactMatch = exactMatch;

		return B_OK;
	}

	status_t _FindImageAtAddress(addr_t address, image_t& image)
	{
		image_queue_t imageQueue;
		if (!_Read(fDebugArea.loaded_images, imageQueue))
			return B_BAD_ADDRESS;

		image_t* imageAddress = imageQueue.head;
		while (imageAddress != NULL) {
			if (!_Read(imageAddress, image))
				return B_BAD_ADDRESS;

			if (image.regions[0].vmstart <= address
				&& address < image.regions[0].vmstart + image.regions[0].size) {
				return B_OK;
			}

			imageAddress = image.next;
		}

		return B_ENTRY_NOT_FOUND;
	}

	bool _ReadString(const image_t& image, uint32 offset, char* buffer,
		size_t bufferSize)
	{
		const char* address = image.strtab + offset;

		if (!IS_USER_ADDRESS(address))
			return false;

		return user_strlcpy(buffer, address, bufferSize) >= 0;
	}

	template<typename T> bool _Read(const T* address, T& data);
		// gcc 2.95.3 doesn't like it defined in-place

private:
	runtime_loader_debug_area	fDebugArea;
	char						fImageName[B_OS_NAME_LENGTH];
	char						fSymbolName[256];
	static UserSymbolLookup		sLookup;
};


template<typename T>
bool
UserSymbolLookup::_Read(const T* address, T& data)
{
	if (!IS_USER_ADDRESS(address))
		return false;

	return user_memcpy(&data, address, sizeof(T)) == B_OK;
}


UserSymbolLookup UserSymbolLookup::sLookup;
	// doesn't need construction, but has an Init() method


//	#pragma mark - public kernel API


status_t
get_image_symbol(image_id id, const char *name, int32 symbolClass,
	void **_symbol)
{
	struct elf_image_info *image;
	struct Elf32_Sym *symbol;
	status_t status = B_OK;

	TRACE(("get_image_symbol(%s)\n", name));

	mutex_lock(&sImageMutex);

	image = find_image(id);
	if (image == NULL) {
		status = B_BAD_IMAGE_ID;
		goto done;
	}

	symbol = elf_find_symbol(image, name);
	if (symbol == NULL || symbol->st_shndx == SHN_UNDEF) {
		status = B_ENTRY_NOT_FOUND;
		goto done;
	}

	// TODO: support the "symbolClass" parameter!

	TRACE(("found: %lx (%lx + %lx)\n",
		symbol->st_value + image->text_region.delta,
		symbol->st_value, image->text_region.delta));

	*_symbol = (void *)(symbol->st_value + image->text_region.delta);

done:
	mutex_unlock(&sImageMutex);
	return status;
}


//	#pragma mark - kernel private API


/*!	Looks up a symbol by address in all images loaded in kernel space.
	Note, if you need to call this function outside a debugger, make
	sure you fix locking and the way it returns its information, first!
*/
status_t
elf_debug_lookup_symbol_address(addr_t address, addr_t *_baseAddress,
	const char **_symbolName, const char **_imageName, bool *_exactMatch)
{
	struct elf_image_info *image;
	struct Elf32_Sym *symbolFound = NULL;
	const char *symbolName = NULL;
	addr_t deltaFound = INT_MAX;
	bool exactMatch = false;
	status_t status;

	TRACE(("looking up %p\n", (void *)address));

	if (!sInitialized)
		return B_ERROR;

	//mutex_lock(&sImageMutex);

	image = find_image_at_address(address);
		// get image that may contain the address

	if (image != NULL) {
		addr_t symbolDelta;
		uint32 i;
		int32 j;

		TRACE((" image %p, base = %p, size = %p\n", image,
			(void *)image->text_region.start, (void *)image->text_region.size));

		if (image->debug_symbols != NULL) {
			// search extended debug symbol table (contains static symbols)

			TRACE((" searching debug symbols...\n"));

			for (i = 0; i < image->num_debug_symbols; i++) {
				struct Elf32_Sym *symbol = &image->debug_symbols[i];

				if (symbol->st_value == 0 || symbol->st_size
						>= image->text_region.size + image->data_region.size)
					continue;

				symbolDelta
					= address - (symbol->st_value + image->text_region.delta);
				if (symbolDelta >= 0 && symbolDelta < symbol->st_size)
					exactMatch = true;

				if (exactMatch || symbolDelta < deltaFound) {
					deltaFound = symbolDelta;
					symbolFound = symbol;
					symbolName = image->debug_string_table + symbol->st_name;

					if (exactMatch)
						break;
				}
			}
		} else {
			// search standard symbol lookup table

			TRACE((" searching standard symbols...\n"));

			for (i = 0; i < HASHTABSIZE(image); i++) {
				for (j = HASHBUCKETS(image)[i]; j != STN_UNDEF;
						j = HASHCHAINS(image)[j]) {
					struct Elf32_Sym *symbol = &image->syms[j];

					if (symbol->st_value == 0
						|| symbol->st_size >= image->text_region.size
							+ image->data_region.size)
						continue;

					symbolDelta = address - (long)(symbol->st_value
						+ image->text_region.delta);
					if (symbolDelta >= 0 && symbolDelta < symbol->st_size)
						exactMatch = true;

					if (exactMatch || symbolDelta < deltaFound) {
						deltaFound = symbolDelta;
						symbolFound = symbol;
						symbolName = SYMNAME(image, symbol);

						if (exactMatch)
							goto symbol_found;
					}
				}
			}
		}
	}
symbol_found:

	if (symbolFound != NULL) {
		if (_symbolName)
			*_symbolName = symbolName;
		if (_imageName)
			*_imageName = image->name;
		if (_baseAddress)
			*_baseAddress = symbolFound->st_value + image->text_region.delta;
		if (_exactMatch)
			*_exactMatch = exactMatch;

		status = B_OK;
	} else if (image != NULL) {
		TRACE(("symbol not found!\n"));

		if (_symbolName)
			*_symbolName = NULL;
		if (_imageName)
			*_imageName = image->name;
		if (_baseAddress)
			*_baseAddress = image->text_region.start;
		if (_exactMatch)
			*_exactMatch = false;

		status = B_OK;
	} else {
		TRACE(("image not found!\n"));
		status = B_ENTRY_NOT_FOUND;
	}

	// Note, theoretically, all information we return back to our caller
	// would have to be locked - but since this function is only called
	// from the debugger, it's safe to do it this way

	//mutex_unlock(&sImageMutex);

	return status;
}


/*!	Tries to find a matching user symbol for the given address.
	Note that the given team's address space must already be in effect.
*/
status_t
elf_debug_lookup_user_symbol_address(struct team* team, addr_t address,
	addr_t *_baseAddress, const char **_symbolName, const char **_imageName,
	bool *_exactMatch)
{
	if (team == NULL || team == team_get_kernel_team())
		return B_BAD_VALUE;

	UserSymbolLookup& lookup = UserSymbolLookup::Default();
	status_t error = lookup.Init(team);
	if (error != B_OK)
		return error;

	return lookup.LookupSymbolAddress(address, _baseAddress, _symbolName,
		_imageName, _exactMatch);
}


status_t
elf_load_user_image(const char *path, struct team *team, int flags,
	addr_t *entry)
{
	struct Elf32_Ehdr elfHeader;
	struct Elf32_Phdr *programHeaders = NULL;
	char baseName[B_OS_NAME_LENGTH];
	status_t status;
	ssize_t length;
	int fd;
	int i;

	TRACE(("elf_load: entry path '%s', team %p\n", path, team));

	fd = _kern_open(-1, path, O_RDONLY, 0);
	if (fd < 0)
		return fd;

	// read and verify the ELF header

	length = _kern_read(fd, 0, &elfHeader, sizeof(elfHeader));
	if (length < B_OK) {
		status = length;
		goto error;
	}

	if (length != sizeof(elfHeader)) {
		// short read
		status = B_NOT_AN_EXECUTABLE;
		goto error;
	}
	status = verify_eheader(&elfHeader);
	if (status < B_OK)
		goto error;

	// read program header

	programHeaders = (struct Elf32_Phdr *)malloc(
		elfHeader.e_phnum * elfHeader.e_phentsize);
	if (programHeaders == NULL) {
		dprintf("error allocating space for program headers\n");
		status = B_NO_MEMORY;
		goto error;
	}

	TRACE(("reading in program headers at 0x%lx, length 0x%x\n",
		elfHeader.e_phoff, elfHeader.e_phnum * elfHeader.e_phentsize));
	length = _kern_read(fd, elfHeader.e_phoff, programHeaders,
		elfHeader.e_phnum * elfHeader.e_phentsize);
	if (length < B_OK) {
		status = length;
		dprintf("error reading in program headers\n");
		goto error;
	}
	if (length != elfHeader.e_phnum * elfHeader.e_phentsize) {
		dprintf("short read while reading in program headers\n");
		status = -1;
		goto error;
	}

	// construct a nice name for the region we have to create below
	{
		int32 length;

		const char *leaf = strrchr(path, '/');
		if (leaf == NULL)
			leaf = path;
		else
			leaf++;

		length = strlen(leaf);
		if (length > B_OS_NAME_LENGTH - 8)
			sprintf(baseName, "...%s", leaf + length + 8 - B_OS_NAME_LENGTH);
		else
			strcpy(baseName, leaf);
	}

	// map the program's segments into memory

	for (i = 0; i < elfHeader.e_phnum; i++) {
		char regionName[B_OS_NAME_LENGTH];
		char *regionAddress;
		area_id id;

		if (programHeaders[i].p_type != PT_LOAD)
			continue;

		regionAddress = (char *)ROUNDOWN(programHeaders[i].p_vaddr,
			B_PAGE_SIZE);
		if (programHeaders[i].p_flags & PF_WRITE) {
			// rw/data segment
			uint32 memUpperBound = (programHeaders[i].p_vaddr % B_PAGE_SIZE)
				+ programHeaders[i].p_memsz;
			uint32 fileUpperBound = (programHeaders[i].p_vaddr % B_PAGE_SIZE)
				+ programHeaders[i].p_filesz;

			memUpperBound = ROUNDUP(memUpperBound, B_PAGE_SIZE);
			fileUpperBound = ROUNDUP(fileUpperBound, B_PAGE_SIZE);

			sprintf(regionName, "%s_seg%drw", baseName, i);

			id = vm_map_file(team->id, regionName, (void **)&regionAddress,
				B_EXACT_ADDRESS, fileUpperBound,
				B_READ_AREA | B_WRITE_AREA, REGION_PRIVATE_MAP,
				fd, ROUNDOWN(programHeaders[i].p_offset, B_PAGE_SIZE));
			if (id < B_OK) {
				dprintf("error mapping file data: %s!\n", strerror(id));
				status = B_NOT_AN_EXECUTABLE;
				goto error;
			}

			// clean garbage brought by mmap (the region behind the file,
			// at least parts of it are the bss and have to be zeroed)
			uint32 start = (uint32)regionAddress
				+ (programHeaders[i].p_vaddr % B_PAGE_SIZE)
				+ programHeaders[i].p_filesz;
			uint32 amount = fileUpperBound
				- (programHeaders[i].p_vaddr % B_PAGE_SIZE)
				- (programHeaders[i].p_filesz);
			memset((void *)start, 0, amount);

			// Check if we need extra storage for the bss - we have to do this if
			// the above region doesn't already comprise the memory size, too.

			if (memUpperBound != fileUpperBound) {
				size_t bssSize = memUpperBound - fileUpperBound;

				snprintf(regionName, B_OS_NAME_LENGTH, "%s_bss%d", baseName, i);

				regionAddress += fileUpperBound;
				id = create_area_etc(team->id, regionName,
					(void **)&regionAddress, B_EXACT_ADDRESS, bssSize,
					B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
				if (id < B_OK) {
					dprintf("error allocating bss area: %s!\n", strerror(id));
					status = B_NOT_AN_EXECUTABLE;
					goto error;
				}
			}
		} else {
			// assume ro/text segment
			snprintf(regionName, B_OS_NAME_LENGTH, "%s_seg%dro", baseName, i);

			id = vm_map_file(team->id, regionName, (void **)&regionAddress,
				B_EXACT_ADDRESS, ROUNDUP(programHeaders[i].p_memsz
					+ (programHeaders[i].p_vaddr % B_PAGE_SIZE), B_PAGE_SIZE),
				B_READ_AREA | B_EXECUTE_AREA, REGION_PRIVATE_MAP,
				fd, ROUNDOWN(programHeaders[i].p_offset, B_PAGE_SIZE));
			if (id < B_OK) {
				dprintf("error mapping file text: %s!\n", strerror(id));
				status = B_NOT_AN_EXECUTABLE;
				goto error;
			}
		}
	}

	TRACE(("elf_load: done!\n"));

	*entry = elfHeader.e_entry;
	status = B_OK;

error:
	if (programHeaders)
		free(programHeaders);
	_kern_close(fd);

	return status;
}


image_id
load_kernel_add_on(const char *path)
{
	struct Elf32_Phdr *programHeaders;
	struct Elf32_Ehdr *elfHeader;
	struct elf_image_info *image;
	const char *fileName;
	void *reservedAddress;
	addr_t start;
	size_t reservedSize;
	status_t status;
	ssize_t length;

	TRACE(("elf_load_kspace: entry path '%s'\n", path));

	int fd = _kern_open(-1, path, O_RDONLY, 0);
	if (fd < 0)
		return fd;

	struct vnode *vnode;
	status = vfs_get_vnode_from_fd(fd, true, &vnode);
	if (status < B_OK)
		goto error0;

	// get the file name
	fileName = strrchr(path, '/');
	if (fileName == NULL)
		fileName = path;
	else
		fileName++;

	// Prevent someone else from trying to load this image
	mutex_lock(&sImageLoadMutex);

	// make sure it's not loaded already. Search by vnode
	image = find_image_by_vnode(vnode);
	if (image) {
		atomic_add(&image->ref_count, 1);
		goto done;
	}

	elfHeader = (struct Elf32_Ehdr *)malloc(sizeof(*elfHeader));
	if (!elfHeader) {
		status = B_NO_MEMORY;
		goto error;
	}

	length = _kern_read(fd, 0, elfHeader, sizeof(*elfHeader));
	if (length < B_OK) {
		status = length;
		goto error1;
	}
	if (length != sizeof(*elfHeader)) {
		// short read
		status = B_NOT_AN_EXECUTABLE;
		goto error1;
	}
	status = verify_eheader(elfHeader);
	if (status < B_OK)
		goto error1;

	image = create_image_struct();
	if (!image) {
		status = B_NO_MEMORY;
		goto error1;
	}
	image->vnode = vnode;
	image->elf_header = elfHeader;
	image->name = strdup(path);

	programHeaders = (struct Elf32_Phdr *)malloc(elfHeader->e_phnum
		* elfHeader->e_phentsize);
	if (programHeaders == NULL) {
		dprintf("%s: error allocating space for program headers\n", fileName);
		status = B_NO_MEMORY;
		goto error2;
	}

	TRACE(("reading in program headers at 0x%lx, length 0x%x\n",
		elfHeader->e_phoff, elfHeader->e_phnum * elfHeader->e_phentsize));

	length = _kern_read(fd, elfHeader->e_phoff, programHeaders,
		elfHeader->e_phnum * elfHeader->e_phentsize);
	if (length < B_OK) {
		status = length;
		TRACE(("%s: error reading in program headers\n", fileName));
		goto error3;
	}
	if (length != elfHeader->e_phnum * elfHeader->e_phentsize) {
		TRACE(("%s: short read while reading in program headers\n", fileName));
		status = B_ERROR;
		goto error3;
	}

	// determine how much space we need for all loaded segments

	reservedSize = 0;
	length = 0;

	for (int32 i = 0; i < elfHeader->e_phnum; i++) {
		size_t end;

		if (programHeaders[i].p_type != PT_LOAD)
			continue;

		length += ROUNDUP(programHeaders[i].p_memsz
			+ (programHeaders[i].p_vaddr % B_PAGE_SIZE), B_PAGE_SIZE);

		end = ROUNDUP(programHeaders[i].p_memsz + programHeaders[i].p_vaddr,
			B_PAGE_SIZE);
		if (end > reservedSize)
			reservedSize = end;
	}

	// Check whether the segments have an unreasonable amount of unused space
	// inbetween.
	if ((ssize_t)reservedSize > length + 8 * 1024) {
		status = B_BAD_DATA;
		goto error1;
	}

	// reserve that space and allocate the areas from that one
	if (vm_reserve_address_range(vm_kernel_address_space_id(), &reservedAddress,
			B_ANY_KERNEL_ADDRESS, reservedSize, 0) < B_OK) {
		status = B_NO_MEMORY;
		goto error3;
	}

	start = (addr_t)reservedAddress;
	image->data_region.size = 0;
	image->text_region.size = 0;

	for (int32 i = 0; i < elfHeader->e_phnum; i++) {
		char regionName[B_OS_NAME_LENGTH];
		elf_region *region;

		TRACE(("looking at program header %ld\n", i));

		switch (programHeaders[i].p_type) {
			case PT_LOAD:
				break;
			case PT_DYNAMIC:
				image->dynamic_section = programHeaders[i].p_vaddr;
				continue;
			default:
				dprintf("%s: unhandled pheader type 0x%lx\n", fileName,
					programHeaders[i].p_type);
				continue;
		}

		// we're here, so it must be a PT_LOAD segment
		if (programHeaders[i].IsReadWrite()) {
			// this is the writable segment
			if (image->data_region.size != 0) {
				// we've already created this segment
				continue;
			}
			region = &image->data_region;

			snprintf(regionName, B_OS_NAME_LENGTH, "%s_data", fileName);
		} else if (programHeaders[i].IsExecutable()) {
			// this is the non-writable segment
			if (image->text_region.size != 0) {
				// we've already created this segment
				continue;
			}
			region = &image->text_region;

			snprintf(regionName, B_OS_NAME_LENGTH, "%s_text", fileName);
		} else {
			dprintf("%s: weird program header flags 0x%lx\n", fileName,
				programHeaders[i].p_flags);
			continue;
		}

		region->start = (addr_t)reservedAddress + ROUNDOWN(
			programHeaders[i].p_vaddr, B_PAGE_SIZE);
		region->size = ROUNDUP(programHeaders[i].p_memsz
			+ (programHeaders[i].p_vaddr % B_PAGE_SIZE), B_PAGE_SIZE);
		region->id = create_area(regionName, (void **)&region->start,
			B_EXACT_ADDRESS, region->size, B_FULL_LOCK,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (region->id < B_OK) {
			dprintf("%s: error allocating area: %s\n", fileName,
				strerror(region->id));
			status = B_NOT_AN_EXECUTABLE;
			goto error4;
		}
		region->delta = -ROUNDOWN(programHeaders[i].p_vaddr, B_PAGE_SIZE);

		TRACE(("elf_load_kspace: created area \"%s\" at %p\n",
			regionName, (void *)region->start));

		length = _kern_read(fd, programHeaders[i].p_offset,
			(void *)(region->start + (programHeaders[i].p_vaddr % B_PAGE_SIZE)),
			programHeaders[i].p_filesz);
		if (length < B_OK) {
			status = length;
			dprintf("%s: error reading in segment %ld\n", fileName, i);
			goto error5;
		}
	}

	// get the segment order
	elf_region *firstRegion;
	elf_region *secondRegion;
	if (image->text_region.start < image->data_region.start) {
		firstRegion = &image->text_region;
		secondRegion = &image->data_region;
	} else {
		firstRegion = &image->data_region;
		secondRegion = &image->text_region;
	}

	image->data_region.delta += image->data_region.start;
	image->text_region.delta += image->text_region.start;

	// modify the dynamic ptr by the delta of the regions
	image->dynamic_section += image->text_region.delta;

	status = elf_parse_dynamic_section(image);
	if (status < B_OK)
		goto error5;

	status = elf_relocate(image, NULL);
	if (status < B_OK)
		goto error5;

	// We needed to read in the contents of the "text" area, but
	// now we can protect it read-only/execute
	set_area_protection(image->text_region.id,
		B_KERNEL_READ_AREA | B_KERNEL_EXECUTE_AREA);

	// There might be a hole between the two segments, and we don't need to
	// reserve this any longer
	vm_unreserve_address_range(vm_kernel_address_space_id(), reservedAddress,
		reservedSize);

	// ToDo: this should be enabled by kernel settings!
	if (1)
		load_elf_symbol_table(fd, image);

	free(programHeaders);
	mutex_lock(&sImageMutex);
	register_elf_image(image);
	mutex_unlock(&sImageMutex);

done:
	_kern_close(fd);
	mutex_unlock(&sImageLoadMutex);

	return image->id;

error5:
	delete_area(image->data_region.id);
	delete_area(image->text_region.id);
error4:
	vm_unreserve_address_range(vm_kernel_address_space_id(), reservedAddress,
		reservedSize);
error3:
	free(programHeaders);
error2:
	free(image);
error1:
	free(elfHeader);
error:
	mutex_unlock(&sImageLoadMutex);
error0:
	dprintf("Could not load kernel add-on \"%s\": %s\n", path,
		strerror(status));

	if (vnode)
		vfs_put_vnode(vnode);
	_kern_close(fd);

	return status;
}


status_t
unload_kernel_add_on(image_id id)
{
	struct elf_image_info *image;
	status_t status;

	mutex_lock(&sImageLoadMutex);
	mutex_lock(&sImageMutex);

	image = find_image(id);
	if (image != NULL)
		status = unload_elf_image(image);
	else
		status = B_BAD_IMAGE_ID;

	mutex_unlock(&sImageMutex);
	mutex_unlock(&sImageLoadMutex);

	return status;
}


status_t
elf_init(kernel_args *args)
{
	struct preloaded_image *image;

	image_init();

	sImagesHash = hash_init(IMAGE_HASH_SIZE, 0, image_compare, image_hash);
	if (sImagesHash == NULL)
		return B_NO_MEMORY;

	// Build a image structure for the kernel, which has already been loaded.
	// The preloaded_images were already prepared by the VM.
	if (insert_preloaded_image(&args->kernel_image, true) < B_OK)
		panic("could not create kernel image.\n");

	// Build image structures for all preloaded images.
	for (image = args->preloaded_images; image != NULL; image = image->next) {
		insert_preloaded_image(image, false);
	}

	add_debugger_command("ls", &dump_address_info,
		"lookup symbol for a particular address");
	add_debugger_command("symbols", &dump_symbols, "dump symbols for image");
	add_debugger_command("symbol", &dump_symbol, "search symbol in images");
	add_debugger_command("image", &dump_image, "dump image info");

	sInitialized = true;
	return B_OK;
}

