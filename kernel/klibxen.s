#
! Xen modifications - Copyright (c) 2006, I Kelly
! 
! sections

.sect .text; .sect .rom; .sect .data; .sect .bss

#include <minix/config.h>
#include <minix/const.h>
#include "const.h"
#include "sconst.h"
#include "protect.h"
#include <xen/xenasm.h>
        
! This file contains a number of assembly code utility routines needed by the
! kernel.  They are:

.define _cp_mess        ! copies messages from source to destination
.define _exit           ! dummy for library routines
.define __exit          ! dummy for library routines
.define ___exit         ! dummy for library routines
.define ___main         ! dummy for GCC
.define _phys_copy      ! copy data from anywhere to anywhere in memory
.define _phys_memset    ! write pattern anywhere in memory
.define _mem_rdw        ! copy one word from [segment:offset]
.define _idle_task      ! task executed when there is no work
.define _read_tsc       ! read the cycle counter (Pentium and up)
.define _read_cpu_flags ! read the cpu flags
.define _xen_op
.define _hypervisor_callback
.define _failsafe_callback
.define _x86_atomic_xchg
.define _x86_atomic_clear_bit
.define _x86_atomic_test_bit
.define _x86_atomic_set_bit
.define _x86_atomic_test_and_set_bit
.define _x86_scan_forward
.define _x86_barrier
.define _current_ring
.define _xen_proxy
.define _xen_proxy_int

! The routines only guarantee to preserve the registers the C compiler
! expects to be preserved (ebx, esi, edi, ebp, esp, segment registers, and
! direction bit in the flags).

.sect .text

!*===========================================================================*
!*                              cp_mess                                      *
!*===========================================================================*
! PUBLIC void cp_mess(int src, phys_clicks src_clicks, vir_bytes src_offset,
!                     phys_clicks dst_clicks, vir_bytes dst_offset);
! This routine makes a fast copy of a message from anywhere in the address
! space to anywhere else.  It also copies the source address provided as a
! parameter to the call into the first word of the destination message.
!
! Note that the message size, "Msize" is in DWORDS (not bytes) and must be set
! correctly.  Changing the definition of message in the type file and not
! changing it here will lead to total disaster.

CM_ARGS =       4 + 4 + 4 + 4 + 4       ! 4 + 4 + 4 + 4 + 4
!               es  ds edi esi eip      proc scl sof dcl dof

        .align  16
_cp_mess:
        cld
        push    esi
        push    edi
        push    ds
        push    es

        mov     eax, FLAT_DS_SELECTOR
        mov     ds, ax
        mov     es, ax

        mov     esi, CM_ARGS+4(esp)             ! src clicks
        shl     esi, CLICK_SHIFT
        add     esi, CM_ARGS+4+4(esp)           ! src offset
        mov     edi, CM_ARGS+4+4+4(esp)         ! dst clicks
        shl     edi, CLICK_SHIFT
        add     edi, CM_ARGS+4+4+4+4(esp)       ! dst offset

        mov     eax, CM_ARGS(esp)       ! process number of sender
        stos                            ! copy number of sender to dest message
        add     esi, 4                  ! do not copy first word
        mov     ecx, Msize - 1          ! remember, first word does not count
        rep
        movs                            ! copy the message

        pop     es
        pop     ds
        pop     edi
        pop     esi
        ret                             ! that is all folks!


!*===========================================================================*
!*                              exit                                         *
!*===========================================================================*
! PUBLIC void exit();
! Some library routines use exit, so provide a dummy version.
! Actual calls to exit cannot occur in the kernel.
! GNU CC likes to call ___main from main() for nonobvious reasons.

_exit:
__exit:
___exit:
        sti
        jmp     ___exit

___main:
        ret


!*===========================================================================*
!*                              phys_copy                                    *
!*===========================================================================*
! PUBLIC void phys_copy(phys_bytes source, phys_bytes destination,
!                       phys_bytes bytecount);
! Copy a block of physical memory.

PC_ARGS =       4 + 4 + 4 + 4   ! 4 + 4 + 4
!               es edi esi eip   src dst len

        .align  16
_phys_copy:
        cld
        push    esi
        push    edi
        push    es

        mov     eax, FLAT_DS_SELECTOR
        mov     es, ax

        mov     esi, PC_ARGS(esp)
        mov     edi, PC_ARGS+4(esp)
        mov     eax, PC_ARGS+4+4(esp)

        cmp     eax, 10                 ! avoid align overhead for small counts
        jb      pc_small
        mov     ecx, esi                ! align source, hope target is too
        neg     ecx
        and     ecx, 3                  ! count for alignment
        sub     eax, ecx
        rep
   eseg movsb
        mov     ecx, eax
        shr     ecx, 2                  ! count of dwords
        rep
   eseg movs
        and     eax, 3
pc_small:
        xchg    ecx, eax                ! remainder
        rep
   eseg movsb

        pop     es
        pop     edi
        pop     esi
        ret

!*===========================================================================*
!*                              phys_memset                                  *
!*===========================================================================*
! PUBLIC void phys_memset(phys_bytes source, unsigned long pattern,
!       phys_bytes bytecount);
! Fill a block of physical memory with pattern.

        .align  16
_phys_memset:
        push    ebp
        mov     ebp, esp
        push    esi
        push    ebx
        push    ds
        mov     esi, 8(ebp)
        mov     eax, 16(ebp)
        mov     ebx, FLAT_DS_SELECTOR
        mov     ds, bx
        mov     ebx, 12(ebp)
        shr     eax, 2
fill_start:
        mov     (esi), ebx
        add     esi, 4
        dec     eax
        jnz     fill_start
        ! Any remaining bytes?
        mov     eax, 16(ebp)
        and     eax, 3
remain_fill:
        cmp     eax, 0
        jz      fill_done
        movb    bl, 12(ebp)
        movb    (esi), bl
        add     esi, 1
        inc     ebp
        dec     eax
        jmp     remain_fill
fill_done:
        pop     ds
        pop     ebx
        pop     esi
        pop     ebp
        ret

!*===========================================================================*
!*                              mem_rdw                                      *
!*===========================================================================*
! PUBLIC u16_t mem_rdw(U16_t segment, u16_t *offset);
! Load and return word at far pointer segment:offset.

        .align  16
_mem_rdw:
        mov     cx, ds
        mov     ds, 4(esp)              ! segment
        mov     eax, 4+4(esp)           ! offset
        movzx   eax, (eax)              ! word to return
        mov     ds, cx
        ret


!*===========================================================================*
!*                              idle_task                                    *
!*===========================================================================*
_idle_task:
! This task is called when the system has nothing else to do.  The HLT
! instruction puts the processor in a state where it draws minimum power.
        jmp     _idle_task

!*===========================================================================*
!*                            read_tsc                                       *
!*===========================================================================*
! PUBLIC void read_tsc(unsigned long *high, unsigned long *low);
! Read the cycle counter of the CPU. Pentium and up. 
.align 16
_read_tsc:
.data1 0x0f             ! this is the RDTSC instruction 
.data1 0x31             ! it places the TSC in EDX:EAX
        push ebp
        mov ebp, 8(esp)
        mov (ebp), edx
        mov ebp, 12(esp)
        mov (ebp), eax
        pop ebp
        ret

!*===========================================================================*
!*                            read_flags                                             *
!*===========================================================================*
! PUBLIC unsigned long read_cpu_flags(void);
! Read CPU status flags from C.
.align 16
_read_cpu_flags:
        pushf
        mov eax, (esp)
        popf
        ret

!*==========================================================================*
!*                      _xen_op                                             *
!*==========================================================================*
! PUBLIC int xen_op(int vector, ...)
! Perform a xen operation. First argument is the operation to perform,
! Subsequent arguments are the arguments for that operation.
.align 16
_xen_op:
        mov     eax, 4(esp)
        mov     ebx, 8(esp)
        mov     ecx, 12(esp)
        mov     edx, 16(esp)
        mov     esi, 20(esp)
        int     XEN_TRAP_VECTOR
        ret     

!*==========================================================================*
!*                      _xen_proxy_int                                      *
!*==========================================================================*
! PUBLIC void xen_proxy_int(void)
! Make a software interrupt, causing xen_proxy to be called.
.align 16
_xen_proxy_int:
        int     XEN_PROXY_VECTOR
        ret

!*==========================================================================*
!*                      _xen_proxy                                          *
!*==========================================================================*
! PUBLIC void xen_proxy(void)
! Save cpu state and execute a saved xen operation.
.align 16
_xen_proxy:
        call save
        call _do_xen_proxy_op
        ret

!*==========================================================================*
!*                      _hypervisor_callback                                *
!*==========================================================================*
! PUBLIC void hypervisor_callback(void)
! Callback xen calls when there's an interrupts that needed attention
.align 16
_hypervisor_callback:
        call    save
        incb    (_k_callback)
        XEN_GET_VCPU_INFO(esi)
0:      XEN_BLOCK_EVENTS(esi)
        push    esp
        call    _do_hypervisor_callback
        add     esp, 4
        XEN_GET_VCPU_INFO(esi)
        XEN_UNBLOCK_EVENTS(esi)
        XEN_TEST_PENDING(esi)
        jnz 0b
        decb    (_k_callback)
        ret
        
!*==========================================================================*
!*                      _failsafe_callback                                  *
!*==========================================================================*
! PUBLIC void failsafe_callback(void)
! Callback xen when it can't do anything else.
.align 16
_failsafe_callback:
        iretd

!*==========================================================================*
!*                      _current_ring                                       *
!*==========================================================================*
! PUBLIC int current_ring(void)
! Returns the current privilege ring.
.align 16
_current_ring:
        mov     eax, ss
        and     eax, 0x3        ! make sure only the rpl is there
        ret

!*==========================================================================*
!* _x86_atomic_xchg, _x86_scan_forward, _x86_atomic_test_bit,               *
!* _x86_atomic_test_and_set_bit, x86_atomic_clear_bit, x86_atomic_set_bit   *
!* _x86_barrier                                                             *
!*==========================================================================*
! These function are just wrappers around assembly instructions so they can
! called from C.
.align 16
_x86_atomic_xchg:
        mov ebx, 4(esp)
        mov eax, 8(esp)
        lock
        xchg eax, (ebx)
        ret
        
.align 16
_x86_scan_forward:
        mov ebx, 4(esp)
        bsf eax, ebx
        ret

.align 16
_x86_atomic_test_bit:
        mov ebx, 4(esp)
        mov ecx, 8(esp)
        bt  (ecx), ebx
        sbb eax, eax
        ret

.align 16
_x86_atomic_test_and_set_bit:
        mov ebx, 4(esp)
        mov ecx, 8(esp)
        bts (ecx), ebx
        sbb eax, eax
        ret

.align 16
_x86_atomic_clear_bit:
        mov eax, 4(esp)
        mov ebx, 8(esp)
        btr (ebx), eax  
        ret

.align 16
_x86_atomic_set_bit:    
        mov ebx, 4(esp)
        mov ecx, 8(esp)
        bts (ecx), ebx
        ret

_x86_barrier:
        xchg ebx, ebx
        ret
        
