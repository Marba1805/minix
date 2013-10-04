/* This file contains a simple exception handler.  Exceptions in user
 * processes are converted to signals. Exceptions in a kernel task cause
 * a panic.
 */

#include "kernel.h"
#include <signal.h>
#include "proc.h"

extern int in_cif_interrupt;

/*===========================================================================*
 *				exception				     *
 *===========================================================================*/
PUBLIC void exception(vec_nr, errno)
unsigned vec_nr;
unsigned errno;
{
/* An exception or unexpected interrupt has occurred. */

  struct ex_s {
	char *msg;
	int signum;
	int minprocessor;
  };
  static struct ex_s ex_data[] = {
	{ "Divide error", SIGFPE, 86 },
	{ "Debug exception", SIGTRAP, 86 },
	{ "Nonmaskable interrupt", SIGBUS, 86 },
	{ "Breakpoint", SIGEMT, 86 },
	{ "Overflow", SIGFPE, 86 },
	{ "Bounds check", SIGFPE, 186 },
	{ "Invalid opcode", SIGILL, 186 },
	{ "Coprocessor not available", SIGFPE, 186 },
	{ "Double fault", SIGBUS, 286 },
	{ "Copressor segment overrun", SIGSEGV, 286 },
	{ "Invalid TSS", SIGSEGV, 286 },
	{ "Segment not present", SIGSEGV, 286 },
	{ "Stack exception", SIGSEGV, 286 },	/* STACK_FAULT already used */
	{ "General protection", SIGSEGV, 286 },
	{ "Page fault", SIGSEGV, 386 },		/* not close */
	{ NIL_PTR, SIGILL, 0 },			/* probably software trap */
	{ "Coprocessor error", SIGFPE, 386 },
  };
  register struct ex_s *ep;
  struct proc *saved_proc;

  /* Save proc_ptr, because it may be changed by debug statements. */
  saved_proc = proc_ptr;	

  xen_kprintf("Exception: %x   proc_addr: %x\n", vec_nr, saved_proc);
  xen_kprintf("pc = %x:0x%x\n", (unsigned) saved_proc->p_reg.cs,
	      (unsigned) saved_proc->p_reg.pc);
  xen_kprintf("ss = %x\n", saved_proc->p_reg.ss);
  xen_kprintf("gs = %x\n", saved_proc->p_reg.gs);
  xen_kprintf("fs = %x\n", saved_proc->p_reg.fs);
  xen_kprintf("es = %x\n", saved_proc->p_reg.es);
  xen_kprintf("ds = %x\n", saved_proc->p_reg.ds);
  xen_kprintf("di = %x\n", saved_proc->p_reg.di);
  xen_kprintf("si = %x\n", saved_proc->p_reg.si);
  xen_kprintf("fp = %x\n", saved_proc->p_reg.fp);
  xen_kprintf("st = %x\n", saved_proc->p_reg.st);
  xen_kprintf("bx = %x\n", saved_proc->p_reg.bx);
  xen_kprintf("dx = %x\n", saved_proc->p_reg.dx);
  xen_kprintf("cx = %x\n", saved_proc->p_reg.cx);
  xen_kprintf("ax = %x\n", saved_proc->p_reg.retreg);
  xen_kprintf("k_callback %x\n", k_callback);
  xen_kprintf("in_cif_interrupt %x\n", in_cif_interrupt);

  ep = &ex_data[vec_nr];

  if (vec_nr == 2) {	/* spurious NMI on some machines */
    xen_kprintf("got spurious NMI\n");
    return;
  }

  /* If an exception occurs while running a process, the k_reenter variable 
   * will be zero. Exceptions in interrupt handlers or system traps will make 
   * k_reenter larger than zero.
   */
  if (k_reenter == 0 && ! iskernelp(saved_proc)) {
	cause_sig(proc_nr(saved_proc), ep->signum);
	return;
  }

  /* Exception in system code. This is not supposed to happen. */
  if (ep->msg == NIL_PTR || machine.processor < ep->minprocessor)
    xen_kprintf("\nIntel-reserved exception %d\n", vec_nr);
  else
    xen_kprintf("\n%s\n", ep->msg);
  xen_kprintf("k_reenter = %d ", k_reenter);
  xen_kprintf("process %d (%s), ", proc_nr(saved_proc),
	      saved_proc->p_name);
  xen_kprintf("pc = %u:0x%x", (unsigned) saved_proc->p_reg.cs,
	      (unsigned) saved_proc->p_reg.pc);

  panic("exception in a kernel task", NO_NUM);
}
