/******************************************************************************
 * xenasm.h
 * 
 * Minix OS interface to Xen, Definitions.
 *
 * Most of this file has been taken from arch_x86-32.h.
 * There are some extra bits at the end, which this port required.
 *
 * Copyright (c) 2004, K A Fraser
 * Copyright (c) 2006, I Kelly
 */
#ifndef XENASM_H
#define XENASM_H

/*
 * XEN "SYSTEM CALLS" (a.k.a. HYPERCALLS).
 */

/* EAX = vector; EBX, ECX, EDX, ESI, EDI = args 1, 2, 3, 4, 5. */
#define __HYPERVISOR_set_trap_table        0
#define __HYPERVISOR_mmu_update            1
#define __HYPERVISOR_set_gdt               2
#define __HYPERVISOR_stack_switch          3
#define __HYPERVISOR_set_callbacks         4
#define __HYPERVISOR_fpu_taskswitch        5
#define __HYPERVISOR_sched_op              6
#define __HYPERVISOR_dom0_op               7
#define __HYPERVISOR_set_debugreg          8
#define __HYPERVISOR_get_debugreg          9
#define __HYPERVISOR_update_descriptor    10
#define __HYPERVISOR_set_fast_trap        11
#define __HYPERVISOR_dom_mem_op           12
#define __HYPERVISOR_multicall            13
#define __HYPERVISOR_update_va_mapping    14
#define __HYPERVISOR_set_timer_op         15
#define __HYPERVISOR_event_channel_op     16
#define __HYPERVISOR_xen_version          17
#define __HYPERVISOR_console_io           18
#define __HYPERVISOR_physdev_op           19
#define __HYPERVISOR_grant_table_op       20
#define __HYPERVISOR_vm_assist            21
#define __HYPERVISOR_update_va_mapping_otherdomain 22
#define __HYPERVISOR_switch_vm86          23

/*
 * MULTICALLS
 * 
 * Multicalls are listed in an array, with each element being a fixed size 
 * (BYTES_PER_MULTICALL_ENTRY). Each is of the form (op, arg1, ..., argN)
 * where each element of the tuple is a machine word. 
 */
#define ARGS_PER_MULTICALL_ENTRY 8


/* 
 * VIRTUAL INTERRUPTS
 * 
 * Virtual interrupts that a guest OS may receive from Xen.
 */
#define VIRQ_MISDIRECT  0	/* Catch-all interrupt for unbound VIRQs.      */
#define VIRQ_TIMER      1	/* Timebase update, and/or requested timeout.  */
#define VIRQ_DEBUG      2	/* Request guest to dump debug info.           */
#define VIRQ_CONSOLE    3	/* (DOM0) bytes received on emergency console. */
#define VIRQ_DOM_EXC    4	/* (DOM0) Exceptional event for some domain.   */
#define VIRQ_PARITY_ERR 5	/* (DOM0) NMI parity error.                    */
#define VIRQ_IO_ERR     6	/* (DOM0) NMI I/O error.                       */
#define NR_VIRQS        7

/*
 * MMU-UPDATE REQUESTS
 * 
 * HYPERVISOR_mmu_update() accepts a list of (ptr, val) pairs.
 * ptr[1:0] specifies the appropriate MMU_* command.
 * 
 * FOREIGN DOMAIN (FD)
 * -------------------
 *  Some commands recognise an explicitly-declared foreign domain,
 *  in which case they will operate with respect to the foreigner rather than
 *  the calling domain. Where the FD has some effect, it is described below.
 * 
 * ptr[1:0] == MMU_NORMAL_PT_UPDATE:
 * Updates an entry in a page table. If updating an L1 table, and the new
 * table entry is valid/present, the mapped frame must belong to the FD, if
 * an FD has been specified. If attempting to map an I/O page then the
 * caller assumes the privilege of the FD.
 * FD == DOMID_IO: Permit /only/ I/O mappings, at the priv level of the caller.
 * FD == DOMID_XEN: Map restricted areas of Xen's heap space.
 * ptr[:2]  -- Machine address of the page-table entry to modify.
 * val      -- Value to write.
 * 
 * ptr[1:0] == MMU_MACHPHYS_UPDATE:
 * Updates an entry in the machine->pseudo-physical mapping table.
 * ptr[:2]  -- Machine address within the frame whose mapping to modify.
 *             The frame must belong to the FD, if one is specified.
 * val      -- Value to write into the mapping entry.
 *  
 * ptr[1:0] == MMU_EXTENDED_COMMAND:
 * val[7:0] -- MMUEXT_* command.
 * 
 *   val[7:0] == MMUEXT_(UN)PIN_*_TABLE:
 *   ptr[:2]  -- Machine address of frame to be (un)pinned as a p.t. page.
 *               The frame must belong to the FD, if one is specified.
 * 
 *   val[7:0] == MMUEXT_NEW_BASEPTR:
 *   ptr[:2]  -- Machine address of new page-table base to install in MMU.
 * 
 *   val[7:0] == MMUEXT_TLB_FLUSH:
 *   No additional arguments.
 * 
 *   val[7:0] == MMUEXT_INVLPG:
 *   ptr[:2]  -- Linear address to be flushed from the TLB.
 * 
 *   val[7:0] == MMUEXT_FLUSH_CACHE:
 *   No additional arguments. Writes back and flushes cache contents.
 * 
 *   val[7:0] == MMUEXT_SET_LDT:
 *   ptr[:2]  -- Linear address of LDT base (NB. must be page-aligned).
 *   val[:8]  -- Number of entries in LDT.
 * 
 *   val[7:0] == MMUEXT_TRANSFER_PAGE:
 *   val[31:16] -- Domain to whom page is to be transferred.
 *   (val[15:8],ptr[9:2]) -- 16-bit reference into transferee's grant table.
 *   ptr[:12]  -- Page frame to be reassigned to the FD.
 *                (NB. The frame must currently belong to the calling domain).
 * 
 *   val[7:0] == MMUEXT_SET_FOREIGNDOM:
 *   val[31:16] -- Domain to set as the Foreign Domain (FD).
 *                 (NB. DOMID_SELF is not recognised)
 *                 If FD != DOMID_IO then the caller must be privileged.
 * 
 *   val[7:0] == MMUEXT_CLEAR_FOREIGNDOM:
 *   Clears the FD.
 * 
 *   val[7:0] == MMUEXT_REASSIGN_PAGE:
 *   ptr[:2]  -- A machine address within the page to be reassigned to the FD.
 *               (NB. page must currently belong to the calling domain).
 */
#define MMU_NORMAL_PT_UPDATE     0	/* checked '*ptr = val'. ptr is MA.       */
#define MMU_MACHPHYS_UPDATE      2	/* ptr = MA of frame to modify entry for  */
#define MMU_EXTENDED_COMMAND     3	/* least 8 bits of val demux further      */
#define MMUEXT_PIN_L1_TABLE      0	/* ptr = MA of frame to pin               */
#define MMUEXT_PIN_L2_TABLE      1	/* ptr = MA of frame to pin               */
#define MMUEXT_PIN_L3_TABLE      2	/* ptr = MA of frame to pin               */
#define MMUEXT_PIN_L4_TABLE      3	/* ptr = MA of frame to pin               */
#define MMUEXT_UNPIN_TABLE       4	/* ptr = MA of frame to unpin             */
#define MMUEXT_NEW_BASEPTR       5	/* ptr = MA of new pagetable base         */
#define MMUEXT_TLB_FLUSH         6	/* ptr = NULL                             */
#define MMUEXT_INVLPG            7	/* ptr = VA to invalidate                 */
#define MMUEXT_FLUSH_CACHE       8
#define MMUEXT_SET_LDT           9	/* ptr = VA of table; val = # entries     */
#define MMUEXT_SET_FOREIGNDOM   10	/* val[31:16] = dom                       */
#define MMUEXT_CLEAR_FOREIGNDOM 11
#define MMUEXT_TRANSFER_PAGE    12	/* ptr = MA of frame; val[31:16] = dom    */
#define MMUEXT_REASSIGN_PAGE    13
#define MMUEXT_CMD_MASK        255
#define MMUEXT_CMD_SHIFT         8

/* These are passed as 'flags' to update_va_mapping. They can be ORed. */
#define UVMF_FLUSH_TLB          1	/* Flush entire TLB. */
#define UVMF_INVLPG             2	/* Flush the VA mapping being updated. */


/*
 * Commands to HYPERVISOR_sched_op().
 */
#define SCHEDOP_yield           0	/* Give up the CPU voluntarily.       */
#define SCHEDOP_block           1	/* Block until an event is received.  */
#define SCHEDOP_shutdown        2	/* Stop executing this domain.        */
#define SCHEDOP_cmdmask       255	/* 8-bit command. */
#define SCHEDOP_reasonshift     8	/* 8-bit reason code. (SCHEDOP_shutdown) */

/*
 * Reason codes for SCHEDOP_shutdown. These may be interpreted by control 
 * software to determine the appropriate action. For the most part, Xen does
 * not care about the shutdown code (SHUTDOWN_crash excepted).
 */
#define SHUTDOWN_poweroff   0	/* Domain exited normally. Clean up and kill. */
#define SHUTDOWN_reboot     1	/* Clean up, kill, and then restart.          */
#define SHUTDOWN_suspend    2	/* Clean up, save suspend info, kill.         */
#define SHUTDOWN_crash      3	/* Tell controller we've crashed.             */

/*
 * Commands to HYPERVISOR_console_io().
 */
#define CONSOLEIO_write         0
#define CONSOLEIO_read          1

/*
 * Commands to HYPERVISOR_dom_mem_op().
 */
#define MEMOP_increase_reservation 0
#define MEMOP_decrease_reservation 1

/*
 * Commands to HYPERVISOR_vm_assist().
 */
#define VMASST_CMD_enable                0
#define VMASST_CMD_disable               1
#define VMASST_TYPE_4gb_segments         0
#define VMASST_TYPE_4gb_segments_notify  1
#define VMASST_TYPE_writable_pagetables  2
#define MAX_VMASST_TYPE 2

/*
 * everything from here is 32 bit specific
 */

#ifndef PACKED
/* GCC-specific way to pack structure definitions (no implicit padding). */
/*#define PACKED __attribute__ ((packed))*/
#define PACKED
#endif

/*
 * Pointers and other address fields inside interface structures are padded to
 * 64 bits. This means that field alignments aren't different between 32- and
 * 64-bit architectures. 
 */
/* NB. Multi-level macro ensures __LINE__ is expanded before concatenation. */
#define __MEMORY_PADDING(_X) u32_t __pad_ ## _X
#define _MEMORY_PADDING(_X)  __MEMORY_PADDING(_X)
#define MEMORY_PADDING       _MEMORY_PADDING(__LINE__)

/*
 * SEGMENT DESCRIPTOR TABLES
 */
/*
 * A number of GDT entries are reserved by Xen. These are not situated at the
 * start of the GDT because some stupid OSes export hard-coded selector values
 * in their ABI. These hard-coded values are always near the start of the GDT,
 * so Xen places itself out of the way.
 * 
 * NB. The reserved range is inclusive (that is, both FIRST_RESERVED_GDT_ENTRY
 * and LAST_RESERVED_GDT_ENTRY are reserved).
 */
#define NR_RESERVED_GDT_ENTRIES    40
#define FIRST_RESERVED_GDT_ENTRY   256
#define LAST_RESERVED_GDT_ENTRY					\
  (FIRST_RESERVED_GDT_ENTRY + NR_RESERVED_GDT_ENTRIES - 1)
#define FIRST_AVAILABLE_GDT_ENTRY   LAST_RESERVED_GDT_ENTRY+1

/*
 * These flat segments are in the Xen-private section of every GDT. Since these
 * are also present in the initial GDT, many OSes will be able to avoid
 * installing their own GDT.
 */
#define FLAT_RING1_CS 0x0819	/* GDT index 259 */
#define FLAT_RING1_DS 0x0821	/* GDT index 260 */
#define FLAT_RING3_CS 0x082b	/* GDT index 261 */
#define FLAT_RING3_DS 0x0833	/* GDT index 262 */

#define FLAT_GUESTOS_CS FLAT_RING1_CS
#define FLAT_GUESTOS_DS FLAT_RING1_DS
#define FLAT_USER_CS    FLAT_RING3_CS
#define FLAT_USER_DS    FLAT_RING3_DS

/* And the trap vector is... */
#define TRAP_INSTR "int 0x82"

/*
 * Virtual addresses beyond this are not modifiable by guest OSes. The 
 * machine->physical mapping table starts at this address, read-only.
 */
#define HYPERVISOR_VIRT_START (0xFC000000UL)
#ifndef machine_to_phys_mapping
#define machine_to_phys_mapping ((unsigned long *)HYPERVISOR_VIRT_START)
#endif

#define XEN_PROXY_VECTOR  34
#define XEN_TRAP_VECTOR 0x82

#define PAGE_SIZE  4096
#define PAGE_SHIFT 12

/*
  The desired stack pointer must be in reg
  before this macro is used
*/
#define HYPERVISOR_STACK_SWITCH(reg)		\
  mov eax, __HYPERVISOR_stack_switch		\
    mov ebx, ss					\
    mov ecx, esp				\
    int XEN_TRAP_VECTOR

#define HYPERVISOR_STACK_SWITCH_W_SP(reg)	\
  mov ecx, reg                       ;		\
  mov eax, __HYPERVISOR_stack_switch ;		\
  mov ebx, ss                        ;		\
  int XEN_TRAP_VECTOR

#define EVTCHN_UPCALL_PENDING_OFFSET	/* 0 */
#define EVTCHN_UPCALL_MASK_OFFSET          1


#define XEN_GET_VCPU_INFO(reg)  mov reg, (_hypervisor_shared_info)
#define XEN_BLOCK_EVENTS(reg)   movb EVTCHN_UPCALL_MASK_OFFSET(reg), 1
#define XEN_UNBLOCK_EVENTS(reg)	movb EVTCHN_UPCALL_MASK_OFFSET(reg), 0
#define XEN_TEST_PENDING(reg)   test EVTCHN_UPCALL_PENDING_OFFSET(reg), 0xFF

#endif				/* XENASM_H */
