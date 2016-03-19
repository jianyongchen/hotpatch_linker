#include "util.h"
#include "obj.h"
#include "file.h"



struct obj_file *
obj_load (int fp, Elf32_Half e_type, const char *filename)
{
    struct obj_file *f;
    ElfW(Shdr) *section_headers;
    int shnum, i;
    char *shstrtab;

    /* Read the file header.  */
    f = arch_new_file();
    memset(f, 0, sizeof(*f));
    f->symbol_cmp = strcmp;
    f->symbol_hash = obj_elf_hash;
    f->load_order_search_start = &f->load_order;

    file_lseek(fp, 0, SEEK_SET);

    if (file_read(fp, &f->header, sizeof(f->header)) != sizeof(f->header))
    {
        ERROR("cannot read ELF header from %s\n", filename);
        return NULL;
    }
    DEBUG("read ELF header from %s\n", filename);

    DEBUG("read ELF header from %0x %0x %0x %0x\n",
        f->header.e_ident[EI_MAG0],
        f->header.e_ident[EI_MAG1],
        f->header.e_ident[EI_MAG2],
        f->header.e_ident[EI_MAG3]);

    if (f->header.e_ident[EI_MAG0] != ELFMAG0
      || f->header.e_ident[EI_MAG1] != ELFMAG1
      || f->header.e_ident[EI_MAG2] != ELFMAG2
      || f->header.e_ident[EI_MAG3] != ELFMAG3)
    {
        ERROR("%s is not an ELF file\n", filename);
        return NULL;
    }

    if (f->header.e_ident[EI_CLASS] == ELFCLASS64)
    {
        DEBUG("Class: ELF64\n");
    }
    else
    {
        DEBUG("Class: %d \n", f->header.e_ident[EI_CLASS]);
    }

    if (f->header.e_ident[EI_DATA] == ELFDATA2LSB)
    {
        DEBUG("Data: 2 LSB\n");
    }
    else
    {
        DEBUG("Data: %d \n", f->header.e_ident[EI_DATA]);
    }


    DEBUG("Version: %d\n", f->header.e_ident[EI_VERSION]);


    if (f->header.e_ident[EI_CLASS] != ELFCLASSM
      || f->header.e_ident[EI_DATA] != ELFDATAM
      || f->header.e_ident[EI_VERSION] != EV_CURRENT
      || !MATCH_MACHINE(f->header.e_machine))
    {
        ERROR("ELF file %s not for this architecture", filename);
        return NULL;
    }

    DEBUG("elf's type: %d\n", f->header.e_type);

    if (f->header.e_type != e_type && e_type != ET_NONE)
    {
        switch (e_type) {
            case ET_REL:
	            ERROR("ELF file %s not a relocatable object\n", filename);
	            break;
            case ET_EXEC:
	            ERROR("ELF file %s not an executable object\n", filename);
	            break;
            default:
	            ERROR("ELF file %s has wrong type, expecting %d got %d\n",
		            filename, e_type, f->header.e_type);
	        break;
        }

        return NULL;
    }
    DEBUG("ELF file %s is a relocatable object\n", filename);

    /* Read the section headers.  */
    DEBUG("Size of section headers: %d\n", f->header.e_shentsize);
    if (f->header.e_shentsize != sizeof(ElfW(Shdr)))
    {
        ERROR("section header size mismatch %s: %lu != %lu",
            filename,
            (unsigned long)f->header.e_shentsize,
            (unsigned long)sizeof(ElfW(Shdr)));
        return NULL;
    }
    shnum = f->header.e_shnum;
    DEBUG("number of section header: %d\n", shnum);
    f->sections = xmalloc(sizeof(struct obj_section *) * shnum);
    memset(f->sections, 0, sizeof(struct obj_section *) * shnum);

    DEBUG("section header offset %d 0x%x\n", (unsigned int)f->header.e_shoff, (unsigned int)f->header.e_shoff);
    section_headers = alloca(sizeof(ElfW(Shdr)) * shnum);
    file_lseek(fp, f->header.e_shoff, SEEK_SET);

    if (file_read(fp, section_headers, sizeof(ElfW(Shdr))*shnum) != sizeof(ElfW(Shdr))*shnum)
    {
        ERROR("error reading ELF section headers %s: %m", filename);
        return NULL;
    }

    /* Read the section data.  */
    for (i = 0; i < shnum; ++i)
    {
        struct obj_section *sec;

        f->sections[i] = sec = arch_new_section();
        memset(sec, 0, sizeof(*sec));

        sec->header = section_headers[i];
        sec->idx = i;

        switch (sec->header.sh_type)
    	{
        	case SHT_NULL:
        	case SHT_NOTE:
        	case SHT_NOBITS:
        	  /* ignore */
        	  break;

        	case SHT_PROGBITS:
        	case SHT_SYMTAB:
        	case SHT_STRTAB:
        	case SHT_RELM:
                DEBUG("section size: 0x%x, offset: 0x%x\n", (unsigned int)sec->header.sh_size,(unsigned int)sec->header.sh_offset);
                if (sec->header.sh_size > 0)
        	    {
                    sec->contents = xmalloc(sec->header.sh_size);
        	        file_lseek(fp, sec->header.sh_offset, SEEK_SET);
        	        if (file_read(fp, sec->contents, sec->header.sh_size) != sec->header.sh_size)
        		    {
        		        ERROR("error reading ELF section data %s: %m", filename);
        		        return NULL;
        		    }
        	    }
        	    else
        	        sec->contents = NULL;
        	    break;
        #if SHT_RELM == SHT_REL
	        case SHT_RELA:
                if (sec->header.sh_size) {
                    ERROR("RELA relocations not supported on this architecture %s", filename);
            	    return NULL;
                }
                break;
        #else
	        case SHT_REL:
    	        if (sec->header.sh_size) {
    	            ERROR("REL relocations not supported on this architecture %s", filename);
    	            return NULL;
    	        }
    	        break;
        #endif
            default:
    	        if (sec->header.sh_type >= SHT_LOPROC)
    	        {
    	            if (arch_load_proc_section(sec, fp) < 0)
    		            return NULL;
    	            break;
    	        }
    	        ERROR("can't handle sections of type %ld %s",
    		        (long)sec->header.sh_type, filename);
    	        return NULL;
        }
    }

    /* Do what sort of interpretation as needed by each section.  */
    shstrtab = f->sections[f->header.e_shstrndx]->contents;

    for (i = 0; i < shnum; ++i)
    {
        struct obj_section *sec = f->sections[i];
        sec->name = shstrtab + sec->header.sh_name;
        DEBUG("name: %s\n", sec->name);
    }

    for (i = 0; i < shnum; ++i)
    {
        struct obj_section *sec = f->sections[i];

        if (strcmp(sec->name, ".modinfo") == 0 || strcmp(sec->name, ".modstring") == 0)
        {
            sec->header.sh_flags &= ~SHF_ALLOC;
        }

        if (sec->header.sh_flags & SHF_ALLOC)
        {
            DEBUG("section flag: %d\n", (int)sec->header.sh_flags);
            obj_insert_section_load_order(f, sec);
        }

        switch (sec->header.sh_type)
	    {
            case SHT_SYMTAB:
            {
	            unsigned long nsym, j;
	            char *strtab;
	            ElfW(Sym) *sym;

	            if (sec->header.sh_entsize != sizeof(ElfW(Sym)))
	            {
		            ERROR("symbol size mismatch %s: %lu != %lu",
		                filename,
		                (unsigned long)sec->header.sh_entsize,
		                (unsigned long)sizeof(ElfW(Sym)));
		            return NULL;
	            }

	            nsym = sec->header.sh_size / sizeof(ElfW(Sym));
	            strtab = f->sections[sec->header.sh_link]->contents;
	            sym = (ElfW(Sym) *) sec->contents;

	            /* Allocate space for a table of local symbols.  */
	            j = f->local_symtab_size = sec->header.sh_info;
                DEBUG("f->local_symtab_size: %d\n", (int)f->local_symtab_size);
	            f->local_symtab = xmalloc(j *= sizeof(struct obj_symbol *));
	            memset(f->local_symtab, 0, j);

	            /* Insert all symbols into the hash table.  */
	            for (j = 1, ++sym; j < nsym; ++j, ++sym)
	            {
		            const char *name;
		            if (sym->st_name)
		                name = strtab+sym->st_name;
		            else
		                name = f->sections[sym->st_shndx]->name;

                    DEBUG("sym name: %s\n", name);

                    {
                    #ifdef ARCH_sh64

                     /*
            		                * For sh64 it is possible that the target of a branch requires a
            		                * mode switch---32 to 16 and back again---and this is implied by
            		                * the lsb being set in the target address for SHmedia mode and clear
            		                * for SHcompact.
            		                */
                        int lsb;
                        lsb = (sym->st_other & 4) ? 1 : 0;

		                obj_add_symbol(f, name, j, sym->st_info,
				            sym->st_shndx,
				            sym->st_value | lsb, sym->st_size);
                    #else
		                obj_add_symbol(f, name, j, sym->st_info,
				            sym->st_shndx,
				            sym->st_value, sym->st_size);
                    #endif
                    }

	            }
	        }
            break;
        }
    }

    /* second pass to add relocation data to symbols */
    for (i = 0; i < shnum; ++i)
    {
        struct obj_section *sec = f->sections[i];
        switch (sec->header.sh_type)
	    {
	        case SHT_RELM:
	        {
	            unsigned long nrel, j, nsyms;
	            ElfW(RelM) *rel;
	            struct obj_section *symtab;
	            char *strtab;
	            if (sec->header.sh_entsize != sizeof(ElfW(RelM)))
	            {
		            ERROR("relocation entry size mismatch %s: %lu != %lu",
		                filename,
		                (unsigned long)sec->header.sh_entsize,
		                (unsigned long)sizeof(ElfW(RelM)));
		            return NULL;
	            }

	            nrel = sec->header.sh_size / sizeof(ElfW(RelM));
	            rel = (ElfW(RelM) *) sec->contents;
	            symtab = f->sections[sec->header.sh_link];
	            nsyms = symtab->header.sh_size / symtab->header.sh_entsize;
	            strtab = f->sections[symtab->header.sh_link]->contents;

	            /* Save the relocate type in each symbol entry.  */
	            for (j = 0; j < nrel; ++j, ++rel)
	            {
		            struct obj_symbol *intsym;
		            unsigned long symndx;
		            symndx = ELFW(R_SYM)(rel->r_info);
		            if (symndx)
		            {
		                if (symndx >= nsyms)
		                {
			                ERROR("%s: Bad symbol index: %08lx >= %08lx",
			                    filename, symndx, nsyms);
			                continue;
		                }
		                obj_find_relsym(intsym, f, f, rel, (ElfW(Sym) *)(symtab->contents), strtab);
		                intsym->r_type = ELFW(R_TYPE)(rel->r_info);
		            }
	            }
            }
	        break;
	    }
    }

  f->filename = xstrdup(filename);
  INFO("filename: %s\n", f->filename);
  return f;
}

