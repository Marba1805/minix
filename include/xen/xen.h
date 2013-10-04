/******************************************************************************
 * xen.h
 * 
 * Minix OS interface to Xen.
 * Most of this file has been taken from arch_x86-32.h.
 * There are some extra structures at the end, which are specific to minix,
 * and the minix xen builder.

 * Copyright (c) 2004, K A Fraser
 * Copyright (c) 2006, I Kelly
 */

#ifndef XEN_H
#define XEN_H

#include <minix/type.h>
#include <a.out.h>
#include <xen/xenasm.h>

/* NB. Both the following are 32 bits each. */
typedef unsigned long memory_t;	/* Full-sized pointer/address/memory-size. */
typedef unsigned long cpureg_t;	/* Full-sized register.                    */

/*
 * Send an array of these to HYPERVISOR_set_trap_table()
 */
#define TI_GET_DPL(_ti)      ((_ti)->flags & 3)
#define TI_GET_IF(_ti)       ((_ti)->flags & 4)
#define TI_SET_DPL(_ti,_dpl) ((_ti)->flags |= (_dpl))
#define TI_SET_IF(_ti,_if)   ((_ti)->flags |= ((!!(_if))<<2))
typedef struct {
  u8_t vector;		/* 0: exception vector                              */
  u8_t flags;		/* 1: 0-3: privilege level; 4: clear event enable?  */
  u16_t cs;		/* 2: code selector                                 */
  memory_t address;	/* 4: code address                                  */
} trap_info_t;		/* 8 bytes */

typedef struct {
  unsigned long ebx;
  unsigned long ecx;
  unsigned long edx;
  unsigned long esi;
  unsigned long edi;
  unsigned long ebp;
  unsigned long eax;
  unsigned long _unused;
  unsigned long eip;
  unsigned long cs;
  unsigned long eflags;
  unsigned long esp;
  unsigned long ss;
  unsigned long es;
  unsigned long ds;
  unsigned long fs;
  unsigned long gs;
} execution_context_t;

typedef u64_t tsc_timestamp_t;	/* RDTSC timestamp */

/*
 * The following is all CPU context. Note that the i387_ctxt block is filled 
 * in by FXSAVE if the CPU has feature FXSR; otherwise FSAVE is used.
 */
typedef struct {
#define ECF_I387_VALID (1<<0)
  unsigned long flags;
  execution_context_t cpu_ctxt;	/* User-level CPU registers     */
  char fpu_ctxt[256];	/* User-level FPU registers     */
  trap_info_t trap_ctxt[256];	/* Virtual IDT                  */
  unsigned int fast_trap_idx;	/* "Fast trap" vector offset    */
  unsigned long ldt_base, ldt_ents;	/* LDT (linear address, # ents) */
  unsigned long gdt_frames[16], gdt_ents;	/* GDT (machine frames, # ents) */
  unsigned long guestos_ss, guestos_esp;	/* Virtual TSS (only SS1/ESP1)  */
  unsigned long pt_base;	/* CR3 (pagetable base)         */
  unsigned long debugreg[8];	/* DB0-DB7 (debug registers)    */
  unsigned long event_callback_cs;	/* CS:EIP of event callback     */
  unsigned long event_callback_eip;
  unsigned long failsafe_callback_cs;	/* CS:EIP of failsafe callback  */
  unsigned long failsafe_callback_eip;
} full_execution_context_t;

typedef struct {
  u64_t mfn_to_pfn_start;	/* MFN of start of m2p table */
  u64_t pfn_to_mfn_frame_list;	/* MFN of a table of MFNs that 
				   make up p2m table */
} arch_shared_info_t;

#define ARCH_HAS_FAST_TRAP

/* from xen.h */

typedef u16_t domid_t;

/* Domain ids >= DOMID_FIRST_RESERVED cannot be used for ordinary domains. */
#define DOMID_FIRST_RESERVED (0x7FF0U)

/* DOMID_SELF is used in certain contexts to refer to oneself. */
#define DOMID_SELF (0x7FF0U)

/*
 * DOMID_IO is used to restrict page-table updates to mapping I/O memory.
 * Although no Foreign Domain need be specified to map I/O pages, DOMID_IO
 * is useful to ensure that no mappings to the OS's own heap are accidentally
 * installed. (e.g., in Linux this could cause havoc as reference counts
 * aren't adjusted on the I/O-mapping code path).
 * This only makes sense in MMUEXT_SET_FOREIGNDOM, but in that context can
 * be specified by any calling domain.
 */
#define DOMID_IO   (0x7FF1U)

/*
 * DOMID_XEN is used to allow privileged domains to map restricted parts of
 * Xen's heap space (e.g., the machine_to_phys table).
 * This only makes sense in MMUEXT_SET_FOREIGNDOM, and is only permitted if
 * the caller is privileged.
 */
#define DOMID_XEN  (0x7FF2U)

/*
 * Send an array of these to HYPERVISOR_mmu_update().
 * NB. The fields are natural pointer/address size for this architecture.
 */
typedef struct {
  memory_t ptr;		/* Machine address of PTE. */
  memory_t val;		/* New contents of PTE.    */
} mmu_update_t;

/*
 * Send an array of these to HYPERVISOR_multicall().
 * NB. The fields are natural register size for this architecture.
 */
typedef struct {
  cpureg_t op;
  cpureg_t args[7];
} multicall_entry_t;

/* Event channel endpoints per domain. */
#define NR_EVENT_CHANNELS 1024

/* No support for multi-processor guests. */
#define MAX_VIRT_CPUS 1

/*
 * Xen/guestos shared data -- pointer provided in start_info.
 * NB. We expect that this struct is smaller than a page.
 */
typedef struct {
  /*
   * Per-VCPU information goes here. This will be cleaned up more when Xen 
   * actually supports multi-VCPU guests.
   */
  struct {
    /*
     * 'evtchn_upcall_pending' is written non-zero by Xen to indicate
     * a pending notification for a particular VCPU. It is then cleared 
     * by the guest OS /before/ checking for pending work, thus avoiding
     * a set-and-check race. Note that the mask is only accessed by Xen
     * on the CPU that is currently hosting the VCPU. This means that the
     * pending and mask flags can be updated by the guest without special
     * synchronisation (i.e., no need for the x86 LOCK prefix).
     * This may seem suboptimal because if the pending flag is set by
     * a different CPU then an IPI may be scheduled even when the mask
     * is set. However, note:
     *  1. The task of 'interrupt holdoff' is covered by the per-event-
     *     channel mask bits. A 'noisy' event that is continually being
     *     triggered can be masked at source at this very precise
     *     granularity.
     *  2. The main purpose of the per-VCPU mask is therefore to restrict
     *     reentrant execution: whether for concurrency control, or to
     *     prevent unbounded stack usage. Whatever the purpose, we expect
     *     that the mask will be asserted only for short periods at a time,
     *     and so the likelihood of a 'spurious' IPI is suitably small.
     * The mask is read before making an event upcall to the guest: a
     * non-zero mask therefore guarantees that the VCPU will not receive
     * an upcall activation. The mask is cleared when the VCPU requests
     * to block: this avoids wakeup-waiting races.
     */
    u8_t evtchn_upcall_pending;
    u8_t evtchn_upcall_mask;
    u8_t pad0, pad1;
  } vcpu_data[MAX_VIRT_CPUS];	/*   0 */

  /*
   * A domain can have up to 1024 "event channels" on which it can send
   * and receive asynchronous event notifications. There are three classes
   * of event that are delivered by this mechanism:
   *  1. Bi-directional inter- and intra-domain connections. Domains must
   *     arrange out-of-band to set up a connection (usually the setup
   *     is initiated and organised by a privileged third party such as
   *     software running in domain 0).
   *  2. Physical interrupts. A domain with suitable hardware-access
   *     privileges can bind an event-channel port to a physical interrupt
   *     source.
   *  3. Virtual interrupts ('events'). A domain can bind an event-channel
   *     port to a virtual interrupt source, such as the virtual-timer
   *     device or the emergency console.
   * 
   * Event channels are addressed by a "port index" between 0 and 1023.
   * Each channel is associated with two bits of information:
   *  1. PENDING -- notifies the domain that there is a pending notification
   *     to be processed. This bit is cleared by the guest.
   *  2. MASK -- if this bit is clear then a 0->1 transition of PENDING
   *     will cause an asynchronous upcall to be scheduled. This bit is only
   *     updated by the guest. It is read-only within Xen. If a channel
   *     becomes pending while the channel is masked then the 'edge' is lost
   *     (i.e., when the channel is unmasked, the guest must manually handle
   *     pending notifications as no upcall will be scheduled by Xen).
   * 
   * To expedite scanning of pending notifications, any 0->1 pending
   * transition on an unmasked channel causes a corresponding bit in a
   * 32-bit selector to be set. Each bit in the selector covers a 32-bit
   * word in the PENDING bitfield array.
   */
  u32_t evtchn_pending[32];	/*   4 */
  u32_t evtchn_pending_sel;	/* 132 */
  u32_t evtchn_mask[32];	/* 136 */

  /*
   * Time: The following abstractions are exposed: System Time, Clock Time,
   * Domain Virtual Time. Domains can access Cycle counter time directly.
   */
  u64_t cpu_freq;		/* 264: CPU frequency (Hz).          */

  /*
   * The following values are updated periodically (and not necessarily
   * atomically!). The guest OS detects this because 'time_version1' is
   * incremented just before updating these values, and 'time_version2' is
   * incremented immediately after. See the Xen-specific Linux code for an
   * example of how to read these values safely (arch/xen/kernel/time.c).
   */
  u32_t time_version1;	/* 272 */
  u32_t time_version2;	/* 276 */
  tsc_timestamp_t tsc_timestamp;	/* TSC at last update of time vals.  */
  u64_t system_time;	/* Time, in nanosecs, since boot.    */
  u32_t wc_sec;		/* Secs  00:00:00 UTC, Jan 1, 1970.  */
  u32_t wc_usec;		/* Usecs 00:00:00 UTC, Jan 1, 1970.  */
  u64_t domain_time;	/* Domain virtual time, in nanosecs. */

  /*
   * Timeout values:
   * Allow a domain to specify a timeout value in system time and 
   * domain virtual time.
   */
  u64_t wall_timeout;	/* 312 */
  u64_t domain_timeout;	/* 320 */

  arch_shared_info_t arch;
} shared_info_t;

/*
 * Start-of-day memory layout for the initial domain (DOM0):
 *  1. The domain is started within contiguous virtual-memory region.
 *  2. The contiguous region begins and ends on an aligned 4MB boundary.
 *  3. The region start corresponds to the load address of the OS image.
 *     If the load address is not 4MB aligned then the address is rounded down.
 *  4. This the order of bootstrap elements in the initial virtual region:
 *      a. relocated kernel image
 *      b. initial ram disk              [mod_start, mod_len]
 *      c. list of allocated page frames [mfn_list, nr_pages]
 *      d. bootstrap page tables         [pt_base, CR3 (x86)]
 *      e. start_info_t structure        [register ESI (x86)]
 *      f. bootstrap stack               [register ESP (x86)]
 *  5. Bootstrap elements are packed together, but each is 4kB-aligned.
 *  6. The initial ram disk may be omitted.
 *  7. The list of page frames forms a contiguous 'pseudo-physical' memory
 *     layout for the domain. In particular, the bootstrap virtual-memory
 *     region is a 1:1 mapping to the first section of the pseudo-physical map.
 *  8. All bootstrap elements are mapped read-writable for the guest OS. The
 *     only exception is the bootstrap page table, which is mapped read-only.
 *  9. There is guaranteed to be at least 512kB padding after the final
 *     bootstrap element. If necessary, the bootstrap virtual region is
 *     extended by an extra 4MB to ensure this.
 */

#define MAX_CMDLINE 256
typedef struct {
  /* THE FOLLOWING ARE FILLED IN BOTH ON INITIAL BOOT AND ON RESUME.     */
  memory_t nr_pages;	/*  0: Total pages allocated to this domain. */
  _MEMORY_PADDING(A);
  memory_t shared_info;	/*  8: MACHINE address of shared info struct. */
  _MEMORY_PADDING(B);
  u32_t flags;		/* 16: SIF_xxx flags.                        */
  u16_t domain_controller_evtchn;	/* 20 */
  u16_t __pad;
  /* THE FOLLOWING ARE ONLY FILLED IN ON INITIAL BOOT (NOT RESUME).      */
  memory_t pt_base;	/* 24: VIRTUAL address of page directory.    */
  _MEMORY_PADDING(C);
  memory_t nr_pt_frames;	/* 32: Number of bootstrap p.t. frames.      */
  _MEMORY_PADDING(D);
  memory_t mfn_list;	/* 40: VIRTUAL address of page-frame list.   */
  _MEMORY_PADDING(E);
  memory_t mod_start;	/* 48: VIRTUAL address of pre-loaded module. */
  _MEMORY_PADDING(F);
  memory_t mod_len;	/* 56: Size (bytes) of pre-loaded module.    */
  _MEMORY_PADDING(G);
  u8_t cmd_line[MAX_CMDLINE];	/* 64 */
} start_info_t;		/* 320 bytes */

/* These flags are passed in the 'flags' field of start_info_t. */
#define SIF_PRIVILEGED    (1<<0)	/* Is the domain privileged? */
#define SIF_INITDOMAIN    (1<<1)	/* Is this the initial control domain? */
#define SIF_BLK_BE_DOMAIN (1<<4)	/* Is this a block backend domain? */
#define SIF_NET_BE_DOMAIN (1<<5)	/* Is this a net backend domain? */


/* must be same as in minix.h in xen/tools/libxc */
#define PROCESS_MAX     16	/* must match mpxxen.s */
#define IMAGE_NAME_MAX 63

typedef struct {
  u32_t entry;		/* Entry point. */
  u32_t cs;		/* Code segment. */
  u32_t ds;		/* Data segment. */
  u32_t data;		/* To access the data segment. */
  u32_t end;		/* End of this process, size = (end - cs). */
} process_t;

typedef struct {
  char name[IMAGE_NAME_MAX + 1];	/* nul terminated name */
  struct exec process;
} image_header_t;

#define NR_GDT_MF 16
/**
 * Extra information needed by minix, passed in along with the shared info
 */
typedef struct {
  unsigned long msi_vaddr;	/* virtual address of start_info struct */
  unsigned long fmem_vaddr;	/* virtual address of free memory */
  unsigned long gdt_vaddr;	/* virtual address of GDT */
  unsigned long gdt_mfns[NR_GDT_MF];	/* Machine frame numbers of the GDT */
  process_t processes[PROCESS_MAX];
  image_header_t procheaders[PROCESS_MAX];
} domain_setup_info_t;

typedef struct {
  start_info_t start_info;
  domain_setup_info_t setup_info;
  shared_info_t *shared_info;	/* vaddr of shared info */
} minix_start_info_t;

#define RING1 1
#define RING2 2
#define RING3 3

#define lock(c, v)      disable_evtchn_callbacks();
#define unlock(c)       enable_evtchn_callbacks();

#endif				/* XEN_H */
