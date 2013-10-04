/* This file contains code for initialization of protected mode, to initialize
 * code and data segment descriptors, and to initialize global descriptors
 * for local descriptors in the process table.
 *
 * Xen modifications - Copyright (c) 2006, I Kelly
 */

#include "kernel.h"
#include "proc.h"
#include "protect.h"

FORWARD _PROTOTYPE(void sdesc, (struct segdesc_s * segdp, phys_bytes base,
				vir_bytes size));

/*===========================================================================*
 *				prot_init				     *
 *===========================================================================*/
PUBLIC void xen_prot_init(void)
{
  /* Set up tables for protected mode.
   * All GDT slots are allocated at compile time.
   */
  unsigned gdt_index;
  register struct proc *rp;

  /* Build segment descriptors for tasks and interrupt handlers. */
  init_codeseg(CS_INDEX, kinfo.code_base, kinfo.code_size,
	       INTR_PRIVILEGE);
  /*init_dataseg(&gdt[DS_INDEX],
    kinfo.data_base, kinfo.data_size, INTR_PRIVILEGE); */
  init_dataseg(ES_INDEX, 0L, 0, TASK_PRIVILEGE);
  /* Build local descriptors in GDT for LDT's in process table.
   * The LDT's are allocated at compile time in the process table, and
   * initialized whenever a process' map is initialized or changed.
   */
  for (rp = BEG_PROC_ADDR, gdt_index = FIRST_AVAILABLE_GDT_ENTRY;
       rp < END_PROC_ADDR; ++rp, gdt_index += GDT_ENTRIES_PER_PROC) {
    rp->p_cs_idx = gdt_index;
    rp->p_ds_idx = gdt_index + 1;
    rp->p_extra_idx = gdt_index + 2;
  }
}

/*===========================================================================*
 *				init_codeseg				     *
 *===========================================================================*/
PUBLIC void init_codeseg(index, base, size, privilege)
unsigned long index;
phys_bytes base;
vir_bytes size;
int privilege;
{
  struct segdesc_s segd;

  /* Build descriptor for a code segment. */
  sdesc(&segd, base, size);
  segd.access = (privilege << DPL_SHIFT)
    | (PRESENT | SEGMENT | EXECUTABLE | READABLE);
  /* CONFORMING = 0, ACCESSED = 0 */

  hypervisor_update_descriptor(index, &segd);
}

/*===========================================================================*
 *				init_dataseg				     *
 *===========================================================================*/
PUBLIC void init_dataseg(index, base, size, privilege)
unsigned long index;
phys_bytes base;
vir_bytes size;
int privilege;
{
  struct segdesc_s segd;

  /* Build descriptor for a data segment. */
  sdesc(&segd, base, size);
  segd.access =
    (privilege << DPL_SHIFT) | (PRESENT | SEGMENT | WRITEABLE);
  /* EXECUTABLE = 0, EXPAND_DOWN = 0, ACCESSED = 0 */

  hypervisor_update_descriptor(index, &segd);
}

/*===========================================================================*
 *				sdesc					     *
 *===========================================================================*/
PRIVATE void sdesc(segdp, base, size)
struct segdesc_s *segdp;
phys_bytes base;
vir_bytes size;
{
/* Fill in the size fields (base, limit and granularity) of a descriptor. */
  segdp->base_low = base;
  segdp->base_middle = base >> BASE_MIDDLE_SHIFT;
  segdp->base_high = base >> BASE_HIGH_SHIFT;

  --size;			/* convert to a limit, 0 size means 4G */
  if (size > BYTE_GRAN_MAX) {
    segdp->limit_low = size >> PAGE_GRAN_SHIFT;
    segdp->granularity = GRANULAR | (size >>
				     (PAGE_GRAN_SHIFT +
				      GRANULARITY_SHIFT));
  } else {
    segdp->limit_low = size;
    segdp->granularity = size >> GRANULARITY_SHIFT;
  }
  segdp->granularity |= DEFAULT;	/* means BIG for data seg */
}

/*===========================================================================*
 *				seg2phys				     *
 *===========================================================================*/
PUBLIC phys_bytes seg2phys(seg)
U16_t seg;
{
/* Return the base address of a segment, with seg being either a 8086 segment
 * register, or a 286/386 segment selector.
 */
#ifndef XEN
  phys_bytes base;
  struct segdesc_s *segdp;

  if (! machine.protected) {
    base = hclick_to_physb(seg);
  } else {
    segdp = &gdt[seg >> 3];
    base =    ((u32_t) segdp->base_low << 0)
      | ((u32_t) segdp->base_middle << 16)
      | ((u32_t) segdp->base_high << 24);
  }
  return base;
#else
  return -1;
#endif
}

/*===========================================================================*
 *				phys2seg				     *
 *===========================================================================*/
PUBLIC void phys2seg(seg, off, phys)
u16_t *seg;
vir_bytes *off;
phys_bytes phys;
{
/* Return a segment selector and offset that can be used to reach a physical
 * address, for use by a driver doing memory I/O in the A0000 - DFFFF range.
 */
#ifndef XEN
#if _WORD_SIZE == 2 
  if (!machine.protected) {
    *seg = phys / HCLICK_SIZE;
    *off = phys % HCLICK_SIZE;
  } else {
	unsigned bank = phys >> 16;
	unsigned index = bank - 0xA + A_INDEX;
	init_dataseg(&gdt[index], (phys_bytes) bank << 16, 0, TASK_PRIVILEGE);
	*seg = (index * 0x08) | TASK_PRIVILEGE;
	*off = phys & 0xFFFF;
  }
#else
  *seg = FLAT_DS_SELECTOR;
  *off = phys;
#endif
#endif
}

/*===========================================================================*
 *				alloc_segments				     *
 *===========================================================================*/
PUBLIC void alloc_segments(rp)
register struct proc *rp;
{
/* This is called at system initialization from main() and by do_newmap(). 
 * The code has a separate function because of all hardware-dependencies.
 * Note that IDLE is part of the kernel and gets TASK_PRIVILEGE here.
 */
  phys_bytes code_bytes;
  phys_bytes data_bytes;
  int privilege;

  data_bytes = (phys_bytes) (rp->p_memmap[S].mem_vir +
			     rp->p_memmap[S].mem_len) << CLICK_SHIFT;
  if (rp->p_memmap[T].mem_len == 0)
    code_bytes = data_bytes;	/* common I&D, poor protect */
  else
    code_bytes =
      (phys_bytes) rp->p_memmap[T].mem_len << CLICK_SHIFT;

  if (iskernelp(rp)) {
    code_bytes = 0;
    data_bytes = 0;
  }

  privilege = (iskernelp(rp)) ? TASK_PRIVILEGE : USER_PRIVILEGE;

  init_codeseg(rp->p_cs_idx,
	       (phys_bytes) rp->p_memmap[T].mem_phys << CLICK_SHIFT,
	       code_bytes, privilege);
  init_dataseg(rp->p_ds_idx,
	       (phys_bytes) rp->p_memmap[D].mem_phys << CLICK_SHIFT,
	       data_bytes, privilege);
  rp->p_reg.cs = (rp->p_cs_idx << 3) | privilege;
  rp->p_reg.gs =
    rp->p_reg.fs =
    rp->p_reg.ss =
    rp->p_reg.es = rp->p_reg.ds = (rp->p_ds_idx << 3) | privilege;
}

PUBLIC void dump_gdt(offset, nr_entries)
unsigned long offset;
unsigned long nr_entries;
{
	int i = 0;
	unsigned long *gdt =
	    (unsigned long *) (hypervisor_start_info->setup_info.
			       gdt_vaddr -
			       hypervisor_start_info->setup_info.
			       processes[0].ds);

	xen_kprintf("===GDT Dump===\n");
	for (i = offset * 2; i < (nr_entries * 2) + (offset * 2); i += 2) {
		xen_kprintf("(%x) %x %x        ", i / 2, gdt[i],
			    gdt[i + 1]);
		if ((i % 3) == 0) {
			xen_kprintf("\n");
		}
	}
}
