/*
 * module_load.c
 *
 * For loading a kernel module from disk into memory.
 *
 * Copyright: University of Utah
 */

#include <linux/slab.h>
#include <liblcd/liblcd.h>
#include <lcd_domains/microkernel.h>
#include <asm/lcd_domains/libvmfunc.h>
#include <linux/kallsyms.h>

/* HOST LOAD/UNLOAD -------------------------------------------------- */

static inline size_t mpath_size(char *mdir, char *module_name)
{
	size_t psize = 0;
	/* mdir/ */
	psize += strlen(mdir) + 1;
	/* module_name.ko */
	psize += strlen(module_name) + 3;
        /* Allow for trailing \0 */
	return psize + 1;
}

int __kliblcd_module_host_load(char *mdir, char *module_name, 
			struct module **m_out)
{
	int ret;
	struct module *m;
	char *mpath;
	size_t psize;
	/*
	 * Form module path
	 */
	psize = mpath_size(mdir, module_name);
	if (psize >= LCD_MPATH_SIZE) { 
		LIBLCD_ERR("path to lcd's module .ko is too big");
		LIBLCD_ERR("   mdir = %s, name = %s, size = %llu",
			mdir, module_name, psize);
		ret = -EINVAL;
		goto fail;
	}
	mpath = kmalloc(psize, GFP_KERNEL);
	if (!mpath) {
		LIBLCD_ERR("malloc module path");
		ret = -ENOMEM;
		goto fail0;
	}
	snprintf(mpath, psize, "%s/%s.ko", mdir, module_name);
	/*
	 * Load the requested module
	 */
	ret = request_lcd_module(mpath);
	if (ret < 0) {
		LIBLCD_ERR("load module failed for %s (ret = %d)", mpath, ret);
               /* To facilitate debugging */
               if (ret == -ENOENT)
                       LIBLCD_ERR("Maybe /sbin/lcd-insmod is missing?");
		goto fail1;
	}
	/*
	 * Find loaded module, and inc its ref counter; must hold module mutex
	 * while finding module.
	 */
	mutex_lock(&module_mutex);
	m = find_module(module_name);
	mutex_unlock(&module_mutex);	
	if (!m) {
		LIBLCD_ERR("couldn't find module");
		ret = -EIO;
		goto fail2;
	}
	if(!try_module_get(m)) {
		LIBLCD_ERR("incrementing module ref count");
		ret = -EIO;
		goto fail3;
	}

	*m_out = m;

	kfree(mpath);

	return 0;

fail3:
	ret = do_sys_delete_module(module_name, 0, 1);
	if (ret)
		LIBLCD_ERR("double fault: deleting module");
fail2:
fail1:
	*m_out = NULL;
	kfree(mpath);
fail0:
fail:
	return ret;
}

void __kliblcd_module_host_unload(char *module_name)
{
	int ret;
	struct module *m;
	/*
	 * Delete module
	 *
	 * We need to look it up so we can do a put
	 */
 	mutex_lock(&module_mutex);
 	m = find_module(module_name);
 	mutex_unlock(&module_mutex);
	if (!m) {
		LIBLCD_ERR("couldn't find module");
		goto fail;
	}
	module_put(m);
	ret = do_sys_delete_module(module_name, 0, 1);
	if (ret) {
		LIBLCD_ERR("Error deleting module from host %d", ret);
		goto fail;
	}

	goto out;
out:
fail:
	return;
}

/* HIGHER-LEVEL LOAD/UNLOAD ---------------------------------------- */
static int dup_module_pages(hva_t pages_base, unsigned long size,
			void **dup_pages_bits,
			cptr_t *dup_pages_cap_out)
{
	int ret;
	void *dup_pages;
	unsigned long unused1, unused2;
	/*
	 * Alloc dup pages, and memcpy the bits over
	 */
	dup_pages = lcd_vmalloc(size);
	if (!dup_pages) {
		LIBLCD_ERR("failed to vmalloc dup pages");
		ret = -ENOMEM;
		goto fail1;
	}
	LIBLCD_MSG("dup_page %p", dup_pages);
	memcpy(dup_pages, hva2va(pages_base), size);
	/*
	 * Look up the cptr to the vmalloc memory object capability
	 */
	ret = lcd_virt_to_cptr(__gva((unsigned long)dup_pages),
			dup_pages_cap_out,
			&unused1, &unused2);
	if (ret) {
		LIBLCD_ERR("vmalloc mem object lookup failed");
		goto fail2;
	}

	*dup_pages_bits = dup_pages;

	return 0;

fail2:
	lcd_vfree(dup_pages);
fail1:
	return ret;
}

static void dedup_module_pages(void *pages_bits)
{
	lcd_vfree(pages_bits);
}

int lcd_load_module(char *mdir, char *mname,
		void **m_init_bits,
		void **m_core_bits,
		cptr_t *m_init_pages,
		cptr_t *m_core_pages,
		gva_t *m_init_link_addr,
		gva_t *m_core_link_addr,
		unsigned long *m_init_size,
		unsigned long *m_core_size,
		gva_t *m_init_func_addr,
		unsigned long *m_struct_module_core_offset)
{
	int ret;
	struct module *m;

	/*
	 * Load module in host
	 */
	ret = __kliblcd_module_host_load(mdir, mname, &m);
	if (ret)
		goto fail1;

	/*
	 * Dup init and core pages so that LCD will use a copy
	 * separate from the host (this will protect things
	 * like the struct module, the module's symbol table,
	 * etc. that the host uses for certain operations).
	 */
	ret = dup_module_pages(va2hva(m->init_layout.base), m->init_layout.size,
			m_init_bits, m_init_pages);
	if (ret) {
		LIBLCD_ERR("failed to load module's init");
		goto fail2;
	}
	ret = dup_module_pages(va2hva(m->core_layout.base), m->core_layout.size,
			m_core_bits, m_core_pages);
	if (ret) {
		LIBLCD_ERR("failed to load module's core");
		goto fail3;
	}

	/*
	 * Extract addresses where init and core should be mapped (the
	 * link base addresses), sizes, and location of struct
	 * module in .ko image.
	 */
	*m_init_link_addr = __gva((unsigned long)m->init_layout.base);
	*m_core_link_addr = __gva((unsigned long)m->core_layout.base);
	*m_init_size = m->init_layout.size;
	*m_core_size = m->core_layout.size;
	*m_init_func_addr = __gva((unsigned long)m->init);
	*m_struct_module_core_offset =
		((unsigned long)m) - ((unsigned long)m->core_layout.base);

	LIBLCD_MSG("Loaded %s.ko: ", mname);
	printk("    init addr 0x%p init size 0x%x\n",
		m->init_layout.base, m->init_layout.size);
	printk("    core addr 0x%p core size 0x%x\n",
		m->core_layout.base, m->core_layout.size);

	/*
	 * Unload module from host -- we don't need the host module
	 * loader to hang onto it now that we've got the program
	 * bits.
	 */
	__kliblcd_module_host_unload(mname);

	return 0;

fail3:
	dedup_module_pages(*m_init_bits);
fail2:
	__kliblcd_module_host_unload(mname);
fail1:
	return ret;
}

void lcd_release_module(void *m_init_bits, void *m_core_bits)
{
	/*
	 * Delete duplicates
	 */
	dedup_module_pages(m_init_bits);
	dedup_module_pages(m_core_bits);
}

int lvd_load_module(char *mdir, char *mname,
		void **m_init_bits,
		void **m_core_bits,
		void **m_vmfunc_tr_bits,
		void **m_vmfunc_sb_bits,
		cptr_t *m_init_pages,
		cptr_t *m_core_pages,
		cptr_t *m_vmfunc_tr_pages,
		cptr_t *m_vmfunc_sb_pages,
		gva_t *m_init_link_addr,
		gva_t *m_core_link_addr,
		unsigned long *m_init_size,
		unsigned long *m_core_size,
		gva_t *m_init_func_addr,
		unsigned long *m_struct_module_core_offset,
		gva_t *m_vmfunc_tr_page_addr,
		unsigned long *m_vmfunc_tr_page_size,
		gva_t *m_vmfunc_sb_page_addr,
		unsigned long *m_vmfunc_sb_page_size
		)
{
	int ret;
	struct module *m;
	unsigned long vmfunc_tr_load_addr;
	unsigned long vmfunc_tr_load_addr_lcd;
	unsigned long vmfunc_tr_page_size;

	unsigned long vmfunc_sb_load_addr;
	unsigned long vmfunc_sb_load_addr_lcd;
	unsigned long vmfunc_sb_page_size;

	char mod_sym[100] = {0};

	/*
	 * Do these lookups before loading the LCD module. Otherwise, kallsyms
	 * is going to return this modules vmfunc_tr_load_addr, whereas we want
	 * KLCD's vmfunc_tr_load_addr
	 */
	vmfunc_tr_load_addr = (unsigned long) &__vmfunc_trampoline_load_addr;
	vmfunc_tr_page_size = (unsigned long) &__vmfunc_trampoline_page_size;

	vmfunc_sb_load_addr = (unsigned long) &__vmfunc_sboard_load_addr;
	vmfunc_sb_page_size = (unsigned long) &__vmfunc_sboard_page_size;
	/*
	 * Load module in host
	 */
	ret = __kliblcd_module_host_load(mdir, mname, &m);
	if (ret)
		goto fail1;

	/*
	 * Dup init and core pages so that LCD will use a copy
	 * separate from the host (this will protect things
	 * like the struct module, the module's symbol table,
	 * etc. that the host uses for certain operations).
	 */
	ret = dup_module_pages(va2hva(m->init_layout.base), m->init_layout.size,
			m_init_bits, m_init_pages);
	if (ret) {
		LIBLCD_ERR("failed to load module's init");
		goto fail2;
	}
	ret = dup_module_pages(va2hva(m->core_layout.base), m->core_layout.size,
			m_core_bits, m_core_pages);
	if (ret) {
		LIBLCD_ERR("failed to load module's core");
		goto fail3;
	}

	sprintf(mod_sym, "%s:%s", mname, "vmfunc_trampoline_load_addr");
	vmfunc_tr_load_addr_lcd = kallsyms_lookup_name(mod_sym);

	if (vmfunc_tr_load_addr && vmfunc_tr_page_size) {
		*m_vmfunc_tr_page_addr = __gva(vmfunc_tr_load_addr);
		*m_vmfunc_tr_page_size = vmfunc_tr_page_size;
	} else {
		*m_vmfunc_tr_page_addr = __gva(0UL);
	}

	if (vmfunc_tr_load_addr_lcd) {
		printk("%s, vmfunc_tr_load_addr: %lx, vmfunc_tr_load_add_lcd: %lx\n",
			__func__, vmfunc_tr_load_addr,
			*((unsigned long*)vmfunc_tr_load_addr_lcd));

		print_hex_dump(KERN_DEBUG, "vmfunc.load: ", DUMP_PREFIX_ADDRESS, 32, 1,
			(void*)(*(unsigned long*)vmfunc_tr_load_addr_lcd), 0x40,
			false);
	} else {
		ret = -EINVAL;
		goto fail2;
	}

	sprintf(mod_sym, "%s:%s", mname, "vmfunc_sboard_load_addr");
	vmfunc_sb_load_addr_lcd = kallsyms_lookup_name(mod_sym);

	if (vmfunc_sb_load_addr && vmfunc_sb_page_size) {
		*m_vmfunc_sb_page_addr = __gva(vmfunc_sb_load_addr);
		*m_vmfunc_sb_page_size = vmfunc_sb_page_size;
	} else {
		*m_vmfunc_sb_page_addr = __gva(0UL);
	}

	if (vmfunc_sb_load_addr_lcd) {
		printk("%s, vmfunc_sb_load_addr: %lx, vmfunc_sb_load_add_lcd: %lx\n",
			__func__, vmfunc_sb_load_addr,
			*((unsigned long*)vmfunc_sb_load_addr_lcd));

		if (0)
		print_hex_dump(KERN_DEBUG, "vmfunc.sb.load: ", DUMP_PREFIX_ADDRESS, 32, 1,
			(void*)(*(unsigned long*)vmfunc_sb_load_addr_lcd), 0x40,
			false);
	} else {
		ret = -EINVAL;
		goto fail2;
	}

	/* we make an additional copy for vmfunc page */
	ret = dup_module_pages(va2hva((void*)(*(unsigned long*)vmfunc_tr_load_addr_lcd)), vmfunc_tr_page_size,
			m_vmfunc_tr_bits, m_vmfunc_tr_pages);
	if (ret) {
		LIBLCD_ERR("failed to load module's core");
		goto fail4;
	}

	/* we make an additional copy for vmfunc_sb page */
	ret = dup_module_pages(va2hva((void*)(*(unsigned long*)vmfunc_sb_load_addr_lcd)), vmfunc_sb_page_size,
			m_vmfunc_sb_bits, m_vmfunc_sb_pages);
	if (ret) {
		LIBLCD_ERR("failed to load module's core");
		goto fail4;
	}

	print_hex_dump(KERN_DEBUG, "vmfunc.sboard.kernel: ", DUMP_PREFIX_ADDRESS, 32, 1,
			(void*)vmfunc_sb_load_addr, 0x100,
			false);

	print_hex_dump(KERN_DEBUG, "vmfunc.sboard_new_copy.load: ", DUMP_PREFIX_ADDRESS, 32, 1,
			(void*)(*(unsigned long*)m_vmfunc_sb_bits), 0x100,
			false);

	/*
	 * Extract addresses where init and core should be mapped (the
	 * link base addresses), sizes, and location of struct
	 * module in .ko image.
	 */
	*m_init_link_addr = __gva((unsigned long)m->init_layout.base);
	*m_core_link_addr = __gva((unsigned long)m->core_layout.base);
	*m_init_size = m->init_layout.size;
	*m_core_size = m->core_layout.size;
	*m_init_func_addr = __gva((unsigned long)m->init);
	*m_struct_module_core_offset =
		((unsigned long)m) - ((unsigned long)m->core_layout.base);

	LIBLCD_MSG("Loaded %s.ko: ", mname);
	printk("    init addr 0x%p init size 0x%x\n",
		m->init_layout.base, m->init_layout.size);
	printk("    core addr 0x%p core size 0x%x\n",
		m->core_layout.base, m->core_layout.size);
	printk("    vmfunc addr 0x%lx vmfunc size 0x%lx\n",
		gva_val(*m_vmfunc_tr_page_addr), *m_vmfunc_tr_page_size);
	printk("    vmfunc.sb addr 0x%lx vmfunc size 0x%lx\n",
		gva_val(*m_vmfunc_sb_page_addr), *m_vmfunc_sb_page_size);

	/*
	 * Unload module from host -- we don't need the host module
	 * loader to hang onto it now that we've got the program
	 * bits.
	 */
#ifndef CONFIG_LVD
	/* Do not unload the module from host. We will map these pages onto
	 * LVDs EPT.
	 */
	__kliblcd_module_host_unload(mname);
#endif
	return 0;
fail4:
	dedup_module_pages(*m_core_bits);
fail3:
	dedup_module_pages(*m_init_bits);
fail2:
	__kliblcd_module_host_unload(mname);
fail1:
	return ret;
}

void lvd_release_module(void *m_init_bits, void *m_core_bits, void *m_vmfunc_tr_bits, void *m_vmfunc_sb_bits)
{
	/*
	 * Delete duplicates
	 */
	dedup_module_pages(m_init_bits);
	dedup_module_pages(m_core_bits);
	dedup_module_pages(m_vmfunc_tr_bits);
	dedup_module_pages(m_vmfunc_sb_bits);
}

/* EXPORTS -------------------------------------------------- */

EXPORT_SYMBOL(lvd_load_module);
EXPORT_SYMBOL(lvd_release_module);
