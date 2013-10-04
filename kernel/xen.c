/**
 * xen.c
 *
 * Hypercalls to the xen hypervisor.
 *
 * Some of the functions are of the form:
 * if (current_ring() != RING1) {
 *     // do xen via proxy
 *     return return_val;
 * }
 * return xen_op(...)
 *
 * This is to allow the kernel tasks, running in ring 2 to make xen calls.
 *
 * Copyright (c) 2006, I Kelly
 */
#include <minix/type.h>
#include "kernel.h"
#include <xen/xen.h>
#include <xen/evtchn.h>
#include "protect.h"

PRIVATE multicall_entry_t xen_proxy_op;
PRIVATE int xen_proxy_op_ret;

/**
 * Load the GDT which has been initialised by the minix builder.
 * The segment selectors are not set until after this function returns
 * however, as that must be done from assembly.
 * The return value is the address of the start_info, adjusted for the
 * position of the data segment.
 */
PUBLIC unsigned long xen_init_gdt(msi)
     minix_start_info_t *msi;
{
  unsigned long msi_addr = (unsigned long) msi;

  hypervisor_set_gdt(msi->setup_info.gdt_mfns,
		     NR_GDT_MF * (PAGE_SIZE /
				  sizeof(struct segdesc_s)));

  return msi_addr - msi->setup_info.processes[0].ds;
}

void xen_proxy_int_lock()
{
  lock(0, "foo");
  xen_proxy_int();
  unlock(0);
}

/**
 * Write a string to the xen debug console.
 */
PUBLIC int hypervisor_console_write(string, length)
     char *string;
     int length;
{
  if (current_ring() != RING1) {
    xen_proxy_op.op = __HYPERVISOR_console_io;
    xen_proxy_op.args[0] = CONSOLEIO_write;
    xen_proxy_op.args[1] = length;
    xen_proxy_op.args[2] = vir2phys(string);
    xen_proxy_int();
    return xen_proxy_op_ret;
  }

  return xen_op(__HYPERVISOR_console_io, CONSOLEIO_write, length,
		vir2phys(string));
}

/**
 * Set the GDT, equivelant to lgdt in the non-xen world
 */  
PUBLIC int hypervisor_set_gdt(framelist, entries)
     unsigned long *framelist;
     int entries;
{
  return xen_op(__HYPERVISOR_set_gdt, framelist, entries);
}

/**
 * Update a descriptor in the GDT (or LDT, though that isn't used in this port)
 * Descriptors cannot be edited directly as the pages for the GDT must be designated
 * as read only.
 */ 
PUBLIC int hypervisor_update_descriptor(index, segdp)
     unsigned long index;
     struct segdesc_s *segdp;
{
  unsigned long *ptr = (unsigned long *) segdp;
  unsigned long word1 = *ptr;
  unsigned long word2 = *(++ptr);
  int gdt_frame_index =
    (sizeof(struct segdesc_s) * index) >> PAGE_SHIFT;
  int gdt_frame_offset =
    (sizeof(struct segdesc_s) * index) % PAGE_SIZE;
  unsigned long ma =
    (hypervisor_start_info->setup_info.
     gdt_mfns[gdt_frame_index] << PAGE_SHIFT)
    | gdt_frame_offset;

  u8_t access = segdp->access;
  int i = 0;

  if (current_ring() != RING1) {
    xen_proxy_op.op = __HYPERVISOR_update_descriptor;
    xen_proxy_op.args[0] = ma;
    xen_proxy_op.args[1] = word1;
    xen_proxy_op.args[2] = word2;
    xen_proxy_int();
    return xen_proxy_op_ret;
  }

  return xen_op(__HYPERVISOR_update_descriptor, ma, word1, word2);
}

/**
 * Set the IDT, equivelant to lidt assembly instruction.
 * The traps set here will only be used for software interrupts and exceptions
 * Hardware related stuff is handled by the callbacks.
 */
PUBLIC int hypervisor_set_trap_table(traps)
     trap_info_t * traps;
{
  return xen_op(__HYPERVISOR_set_trap_table, vir2phys(traps));
}

/**
 * Set the callbacks that xen will use to notify the guest about hardware events.
 */
PUBLIC int hypervisor_set_callbacks(event_selector, event_address,
				    failsafe_selector, failsafe_address)
     unsigned long event_selector;
     unsigned long event_address;
     unsigned long failsafe_selector;
     unsigned long failsafe_address;
{
  return xen_op(__HYPERVISOR_set_callbacks, event_selector,
		event_address, failsafe_selector, failsafe_address);
}

/**
 * Perform an event channel operation.
 * This is basically used to tell xen to do something with hardware.
 */
PUBLIC int hypervisor_event_channel_op(t)
     evtchn_op_t *t;
{
  int ret;

  if (current_ring() != RING1) {
    xen_proxy_op.op = __HYPERVISOR_event_channel_op;
    xen_proxy_op.args[0] = vir2phys(t);
    xen_proxy_int();
    return xen_proxy_op_ret;
  }

  ret = xen_op(__HYPERVISOR_event_channel_op, vir2phys(t));

  return ret;
}

/**
 * Get the xen version.
 * Is not actually used for that, but to force a hypervisor_callback.
 */
PUBLIC int hypervisor_xen_version()
{
  if (current_ring() != RING1) {
    xen_proxy_op.op = __HYPERVISOR_xen_version;
    xen_proxy_op.args[0] = 0;
    xen_proxy_int();
    return xen_proxy_op_ret;
  }

  return xen_op(__HYPERVISOR_xen_version, 0);
}

/**
 * Shutdown the virtual machine
 */
PUBLIC int hypervisor_shutdown()
{
  if (current_ring() != RING1) {
    xen_proxy_op.op = __HYPERVISOR_sched_op;
    xen_proxy_op.args[0] = SCHEDOP_shutdown;
    xen_proxy_int();
    return xen_proxy_op_ret;
  }

  return xen_op(__HYPERVISOR_sched_op, SCHEDOP_shutdown);
}

/**
 * Yield the processor to another VM because this VM is waiting on I/O or similar
 */
PUBLIC int hypervisor_yield()
{
  if (current_ring() != RING1) {
    xen_proxy_op.op = __HYPERVISOR_sched_op;
    xen_proxy_op.args[0] = SCHEDOP_yield;
    xen_proxy_int();
    return xen_proxy_op_ret;
  }
  return xen_op(__HYPERVISOR_sched_op, SCHEDOP_yield);
}

/**
 * Execute the saved xen proxy operation.
 * Public because it needs to be called from klibxen.s.
 */
PUBLIC void do_xen_proxy_op()
{
  xen_proxy_op_ret = xen_op(xen_proxy_op.op,
			    xen_proxy_op.args[0],
			    xen_proxy_op.args[1],
			    xen_proxy_op.args[2],
			    xen_proxy_op.args[3],
			    xen_proxy_op.args[4]);
}

/**
 * Print a single character to the xen debug console
 */
void xen_debug_putc(char c)
{
  hypervisor_console_write(&c, 1);
}
