/* Function prototypes. */

#ifndef PROTO_H
#define PROTO_H

#include <xen/xen.h>
#include <xen/evtchn.h>

/* Struct declarations. */
struct proc;
struct timer;
struct stackframe_s;
struct segdesc_s;

/* clock.c */
_PROTOTYPE(void clock_task, (void));
_PROTOTYPE(void clock_stop, (void));
_PROTOTYPE(clock_t get_uptime, (void));
/*_PROTOTYPE( unsigned long read_clock, (void)				);*/
_PROTOTYPE(void set_timer, (struct timer * tp, clock_t t, tmr_func_t f));
_PROTOTYPE(void reset_timer, (struct timer * tp));

/* main.c */
_PROTOTYPE( void main, (void)						);
_PROTOTYPE( void prepare_shutdown, (int how)				);

/* utility.c */
_PROTOTYPE(void xen_kprintf, (const char *fmt, ...));
_PROTOTYPE(void kprintf, (const char *fmt, ...));
_PROTOTYPE(void panic, (_CONST char *s, int n));

/* proc.c */
_PROTOTYPE( int sys_call, (int function, int src_dest, message *m_ptr)	);
_PROTOTYPE( int lock_notify, (int src, int dst)				);
_PROTOTYPE( int lock_send, (int dst, message *m_ptr)			);
_PROTOTYPE( void lock_enqueue, (struct proc *rp)			);
_PROTOTYPE( void lock_dequeue, (struct proc *rp)			);

/* start.c */
_PROTOTYPE(void xen_cstart, (minix_start_info_t *si));


/* system.c */
_PROTOTYPE( int get_priv, (register struct proc *rc, int proc_type)	);
_PROTOTYPE( void send_sig, (int proc_nr, int sig_nr)			);
_PROTOTYPE( void cause_sig, (int proc_nr, int sig_nr)			);
_PROTOTYPE( void sys_task, (void)					);
_PROTOTYPE( void get_randomness, (int source)				);
_PROTOTYPE( int virtual_copy, (struct vir_addr *src, struct vir_addr *dst, 
				vir_bytes bytes) 			);
#define numap_local(proc_nr, vir_addr, bytes) \
	umap_local(proc_addr(proc_nr), D, (vir_addr), (bytes))
_PROTOTYPE( phys_bytes umap_local, (struct proc *rp, int seg, 
		vir_bytes vir_addr, vir_bytes bytes)			);
_PROTOTYPE( phys_bytes umap_remote, (struct proc *rp, int seg, 
		vir_bytes vir_addr, vir_bytes bytes)			);
_PROTOTYPE( phys_bytes umap_bios, (struct proc *rp, vir_bytes vir_addr,
		vir_bytes bytes)					);

#if (CHIP == INTEL)

/* exception.c */
_PROTOTYPE(void exception, (unsigned vec_nr, unsigned errno));

/* i8259.c */
/*_PROTOTYPE( void intr_init, (int mine)					);
_PROTOTYPE( void intr_handle, (irq_hook_t *hook)			);
_PROTOTYPE( void put_irq_handler, (irq_hook_t *hook, int irq,
						irq_handler_t handler)	);
_PROTOTYPE( void rm_irq_handler, (irq_hook_t *hook)			);
*/
/* klib*.s */
_PROTOTYPE(void cp_mess,
	   (int src, phys_clicks src_clicks, vir_bytes src_offset,
	    phys_clicks dst_clicks, vir_bytes dst_offset));
_PROTOTYPE(u16_t mem_rdw, (U16_t segm, vir_bytes offset));
_PROTOTYPE(void phys_copy, (phys_bytes source, phys_bytes dest,
			    phys_bytes count));
_PROTOTYPE(void phys_memset, (phys_bytes source, unsigned long pattern,
			      phys_bytes count));
_PROTOTYPE(void read_tsc, (unsigned long *high, unsigned long *low));
_PROTOTYPE(unsigned long read_cpu_flags, (void));
_PROTOTYPE(void xen_proxy, (void));
_PROTOTYPE(void xen_proxy_int, (void));
_PROTOTYPE(int xen_op, (int vector, ...));
_PROTOTYPE(void hypervisor_callback, (void));
_PROTOTYPE(void failsafe_callback, (void));
_PROTOTYPE(int current_ring, (void));
_PROTOTYPE(unsigned long x86_atomic_xchg,
	   (unsigned long *, unsigned long));
_PROTOTYPE(void x86_atomic_clear_bit, (int, unsigned long *));
_PROTOTYPE(int x86_atomic_test_bit, (int, unsigned long *));
_PROTOTYPE(void x86_atomic_set_bit, (int, unsigned long *));
_PROTOTYPE(int x86_atomic_test_and_set_bit, (int, unsigned long *));
_PROTOTYPE(unsigned long x86_scan_forward, (unsigned long));
_PROTOTYPE(void x86_barrier, (void));

/* mpx*.s */
_PROTOTYPE(void xen_console_write, (char *str, int count));
_PROTOTYPE(void idle_task, (void));
_PROTOTYPE(void restart, (void));

/* The following are never called from C (pure asm procs). */

/* Exception handlers (real or protected mode), in numerical order. */
void _PROTOTYPE( int00, (void) ), _PROTOTYPE( divide_error, (void) );
void _PROTOTYPE( int01, (void) ), _PROTOTYPE( single_step_exception, (void) );
void _PROTOTYPE( int02, (void) ), _PROTOTYPE( nmi, (void) );
void _PROTOTYPE( int03, (void) ), _PROTOTYPE( breakpoint_exception, (void) );
void _PROTOTYPE( int04, (void) ), _PROTOTYPE( overflow, (void) );
void _PROTOTYPE( int05, (void) ), _PROTOTYPE( bounds_check, (void) );
void _PROTOTYPE( int06, (void) ), _PROTOTYPE( inval_opcode, (void) );
void _PROTOTYPE( int07, (void) ), _PROTOTYPE( copr_not_available, (void) );
void				  _PROTOTYPE( double_fault, (void) );
void				  _PROTOTYPE( copr_seg_overrun, (void) );
void				  _PROTOTYPE( inval_tss, (void) );
void				  _PROTOTYPE( segment_not_present, (void) );
void				  _PROTOTYPE( stack_exception, (void) );
void				  _PROTOTYPE( general_protection, (void) );
void				  _PROTOTYPE( page_fault, (void) );
void				  _PROTOTYPE( copr_error, (void) );

/* Hardware interrupt handlers. */
_PROTOTYPE( void hwint00, (void) );
_PROTOTYPE( void hwint01, (void) );
_PROTOTYPE( void hwint02, (void) );
_PROTOTYPE( void hwint03, (void) );
_PROTOTYPE( void hwint04, (void) );
_PROTOTYPE( void hwint05, (void) );
_PROTOTYPE( void hwint06, (void) );
_PROTOTYPE( void hwint07, (void) );
_PROTOTYPE( void hwint08, (void) );
_PROTOTYPE( void hwint09, (void) );
_PROTOTYPE( void hwint10, (void) );
_PROTOTYPE( void hwint11, (void) );
_PROTOTYPE( void hwint12, (void) );
_PROTOTYPE( void hwint13, (void) );
_PROTOTYPE( void hwint14, (void) );
_PROTOTYPE( void hwint15, (void) );

/* Software interrupt handlers, in numerical order. */
_PROTOTYPE(void trp, (void));
_PROTOTYPE(void s_call, (void)), _PROTOTYPE(p_s_call, (void));
_PROTOTYPE(void test_call, (void));

/* protect.c */
_PROTOTYPE(void xen_prot_init, (void));
_PROTOTYPE(void init_codeseg, (unsigned long index, phys_bytes base,
			       vir_bytes size, int privilege));
_PROTOTYPE(void init_dataseg, (unsigned long index, phys_bytes base,
			       vir_bytes size, int privilege));
_PROTOTYPE(phys_bytes seg2phys, (U16_t seg));
_PROTOTYPE(void phys2seg, (u16_t * seg, vir_bytes * off, phys_bytes phys));
/*_PROTOTYPE(void enable_iop, (struct proc * pp));*/
_PROTOTYPE(void alloc_segments, (struct proc * rp));
_PROTOTYPE(void dump_gdt, (unsigned long offset, unsigned long nr_entries));

/* evtchn.c */
_PROTOTYPE(unsigned int add_irq_handler,
	   (unsigned int,
	    void (*handler) (unsigned int, struct stackframe_s *)));
_PROTOTYPE(void clear_irq_handler, (unsigned int irq));
_PROTOTYPE(unsigned int enable_irq_handler, (unsigned int irq));
_PROTOTYPE(unsigned int disable_irq_handler, (unsigned int irq));
_PROTOTYPE(void init_events, (void));
_PROTOTYPE(unsigned int bind_virq_to_irq, (unsigned int virq));
_PROTOTYPE(void unbind_virq_from_irq, (unsigned int virq));
_PROTOTYPE(unsigned int bind_evtchn_to_irq, (unsigned int evtchn));
_PROTOTYPE(void unbind_evtchn_from_irq, (unsigned int evtchn));
_PROTOTYPE(unsigned int get_irq_from_virq, (unsigned int irq));
_PROTOTYPE(unsigned int get_irq_from_evtchn, (unsigned int evtchn));
_PROTOTYPE(void notify_evtchn, (unsigned int evtchn));
_PROTOTYPE(void force_evtchn_callback, (void));
_PROTOTYPE(void enable_evtchn_callbacks, (void));
_PROTOTYPE(void enable_evtchn_callbacks_and_save, (u8_t * flags));
_PROTOTYPE(void disable_evtchn_callbacks, (void));
_PROTOTYPE(void disable_evtchn_callbacks_and_save, (u8_t * flags));
_PROTOTYPE(void restore_flags, (u8_t flags));
_PROTOTYPE(void do_hypervisor_callback, (struct stackframe_s * regs));
_PROTOTYPE(void print_evtchn_info, (void));

/* ctrl_if.c */
_PROTOTYPE(void ctrl_if_task, (void));

/* xen.c */
_PROTOTYPE(void do_xen_proxy_op, (void));
_PROTOTYPE(unsigned long xen_init_gdt, (minix_start_info_t *msi));
_PROTOTYPE(int hypervisor_console_write, (char *string, int length));
_PROTOTYPE(int hypervisor_set_gdt, (unsigned long *framelist, int entries));
_PROTOTYPE(int hypervisor_update_descriptor, (unsigned long index, struct segdesc_s *segdp));
_PROTOTYPE(int hypervisor_set_trap_table, (trap_info_t * traps));
_PROTOTYPE(int hypervisor_set_callbacks, (unsigned long event_selector,
					  unsigned long event_address,
					  unsigned long failsafe_selector,
					  unsigned long failsafe_address));
_PROTOTYPE(int hypervisor_event_channel_op, (evtchn_op_t * t));
_PROTOTYPE(int hypervisor_xen_version, (void));
_PROTOTYPE(int hypervisor_shutdown, (void));
_PROTOTYPE(int hypervisor_yield, (void));
_PROTOTYPE(void xen_debug_putc, (char c));

#endif				/* (CHIP == INTEL) */

#if (CHIP == M68000)
/* M68000 specific prototypes go here. */
#endif /* (CHIP == M68000) */

#endif /* PROTO_H */
