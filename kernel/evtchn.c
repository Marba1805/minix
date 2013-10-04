>>/**
   * evtchn.c
   *
   * Event channel controls virtual interrupts from xen.
   * Virtual interrupts take the form of Physical Interrupts (not used),
   * virtual interrupts and interdomain communication.
   *
   * Code snippets taken from Linux, FreeBSD and NetBSD implementations
   *
   * Copyright (c) 2006, I Kelly
   */
#include "kernel.h"
#include "protect.h"
#include <xen/evtchn.h>
#include <xen/ctrl_if.h>

PUBLIC char k_callback = 0;

PRIVATE unsigned int evtchn_to_irq[NR_EVENT_CHANNELS];
PRIVATE unsigned int virq_to_irq[NR_VIRQS];
PRIVATE unsigned int irq_to_evtchn[NR_IRQS];
PRIVATE unsigned int irq_bindcount[NR_IRQS];
PRIVATE irq_handler_t handlers[NR_IRQS];

FORWARD _PROTOTYPE(void misdirect_handler, (unsigned int irq, struct stackframe_s *regs));
FORWARD _PROTOTYPE(unsigned int find_free_irq, (void));
FORWARD _PROTOTYPE(void enable_irq, (unsigned int irq));
FORWARD _PROTOTYPE(void disable_irq, (unsigned int irq));
FORWARD _PROTOTYPE(void ack_irq, (unsigned int irq));
FORWARD _PROTOTYPE(void end_irq, (unsigned int irq));

/*
 * xen interrupt descriptor table
 * (interrupt vector, privilege ring, CS:EIP of handler)
 */
PRIVATE trap_info_t trap_table[] = {
  {DIVIDE_VECTOR, 0, CS_SELECTOR, (unsigned long) divide_error},
  {DEBUG_VECTOR, 0, CS_SELECTOR, (unsigned long) single_step_exception},
  {NMI_VECTOR, 0, CS_SELECTOR, (unsigned long) nmi},
  {BREAKPOINT_VECTOR, 3, CS_SELECTOR, (unsigned long) breakpoint_exception},
  {OVERFLOW_VECTOR, 3, CS_SELECTOR, (unsigned long) overflow},
  {BOUNDS_VECTOR, 0, CS_SELECTOR, (unsigned long) bounds_check},
  {INVAL_OP_VECTOR, 0, CS_SELECTOR, (unsigned long) inval_opcode},
  {COPROC_NOT_VECTOR, 0, CS_SELECTOR, (unsigned long) copr_not_available},
  {DOUBLE_FAULT_VECTOR, 0, CS_SELECTOR, (unsigned long) double_fault},
  {COPROC_SEG_VECTOR, 0, CS_SELECTOR, (unsigned long) copr_seg_overrun},
  {INVAL_TSS_VECTOR, 0, CS_SELECTOR, (unsigned long) inval_tss},
  {SEG_NOT_VECTOR, 0, CS_SELECTOR, (unsigned long) segment_not_present},
  {STACK_FAULT_VECTOR, 0, CS_SELECTOR, (unsigned long) stack_exception},
  {PROTECTION_VECTOR, 0, CS_SELECTOR, (unsigned long) general_protection},
  {PAGE_FAULT_VECTOR, 0, CS_SELECTOR, (unsigned long) page_fault},
  {COPROC_ERR_VECTOR, 0, CS_SELECTOR, (unsigned long) copr_error},
  {SYS386_VECTOR, 3, CS_SELECTOR, (unsigned long) s_call},
  {XEN_PROXY_VECTOR, 3, CS_SELECTOR, (unsigned long) xen_proxy},
};

/**
 * Do nothing
 */
PRIVATE void misdirect_handler(irq, regs)
     unsigned int irq;
     struct stackframe_s *regs;
{
  /* nothing */
  return;
}

/**
 * Add a handler for a virtual interrupt
 */
PUBLIC unsigned int add_irq_handler(irq, handler)
     unsigned int irq;
     void (*handler)(unsigned int, struct stackframe_s *);
{
  if (handlers[irq].handler) {
    xen_kprintf("Event %x already handled by %x\n", irq,
		handlers[irq].handler);
    return 0;
  }

  handlers[irq].handler = handler;
  handlers[irq].status = EV_DISABLED;

  return irq;
}

/**
 * Clear a handler for an interrupt. If the interrupt occurs again,
 * nothing will happen.
 */
PUBLIC void clear_irq_handler(irq)
     unsigned int irq;
{
  handlers[irq].handler = NULL;
  handlers[irq].status = EV_UNINITIALISED;
}

PUBLIC unsigned int enable_irq_handler(irq)
     unsigned int irq;
{
  if (handlers[irq].status == EV_UNINITIALISED) {
    xen_kprintf("Action [%x] is uninitialied\n", irq);
    return 0;
  }
  handlers[irq].status = EV_ENABLED;

  enable_irq(irq);

  return 1;
}

PUBLIC unsigned int disable_irq_handler(irq)
     unsigned int irq;
{
  if (handlers[irq].status == EV_UNINITIALISED) {
    xen_kprintf("Action [%x] is uninitialied\n", irq);
    return 0;
  }
  handlers[irq].status = EV_DISABLED;

  disable_irq(irq);

  return 1;
}

/**
 * Initialise the events interface.
 * - Empty all the interrupt handlers
 * - Block every event channel
 * - Set the callback functions, and trap table
 * - Setup the misdirect interrupt
 */
PUBLIC void init_events()
{
  int i = 0;

  for (i = 0; i < NR_IRQS; i++) {
    handlers[i].handler = NULL;
    handlers[i].status = EV_UNINITIALISED;

    irq_bindcount[i] = 0;
    irq_to_evtchn[i] = -1;
  }

  for (i = 0; i < NR_EVENT_CHANNELS; i++) {
    evtchn_to_irq[i] = -1;
    x86_atomic_set_bit(i,
		       &hypervisor_shared_info->
		       evtchn_mask[0]);
    x86_atomic_clear_bit(i,
			 &hypervisor_shared_info->
			 evtchn_pending[0]);
  }

  for (i = 0; i < NR_VIRQS; i++) {
    virq_to_irq[i] = -1;
  }

  hypervisor_set_callbacks(CS_SELECTOR,
			   (unsigned long) hypervisor_callback,
			   CS_SELECTOR,
			   (unsigned long) failsafe_callback);

  hypervisor_set_trap_table(trap_table);

#if 0
  i = bind_virq_to_irq(VIRQ_MISDIRECT);
  add_irq_handler(i, misdirect_handler);
  enable_irq_handler(VIRQ_MISDIRECT);
#endif
}

/**
 * Find the first free interrupt vector
 */
PRIVATE unsigned int find_free_irq()
{
  int i;

  for (i = 0; i < NR_IRQS; i++) {
    if (irq_to_evtchn[i] == -1) {
      return i;
    }
  }

  panic
    ("No free IRQ found to bind device. You have too many devices.",
     i);
  return 0;		/* shouldn't reach here */
}

PUBLIC unsigned int bind_virq_to_irq(virq)
     unsigned int virq;
{
  unsigned int irq, evtchn;
  evtchn_op_t op;

  if ((irq = virq_to_irq[virq]) == -1) {
    op.cmd = EVTCHNOP_bind_virq;
    op.u.bind_virq.virq = virq;
    if (hypervisor_event_channel_op(&op) != 0) {
      panic("Error binding VIRQ %x\n", irq);
      return 0;
    }
    evtchn = op.u.bind_virq.port;
    irq = find_free_irq();
    virq_to_irq[virq] = irq;
    evtchn_to_irq[evtchn] = irq;
    irq_to_evtchn[irq] = evtchn;
  }
  irq_bindcount[irq]++;

  return irq;
}

PUBLIC void unbind_virq_from_irq(virq)
     unsigned int virq;
{
  evtchn_op_t op;
  int irq = virq_to_irq[irq];
  int evtchn = irq_to_evtchn[irq];

  if (--irq_bindcount[irq] == 0) {
    op.cmd = EVTCHNOP_close;
    op.u.close.dom = DOMID_SELF;
    op.u.close.port = evtchn;
    if (hypervisor_event_channel_op(&op) != 0) {
      panic("Failed to unbind VIRQ %x", virq);
    }

    virq_to_irq[virq] = -1;
    evtchn_to_irq[evtchn] = -1;
    irq_to_evtchn[irq] = -1;
  }
}

PUBLIC unsigned int bind_evtchn_to_irq(evtchn)
     unsigned int evtchn;
{
  int irq;

  if ((irq = evtchn_to_irq[evtchn]) == -1) {
    irq = find_free_irq();
    evtchn_to_irq[evtchn] = irq;
    irq_to_evtchn[irq] = evtchn;
  }

  irq_bindcount[irq]++;

  return irq;
}

PUBLIC void unbind_evtchn_from_irq(evtchn)
     unsigned int evtchn;
{
  int irq = evtchn_to_irq[evtchn];

  if (--irq_bindcount[irq] == 0) {
    irq_to_evtchn[irq] = -1;
    evtchn_to_irq[evtchn] = -1;
  }
}

PUBLIC unsigned int get_irq_from_virq(irq)
     unsigned int irq;
{
  return virq_to_irq[irq];
}

PUBLIC unsigned int get_irq_from_evtchn(evtchn)
     unsigned int evtchn;
{
  return evtchn_to_irq[evtchn];
}

/**
 * Notify the event channel that a message has been sent.
 */
PUBLIC void notify_evtchn(unsigned int evtchn)
{
  evtchn_op_t op;

  op.cmd = EVTCHNOP_send;
  op.u.send.local_port = evtchn;
  hypervisor_event_channel_op(&op);
}

PRIVATE void enable_irq(unsigned int irq)
{
  int evtchn = irq_to_evtchn[irq];

  if (evtchn == -1) {
    return;
  }

  x86_atomic_clear_bit(evtchn,
		       &hypervisor_shared_info->evtchn_mask[0]);

  if (x86_atomic_test_bit
      (evtchn, &hypervisor_shared_info->evtchn_pending[0])
      && !x86_atomic_test_and_set_bit(evtchn >> 5,
				      &hypervisor_shared_info->evtchn_pending_sel)) {
    hypervisor_shared_info->vcpu_data[0].
      evtchn_upcall_pending = 1;
    if (!hypervisor_shared_info->vcpu_data[0].
	evtchn_upcall_mask)
      force_evtchn_callback();
  }
}

PRIVATE void disable_irq(unsigned int irq)
{
  int evtchn = irq_to_evtchn[irq];

  if (evtchn == -1) {
    return;
  }

  x86_atomic_set_bit(evtchn,
		     &hypervisor_shared_info->evtchn_mask[0]);
}

PRIVATE void ack_irq(unsigned int irq)
{
  int evtchn = irq_to_evtchn[irq];

  if (evtchn == -1) {
    return;
  }

  disable_irq(irq);
  x86_atomic_clear_bit(evtchn,
		       &hypervisor_shared_info->evtchn_pending[0]);
}

PRIVATE void end_irq(unsigned int irq)
{
  int evtchn = irq_to_evtchn[irq];

  if (evtchn == -1 || handlers[irq].status != EV_ENABLED) {
    return;
  }

  enable_irq(irq);
}

/**
 * Force a hypervisor_callback
 */
PUBLIC void force_evtchn_callback()
{
  hypervisor_xen_version();
}

/**
 * Unblock hypervisor_callbacks
 * Force a callback to clear out the back log
 */
PUBLIC void enable_evtchn_callbacks()
{
  int i = 0;

  if (k_callback) {
    /*		return;*/
    panic("Enabling inside an interrupt\n", 0);
  }
  /*kprintf("* unblock evtchn callbacks *\n"); */
  x86_barrier();
  hypervisor_shared_info->vcpu_data[0].evtchn_upcall_mask = 0;

  if (hypervisor_shared_info->vcpu_data[0].evtchn_upcall_pending) {
    x86_barrier();
    force_evtchn_callback();
  }
}

/**
 * Unblock hypervisor_callbacks but save their state in flags first
 */
PUBLIC void enable_evtchn_callbacks_and_save(u8_t * flags)
{
  int i = 0;

  if (k_callback) {
    /*		return;*/
    panic("Enabling and saving inside an interrupt\n", 0);
  }
		
  /*	kprintf("* unblock and save evtchn callbacks *\n");*/
  x86_barrier();
  *flags = hypervisor_shared_info->vcpu_data[0].evtchn_upcall_mask;
  hypervisor_shared_info->vcpu_data[0].evtchn_upcall_mask = 0;

  /*	clear_callbacks();*/

  if (hypervisor_shared_info->vcpu_data[0].evtchn_upcall_pending) {
    x86_barrier();
    force_evtchn_callback();
  }

}

/**
 * Block hypervisor_callbacks
 */
PUBLIC void disable_evtchn_callbacks()
{

  /*kprintf("* block evtchn callbacks *\n"); */
  hypervisor_shared_info->vcpu_data[0].evtchn_upcall_mask = 1;
}

/**
 * Block hypervisor_callbacks, but save their state in flags first
 */
PUBLIC void disable_evtchn_callbacks_and_save(u8_t * flags)
{
  *flags = hypervisor_shared_info->vcpu_data[0].evtchn_upcall_mask;
  hypervisor_shared_info->vcpu_data[0].evtchn_upcall_mask = 1;
  x86_barrier();
}

/**
 * Restore flags from a previous disable or enable operation.
 * Force a callback to flush pending requests, if callbacks have been reenabled
 */
PUBLIC void restore_flags(u8_t flags)
{
  int i = 0;
	
  if (k_callback) {
    /*		return;*/
    panic("Restoring inside an interrupt\n", 0);
  }

  x86_barrier();
  hypervisor_shared_info->vcpu_data[0].evtchn_upcall_mask = flags;

  if (hypervisor_shared_info->vcpu_data[0].evtchn_upcall_pending) {
    x86_barrier();
    force_evtchn_callback();
  }
}

/**
 * Do a hypervisor callback. Decide what interrupts have occured and run their handlers
 */
PUBLIC void do_hypervisor_callback(struct stackframe_s *regs)
{
  unsigned long l1, l2;
  u8_t flags;
  unsigned int l1i, l2i, evtchn;
  int irq;
  shared_info_t *s = (shared_info_t*)hypervisor_shared_info;

  while (s->vcpu_data[0].evtchn_upcall_pending) {
    s->vcpu_data[0].evtchn_upcall_pending = 0;

    l1 = x86_atomic_xchg(&s->evtchn_pending_sel, 0);

    while (l1 != 0) {
      l1i = x86_scan_forward(l1);
      l1 &= ~(1 << l1i);

      l2 = s->evtchn_pending[l1i] & ~s->evtchn_mask[l1i];

      while (l2 != 0) {
	l2i = x86_scan_forward(l2);
	l2 &= ~(1 << l2i);

	evtchn = (l1i << 5) + l2i;

	if ((irq = evtchn_to_irq[evtchn]) != -1) {

	  ack_irq(irq);

	  if (handlers[irq].status ==
	      EV_ENABLED
	      && handlers[irq].handler) {
	    handlers[irq].handler(irq,
				  regs);
	  }

	  end_irq(irq);

	}
      }
    }
  }
}

PUBLIC void print_evtchn_info()
{
  shared_info_t *s = hypervisor_shared_info;
  xen_kprintf("=== important values ===\n");
  xen_kprintf("*** evtchn_upcall_mask: %x\n",
	      s->vcpu_data[0].evtchn_upcall_mask);
  xen_kprintf("*** evtchn_upcall_pending: %x\n",
	      s->vcpu_data[0].evtchn_upcall_pending);
  xen_kprintf("*** evtchn_mask[0]: %x\n", s->evtchn_mask[0]);
  xen_kprintf("*** evtchn_pending_sel: %x\n", s->evtchn_pending_sel);
  xen_kprintf("*** evtchn_pending[0]: %x\n", s->evtchn_pending[0]);
}
