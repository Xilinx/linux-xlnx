/*  Kernel module help for Nios2.
    Copyright (C) 2004 Microtronix Datacom Ltd.
    Copyright (C) 2001,03  Rusty Russell

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
    
    Written by Wentao Xu <xuwentao@microtronix.com>
*/
#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt , ...)
#endif

void *module_alloc(unsigned long size)
{
	if (size == 0)
		return NULL;
	return vmalloc(size);
}


/* Free memory returned from module_alloc */
void module_free(struct module *mod, void *module_region)
{
	vfree(module_region);
	/* FIXME: If module_region == mod->init_region, trim exception
           table entries. */
}

/* We don't need anything special. */
int module_frob_arch_sections(Elf_Ehdr *hdr,
			      Elf_Shdr *sechdrs,
			      char *secstrings,
			      struct module *mod)
{
	return 0;
}

int apply_relocate(Elf32_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	printk(KERN_ERR "module %s: NO-ADD RELOCATION unsupported\n",
	       me->name);
	return -ENOEXEC;
}


int apply_relocate_add (Elf32_Shdr *sechdrs, const char *strtab,
			unsigned int symindex, unsigned int relsec,
			struct module *mod)
{
	unsigned int i;
	Elf32_Rela *rela = (void *)sechdrs[relsec].sh_addr;

	DEBUGP ("Applying relocate section %u to %u\n", relsec,
		sechdrs[relsec].sh_info);

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof (*rela); i++) {
		/* This is where to make the change */
		uint32_t word;
		uint32_t *loc
			= ((void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
			   + rela[i].r_offset);
		/* This is the symbol it is referring to.  Note that all
		   undefined symbols have been resolved.  */
		Elf32_Sym *sym
			= ((Elf32_Sym *)sechdrs[symindex].sh_addr
			   + ELF32_R_SYM (rela[i].r_info));
		uint32_t v = sym->st_value + rela[i].r_addend;

		switch (ELF32_R_TYPE (rela[i].r_info)) {
		case R_NIOS2_NONE:
			break;
			
		case R_NIOS2_BFD_RELOC_32:
			*loc += v;
			break;
			
		case R_NIOS2_PCREL16:
			v -= (uint32_t)loc + 4;
			if ((int32_t)v > 0x7fff ||
					(int32_t)v < -(int32_t)0x8000) {
				printk(KERN_ERR
				       "module %s: relocation overflow\n",
				       mod->name);
				return -ENOEXEC;
			}
			word = *loc;
			*loc = ((((word >> 22) << 16) | (v & 0xffff)) << 6) | (word & 0x3f);
			break;
			
		case R_NIOS2_CALL26:
			if (v & 3) {
				printk(KERN_ERR
				       "module %s: dangerous relocation\n",
				       mod->name);
				return -ENOEXEC;
			}
			if ((v >> 28) != ((uint32_t)loc >> 28)) {
				printk(KERN_ERR
				       "module %s: relocation overflow\n",
				       mod->name);
				return -ENOEXEC;
			}
			*loc = (*loc & 0x3f) | ((v >> 2) << 6);
			break;
			
		case R_NIOS2_HI16:
			word = *loc;
			*loc = ((((word >> 22) << 16) | ((v >>16) & 0xffff)) << 6) | 
					(word & 0x3f);
			break;
					
		case R_NIOS2_LO16:
			word = *loc;
			*loc = ((((word >> 22) << 16) | (v & 0xffff)) << 6) | 
					(word & 0x3f);
			break;
					
		case R_NIOS2_HIADJ16:
			{
				Elf32_Addr word2;
				
				word = *loc;
				word2 = ((v >> 16) + ((v >> 15) & 1)) & 0xffff;
				*loc = ((((word >> 22) << 16) | word2) << 6) | 
						(word & 0x3f);
			}
			break;

		default:
			printk (KERN_ERR "module %s: Unknown reloc: %u\n",
				mod->name, ELF32_R_TYPE (rela[i].r_info));
			return -ENOEXEC;
		}
	}

	return 0;
}

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	return 0;
}

void module_arch_cleanup(struct module *mod)
{
}
