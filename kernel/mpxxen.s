# 
! This file, mpx386.s, is included by mpx.s when Minix is compiled for 
! 32-bit Intel CPUs. The alternative mpx88.s is compiled for 16-bit CPUs.

! This file is part of the lowest layer of the MINIX kernel.  (The other part
! is "proc.c".)  The lowest layer does process switching and message handling.
! Furthermore it contains the assembler startup code for Minix and the 32-bit
! interrupt handlers.  It cooperates with the code in "start.c" to set up a 
! good environment for main().

! Every transition to the kernel goes through this file.  Transitions to the 
! kernel may be nested.  The initial entry may be with a system call (i.e., 
! send or receive a message), an exception or a hardware interrupt;  kernel 
! reentries may only be made by hardware interrupts.  The count of reentries 
! is kept in "k_reenter". It is important for deciding whether to switch to 
! the kernel stack and for protecting the message passing code in "proc.c".

! For the message passing trap, most of the machine state is saved in the
! proc table.  (Some of the registers need not be saved.)  Then the stack is
! switched to "k_stack", and interrupts are reenabled.  Finally, the system
! call handler (in C) is called.  When it returns, interrupts are disabled
! again and the code falls into the restart routine, to finish off held-up
! interrupts and run the process or task whose pointer is in "proc_ptr".

! Hardware interrupt handlers do the same, except  (1) The entire state must
! be saved.  (2) There are too many handlers to do this inline, so the save
! routine is called.  A few cycles are saved by pushing the address of the
! appropiate restart routine for a return later.  (3) A stack switch is
! avoided when the stack is already switched.  (4) The (master) 8259 interrupt
! controller is reenabled centrally in save().  (5) Each interrupt handler
! masks its interrupt line using the 8259 before enabling (other unmasked)
! interrupts, and unmasks it after servicing the interrupt.  This limits the
! nest level to the number of lines and protects the handler from itself.

! For communication with the boot monitor at startup time some constant
! data are compiled into the beginning of the text segment. This facilitates 
! reading the data at the start of the boot process, since only the first
! sector of the file needs to be read.

! Some data storage is also allocated at the end of this file. This data 
! will be at the start of the data segment of the kernel and will be read
! and modified by the boot monitor before the kernel starts.
!
! Xen modifications - Copyright (c) 2006, I Kelly
!
! sections

.sect .text
begtext:
.sect .rom
begrom:
.sect .data
begdata:
.sect .bss
begbss:

#include <minix/config.h>
#include <minix/const.h>
#include <minix/com.h>
#include <ibm/interrupt.h>
#include "const.h"
#include "protect.h"
#include "sconst.h"
#include <xen/xenasm.h>
        
! Exported functions
! Note: in assembly language the .define statement applied to a function name 
! is loosely equivalent to a prototype in C code -- it makes it possible to
! link to an entity declared in the assembly code but does not create
! the entity.

.define _restart
.define save
.define _get_stacktop
.define _divide_error
.define _single_step_exception
.define _nmi
.define _breakpoint_exception
.define _overflow
.define _bounds_check
.define _inval_opcode
.define _copr_not_available
.define _double_fault
.define _copr_seg_overrun
.define _inval_tss
.define _segment_not_present
.define _stack_exception
.define _general_protection
.define _page_fault
.define _copr_error

.define _s_call
.define _p_s_call

.define scrit
.define ecrit                                                   
! Exported variables.
.define begbss
.define begdata

.sect .text
!*===========================================================================*
!*                              MINIX                                        *
!*===========================================================================*
MINIX:                          ! this is the entry point for the MINIX kernel
        jmp     over_flags      ! skip over the next few bytes
        .data2  CLICK_SHIFT     ! for the monitor: memory granularity
flags:
        .data2  0x01FD          ! boot monitor flags:
                                !       call in 386 mode, make bss, make stack,
                                !       load high, don't patch, will return,
                                !       uses generic INT, memory vector,
                                !       new boot code return
        nop                     ! extra byte to sync up disassembler
over_flags:
        push esi                        ! xen stores shared info in esi register
        call _xen_init_gdt      ! setup_hyperviser(sharedinfo, gdt, ldt)
        add esp, 4

        jmpf    CS_SELECTOR:xen_csinit
xen_csinit:             
    o16 mov     bx, DS_SELECTOR
        mov     ds, bx
        mov     es, bx
        mov     fs, bx
        mov     gs, bx
        mov     ss, bx
        mov     esp, k_stktop

! Call C startup code to set up a proper environment to run main().
        push    eax             ! _xen_init_gdt set this
        call    _xen_cstart             ! cstart(startinfo)
        add     esp, 4

        push    0                       ! set flags to known good state
        popf                            ! esp, clear nested task and int enable

        HYPERVISOR_STACK_SWITCH_W_SP(esp)               
        jmp     _main                   ! main()


!*===========================================================================*
!*                              save                                         *
!*===========================================================================*
! Save for protected mode.
! This is much simpler than for 8086 mode, because the stack already points
! into the process table, or has already been switched to the kernel stack.

        .align  16
save:
        cld                     ! set direction flag to a known value
        pushad                  ! save "general" registers
    o16 push    ds              ! save ds
    o16 push    es              ! save es
    o16 push    fs              ! save fs
    o16 push    gs              ! save gs
        mov     ax, ss          ! ss is kernel data segment
        mov     ds, ax          ! load rest of kernel segments
        mov     es, ax          ! kernel does not use fs, gs
        mov     eax, esp        ! prepare to return
        incb    (_k_reenter)    ! from -1 if not reentering
        jnz     set_restart1    ! stack is already kernel stack
        mov     esp, k_stktop
        push    _restart        ! build return address for int handler
        xor     ebp, ebp        ! for stacktrace
        jmp     RETADR-P_STACKBASE(eax)

        .align  4
set_restart1:
        push    restart1
        jmp     RETADR-P_STACKBASE(eax)

!*===========================================================================*
!*                              _s_call                                      *
!*===========================================================================*
        .align  16
_s_call:
_p_s_call:
        cld                     ! set direction flag to a known value
        sub     esp, 6*4        ! skip RETADR, eax, ecx, edx, ebx, est
        push    ebp             ! stack already points into proc table
        push    esi
        push    edi
    o16 push    ds
    o16 push    es
    o16 push    fs
    o16 push    gs
        mov     dx, ss
        mov     ds, dx
        mov     es, dx
        incb    (_k_reenter)
        mov     esi, esp        ! assumes P_STACKBASE == 0
        mov     esp, k_stktop
        xor     ebp, ebp        ! for stacktrace
                                ! end of inline save
                                ! now set up parameters for sys_call()
        push    ebx             ! pointer to user message
        push    eax             ! src/dest
        push    ecx             ! SEND/RECEIVE/BOTH
        call    _sys_call       ! sys_call(function, src_dest, m_ptr)
                                ! caller is now explicitly in proc_ptr
        mov     AXREG(esi), eax ! sys_call MUST PRESERVE si
! Fall into code to restart proc/task running.

!*===========================================================================*
!*                              restart                                      *
!*===========================================================================*
_restart:
! Restart the current process or the next process if it is set. 
scrit:
        cmp     (_next_ptr), 0          ! see if another process is scheduled
        jz      0f
        mov     eax, (_next_ptr)
        mov     (_proc_ptr), eax        ! schedule new process 
        mov     (_next_ptr), 0
0:      mov     esp, (_proc_ptr)        ! will assume P_STACKBASE == 0

        lea     eax, P_STACKTOP(esp)    ! arrange for next interrupt
        HYPERVISOR_STACK_SWITCH_W_SP(eax)
        
restart1:
        decb    (_k_reenter)
    o16 pop     gs
    o16 pop     fs
    o16 pop     es
    o16 pop     ds  
        popad
        add     esp, 4          ! skip return adr
        iretd                   ! continue process
ecrit:  !just a label
        
_get_stacktop:
        mov eax, P_STACKTOP
        ret
!*===========================================================================*
!*                              exception handlers                           *
!*===========================================================================*
_divide_error:
        push    DIVIDE_VECTOR
        jmp     exception

_single_step_exception:
        push    DEBUG_VECTOR
        jmp     exception

_nmi:
        push    NMI_VECTOR
        jmp     exception

_breakpoint_exception:
        push    BREAKPOINT_VECTOR
        jmp     exception

_overflow:
        push    OVERFLOW_VECTOR
        jmp     exception

_bounds_check:
        push    BOUNDS_VECTOR
        jmp     exception

_inval_opcode:
        push    INVAL_OP_VECTOR
        jmp     exception

_copr_not_available:
        push    COPROC_NOT_VECTOR
        jmp     exception

_double_fault:
        push    DOUBLE_FAULT_VECTOR
        jmp     errexception

_copr_seg_overrun:
        push    COPROC_SEG_VECTOR
        jmp     exception

_inval_tss:
        push    INVAL_TSS_VECTOR
        jmp     errexception

_segment_not_present:
        push    SEG_NOT_VECTOR
        jmp     errexception

_stack_exception:
        push    STACK_FAULT_VECTOR
        jmp     errexception

_general_protection:
        push    PROTECTION_VECTOR
        jmp     errexception

_page_fault:
        push    PAGE_FAULT_VECTOR
        jmp     errexception

_copr_error:
        push    COPROC_ERR_VECTOR
        jmp     exception

!*===========================================================================*
!*                              exception                                    *
!*===========================================================================*
! This is called for all exceptions which do not push an error code.

        .align  16
exception:
  sseg  mov     (trap_errno), 0         ! clear trap_errno
  sseg  pop     (ex_number)
        jmp     exception1

!*===========================================================================*
!*                              errexception                                 *
!*===========================================================================*
! This is called for all exceptions which push an error code.

        .align  16
errexception:
  sseg  pop     (ex_number)
  sseg  pop     (trap_errno)
exception1:                             ! Common for all exceptions.
        push    eax                     ! eax is scratch register
        mov     eax, 0+4(esp)           ! old eip
 sseg   mov     (old_eip), eax
        movzx   eax, 4+4(esp)           ! old cs
 sseg   mov     (old_cs), eax
        mov     eax, 8+4(esp)           ! old eflags
 sseg   mov     (old_eflags), eax
        pop     eax
        call    save
        push    (old_eflags)
        push    (old_cs)
        push    (old_eip)
        push    (trap_errno)
        push    (ex_number)
        call    _exception              ! (ex_number, trap_errno, old_eip,
                                        !       old_cs, old_eflags)
        add     esp, 5*4
        ret


.sect .rom      ! Before the string table please
        .data2  0x526F          ! this must be the first data entry (magic #)
        
.sect .bss
k_stack:
        .space  K_STACK_BYTES   ! kernel stack
        
k_stktop:                       ! top of kernel stack
        .comm   ex_number, 4
        .comm   trap_errno, 4
        .comm   old_eip, 4
        .comm   old_cs, 4
        .comm   old_eflags, 4

        