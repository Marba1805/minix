/*---------------------------------------------------------------------------*
 *  Driver for Xen console
 *
 *  Copyright (c) 2006, Ivan Kelly
 *---------------------------------------------------------------------------*/
#include <minix/config.h>
#include "../drivers.h"
#include <termios.h>
#include <signal.h>
#include "tty.h"
#include <minix/keymap.h>
#include <xen/ctrl_if.h>

#if NR_XEN_CONS > 0

#if NR_XEN_CONS > 1
#error "You can only have 1 xen console."
#endif

#define XEN_BUFFER_SIZE 1024
#define XEN_IBUFFER XEN_BUFFER_SIZE
#define XEN_OBUFFER XEN_BUFFER_SIZE

typedef struct xencons {
  tty_t *tty;

  /* input buffering */
  int icount;
  char *ihead;
  char *itail;
  char ibuf[XEN_IBUFFER];

  /* output buffering */
  int ocount;
  char *ohead;
  char *otail;
  char obuf[XEN_OBUFFER];
} xencons_t;

xencons_t xenconsole;

FORWARD _PROTOTYPE(int xencons_write, (tty_t * tp, int try));
FORWARD _PROTOTYPE(void xencons_echo, (tty_t * tp, int c));
FORWARD _PROTOTYPE(int xencons_ioctl, (tty_t * tp, int try));
FORWARD _PROTOTYPE(int xencons_read, (tty_t * tp, int try));
FORWARD _PROTOTYPE(int xencons_icancel, (tty_t * tp, int try));
FORWARD _PROTOTYPE(int xencons_ocancel, (tty_t * tp, int try));
FORWARD _PROTOTYPE(int xencons_break, (tty_t * tp, int try));
FORWARD _PROTOTYPE(int xencons_close, (tty_t * tp, int try));
FORWARD _PROTOTYPE(int xen_func_key, (ctrl_msg_t * cmsg, int *index));
FORWARD _PROTOTYPE(void xencons_flush, (void));
FORWARD _PROTOTYPE(void xencons_putk, (char c));

char xencons_buf[1024];
char xencons_buf_idx = 0;

#define NR_FMAPPINGS 24
#define NR_CODES 7

struct xen_fkey_map_t {
  int fkey;
  int len;
  int codes[NR_CODES];
} fkey_map[NR_FMAPPINGS] = {
  {F1, 3, {0x1b, 0x4f, 0x50, 0x0, 0x0}},
  {F2, 3, {0x1b, 0x4f, 0x51, 0x0, 0x0}},
  {F3, 3, {0x1b, 0x4f, 0x52, 0x0, 0x0}},
  {F4, 3, {0x1b, 0x4f, 0x53, 0x0, 0x0}},
  {F5, 5, {0x1b, 0x5b, 0x31, 0x35, 0x7e}},
  {F6, 5, {0x1b, 0x5b, 0x31, 0x37, 0x7e}},
  {F7, 5, {0x1b, 0x5b, 0x31, 0x38, 0x7e}},
  {F8, 5, {0x1b, 0x5b, 0x31, 0x39, 0x7e}},
  {F9, 5, {0x1b, 0x5b, 0x32, 0x30, 0x7e}},
  {F10, 5, {0x1b, 0x5b, 0x32, 0x31, 0x7e}},
  {F11, 5, {0x1b, 0x5b, 0x32, 0x33, 0x7e}},
  {F12, 5, {0x1b, 0x5b, 0x32, 0x35, 0x7e}},
  {SF1, 3, {0x1b, 0x4f, 0x32, 0x50, 0x0}},
  {SF2, 3, {0x1b, 0x4f, 0x32, 0x51, 0x0}},
  {SF3, 3, {0x1b, 0x4f, 0x32, 0x52, 0x0}},
  {SF4, 3, {0x1b, 0x4f, 0x32, 0x53, 0x0}},
  {SF5, 5, {0x1b, 0x5b, 0x31, 0x35, 0x3b, 0x32, 0x7e}},
  {SF6, 5, {0x1b, 0x5b, 0x31, 0x37, 0x3b, 0x32, 0x7e}},
  {SF7, 5, {0x1b, 0x5b, 0x31, 0x38, 0x3b, 0x32, 0x7e}},
  {SF8, 5, {0x1b, 0x5b, 0x31, 0x39, 0x3b, 0x32, 0x7e}},
  {SF9, 5, {0x1b, 0x5b, 0x32, 0x30, 0x3b, 0x32, 0x7e}},
  {SF10, 5, {0x1b, 0x5b, 0x32, 0x31, 0x3b, 0x32, 0x7e}},
  {SF11, 5, {0x1b, 0x5b, 0x32, 0x33, 0x3b, 0x32, 0x7e}},
  {SF12, 5, {0x1b, 0x5b, 0x32, 0x35, 0x3b, 0x32, 0x7e}}
};

/*===========================================================================*
 *				kputc					     *
 *===========================================================================*/
PUBLIC void kputc(c)
     int c;
{
  int i;
  /* Accumulate a single character for a kernel message. Send a notification
   * the to output driver if an END_OF_KMESS is encountered. 
   */
  if (c != 0) {
    xencons_putk(c);

    kmess.km_buf[kmess.km_next] = c;	/* put normal char in buffer */
    if (kmess.km_size < KMESS_BUF_SIZE)
      kmess.km_size += 1;
    kmess.km_next = (kmess.km_next + 1) % KMESS_BUF_SIZE;
  } else {
    xencons_flush();
  }
}

/*===========================================================================*
 *				xencons_write				     *
 *===========================================================================*/
PRIVATE int xencons_write(tp, try)
     register tty_t *tp;
     int try;
{
  int count, ocount;

  while (TRUE) {
    ocount = buflen(xenconsole.obuf) - xenconsole.ocount;
    count = bufend(xenconsole.obuf) - xenconsole.ohead;

    if (count > ocount)
      count = ocount;
    if (count > tp->tty_outleft)
      count = tp->tty_outleft;
    if (count == 0) {
      if (try)
	return 0;
      break;
    }
    if (try)
      return 1;

    sys_vircopy(tp->tty_outproc, D,
		(vir_bytes) tp->tty_out_vir, SELF, D,
		(vir_bytes) xenconsole.ohead,
		(phys_bytes) count);

    out_process(tp, xenconsole.obuf, xenconsole.ohead,
		bufend(xenconsole.obuf), &count, &ocount);
    if (count == 0)
      break;

    tp->tty_reprint = TRUE;

    xenconsole.ocount += ocount;
    xenconsole.ohead += ocount;
    if (xenconsole.ohead >= bufend(xenconsole.obuf)) {
      xenconsole.ohead -= buflen(xenconsole.obuf);
    }
    xencons_flush();

    tp->tty_out_vir += count;
    tp->tty_outcum += count;

    if ((tp->tty_outleft -= count) == 0) {
      tty_reply(tp->tty_outrepcode, tp->tty_outcaller,
		tp->tty_outproc, tp->tty_outcum);
      tp->tty_outcum = 0;
    }
  }
  if (tp->tty_outleft > 0) {
    tty_reply(tp->tty_outrepcode, tp->tty_outcaller,
	      tp->tty_outproc, EIO);
    tp->tty_outleft = tp->tty_outcum = 0;
  }

  return 1;
}

/*===========================================================================*
 *				xencons_echo				     *
 *===========================================================================*/
PRIVATE void xencons_echo(tp, c)
     tty_t *tp;			/* which TTY */
     int c;				/* character to echo */
{
  char *grrr = "fooo[X]\r\n";

  /* Echo one character.  (Like xencons_write, but only one character, optionally.) */
  xencons_putk(c);
  xencons_flush();
}

/*===========================================================================*
 *                              xencons_flush                                *
 *===========================================================================*/
PRIVATE void xencons_flush()
{
  int count, ocount;
  int i;
  ctrl_msg_t *cmsg;
  message wmsg;

  while (xenconsole.ocount > 0) {
    ocount = xenconsole.ocount;
    count = bufend(xenconsole.obuf) - xenconsole.otail;

    if (count > ocount) {
      count = ocount;
    }

    if (count > M9_CTRL_MSG) {
      count = M9_CTRL_MSG;
    }
    if (count == 0) {
      return;
    }

    wmsg.m_source = TTY_PROC_NR;
    wmsg.m_type = CTRLIF_SEND_BLOCK;

    cmsg = (ctrl_msg_t *)&wmsg.m9_msg;
    cmsg->type = CMSG_CONSOLE;
    cmsg->subtype = CMSG_CONSOLE_DATA;
    cmsg->length = count;
    for (i = 0; i < count; i++) {
      cmsg->msg[i] = xenconsole.otail[i];
    }

    xenconsole.ocount -= count;
    xenconsole.otail += count;
    if (xenconsole.otail >= bufend(xenconsole.obuf)) {
      xenconsole.otail -= buflen(xenconsole.obuf);
    }

    sendrec(CTRLIF, &wmsg);
  }
}

/*===========================================================================*
 *				xencons_ioctl				     *
 *===========================================================================*/
PRIVATE int xencons_ioctl(tp, dummy)
     tty_t *tp;			/* which TTY */
     int dummy;
{
  return 0;		/* dummy */
}

/*===========================================================================*
 *				xencons_init				     *
 *===========================================================================*/
PUBLIC void xencons_init(tp)
     tty_t *tp;			/* which TTY */
{
  int dummy;
  message regmsg;

  /* Initialize Xen console for one line. */
  /* register this process as msg handler for console */
  regmsg.m_source = TTY_PROC_NR;
  regmsg.m_type = CTRLIF_REG_HND;
  regmsg.m5_c1 = CMSG_CONSOLE;
  regmsg.m5_i1 = TTY_PROC_NR;
  sendrec(CTRLIF, &regmsg);

  /* Associate XENConsole and TTY structure. */
  tp->tty_priv = &xenconsole;
  xenconsole.tty = tp;

  /* Set up queues. */
  xenconsole.ihead = xenconsole.itail = xenconsole.ibuf;
  xenconsole.ohead = xenconsole.otail = xenconsole.obuf;

  xenconsole.icount = 0;
  xenconsole.ocount = 0;

  /* Fill in TTY function hooks. */
  tp->tty_devread = xencons_read;
  tp->tty_devwrite = xencons_write;
  tp->tty_echo = xencons_echo;
  tp->tty_icancel = xencons_icancel;
  tp->tty_ocancel = xencons_ocancel;
  tp->tty_ioctl = xencons_ioctl;
  tp->tty_break = xencons_break;
  tp->tty_close = xencons_close;
}

/*===========================================================================*
 *				xencons_icancel				     *
 *===========================================================================*/
PRIVATE int xencons_icancel(tp, dummy)
     tty_t *tp;			/* which TTY */
     int dummy;
{
  /* Cancel waiting input. */
  xenconsole.icount = 0;
  xenconsole.itail = xenconsole.ihead;

  return 0;		/* dummy */
}

/*===========================================================================*
 *				xencons_ocancel				     *
 *===========================================================================*/
PRIVATE int xencons_ocancel(tp, dummy)
     tty_t *tp;			/* which TTY */
     int dummy;
{
  /* Cancel pending output. */
  xenconsole.ocount = 0;
  xenconsole.otail = xenconsole.ohead;

  return 0;		/* dummy */
}

/*===========================================================================*
 *				xencons_read					     *
 *===========================================================================*/
PRIVATE int xencons_read(tp, try)
     tty_t *tp;			/* which tty */
     int try;
{
  /* Process characters from the circular input buffer. */
  int count, icount;

  if (try) {
    if (xenconsole.icount > 0) {
      return 1;
    }
    return 0;
  }

  while ((count = xenconsole.icount) > 0) {
    icount = bufend(xenconsole.ibuf) - xenconsole.itail;
    if (count > icount)
      count = icount;

    if ((count = in_process(tp, xenconsole.itail, count)) == 0)
      break;

    xenconsole.icount -= count;

    if ((xenconsole.itail += count) >= bufend(xenconsole.ibuf)) {
      xenconsole.itail -= buflen(xenconsole.ibuf);
    }
  }
  return 1;
}

/*===========================================================================*
 *				xencons_break				     *
 *===========================================================================*/
PRIVATE int xencons_break(tp, dummy)
     tty_t *tp;			/* which tty */
     int dummy;
{
  /* Generate a break condition by setting the BREAK bit for 0.4 sec. */
  /* does nothing for xen */
  return 0;		/* dummy */
}

/*===========================================================================*
 *				xencons_close				     *
 *===========================================================================*/
PRIVATE int xencons_close(tp, dummy)
     tty_t *tp;			/* which tty */
     int dummy;
{
  /* The line is closed; optionally hang up. */
  /* does nothing */
  return 0;		/* dummy */
}

PUBLIC void xencons_interrupt(message * msg)
{
  ctrl_msg_t *cmsg = (ctrl_msg_t *) & msg->m9_msg;
  int i;
  int fkey;
  char *foo = "int [X]\r\n";

  /* if buffer is full, discard the char */
  if (xenconsole.icount >= buflen(xenconsole.ibuf))
    return;

  /* if the msg is currupt, discard */
  if (cmsg->subtype != CMSG_CONSOLE_DATA)
    return;

  for (i = 0; i < cmsg->length; i++) {
    if (xen_func_key(cmsg, &i)) {
      continue;
    }

    *xenconsole.ihead = cmsg->msg[i];
    xenconsole.ihead++;
    if (xenconsole.ihead >= bufend(xenconsole.ibuf)) {
      xenconsole.ihead -= buflen(xenconsole.ibuf);
    }
    xenconsole.icount++;
    if (xenconsole.icount > 0) {
      /*              xencons_print("work shall be done\r\n"); */
      xenconsole.tty->tty_events = 1;
    }
  }
}

/*===========================================================================*
 *				func_key				     *
 *===========================================================================*/
PRIVATE int xen_func_key(ctrl_msg_t * cmsg, int *index)
{
  /* This procedure traps function keys for debugging purposes. Observers of 
   * function keys are kept in a global array. If a subject (a key) is pressed
   * the observer is notified of the event. Initialization of the arrays is done
   * in kb_init, where NONE is set to indicate there is no interest in the key.
   * Returns FALSE on a key release or if the key is not observable.
   */
  message m;
  int key = 0;
  int proc_nr;
  int i, s, len = 0, mismatch = FALSE;
  int fkey_i = 0;
  struct xen_fkey_map_t *mapping;

  if (cmsg->msg[0] != 0x1b) {
    return FALSE;
  }

  for (i = 0; i < NR_FMAPPINGS; i++) {
    mapping = &fkey_map[i];
    mismatch = FALSE;

    if ((cmsg->length - *index) != mapping->len) {
      continue;
    }
    for (s = 0; s < mapping->len; s++) {
      if (cmsg->msg[((*index) + s)] != mapping->codes[s]) {
	mismatch = TRUE;
	break;
      }
    }
    if (mismatch) {
      continue;
    }
    key = mapping->fkey;
    *index += mapping->len;
    break;
  }

  /* Key pressed, now see if there is an observer for the pressed key.
   *             F1-F12   observers are in fkey_obs array. 
   *      SHIFT  F1-F12   observers are in sfkey_req array. 
   *      CTRL   F1-F12   reserved (see kb_read)
   *      ALT    F1-F12   reserved (see kb_read)
   * Other combinations are not in use. Note that Alt+Shift+F1-F12 is yet
   * defined in <minix/keymap.h>, and thus is easy for future extensions.
   */
  if (F1 <= key && key <= F12) {	/* F1-F12 */
    proc_nr = fkey_obs[key - F1].proc_nr;
    fkey_obs[key - F1].events++;
  } else if (SF1 <= key && key <= SF12) {	/* Shift F2-F12 */
    proc_nr = sfkey_obs[key - SF1].proc_nr;
    sfkey_obs[key - SF1].events++;
  } else {
    return (FALSE);	/* not observable */
  }

  /* See if an observer is registered and send it a message. */
  if (proc_nr != NONE) {
    m.NOTIFY_TYPE = FKEY_PRESSED;
    notify(proc_nr);
  }
  return (TRUE);
}

PRIVATE void xencons_putk(char c)
{
  int count, ocount;

  ocount = buflen(xenconsole.obuf) - xenconsole.ocount;
  count = 1;
  if (ocount == 0) {
    xencons_flush();
    xencons_putk(c);
    return;
  }

  *xenconsole.ohead = c;

  out_process(xenconsole.tty,
	      xenconsole.obuf, xenconsole.ohead,
	      bufend(xenconsole.obuf), &count, &ocount);

  xenconsole.ocount += ocount;
  xenconsole.ohead += ocount;
  if (xenconsole.ohead >= bufend(xenconsole.obuf)) {
    xenconsole.ohead -= buflen(xenconsole.obuf);
  }
}

/*===========================================================================*
 *				do_diagnostics				     *
 *===========================================================================*/
PUBLIC void do_xen_diagnostics(m_ptr)
     message *m_ptr;			/* pointer to request message */
{
  /* Print a string for a server. */
  char buf[XEN_BUFFER_SIZE];
  vir_bytes src;
  int count, left, i, c;
  int result = OK;
  int proc_nr = m_ptr->DIAG_PROC_NR;
  int offset = 0;
  if (proc_nr == SELF)
    proc_nr = m_ptr->m_source;

  src = (vir_bytes) m_ptr->DIAG_PRINT_BUF;

  left = m_ptr->DIAG_BUF_COUNT;
  while (left > 0) {
    if (left > XEN_BUFFER_SIZE) {
      count = XEN_BUFFER_SIZE;
    } else {
      count = left;
    }

    if (sys_vircopy(proc_nr, D, src + offset, SELF,
		    D, (vir_bytes) & buf, count) != OK) {
      result = EFAULT;
      break;
    }

    for (i = 0; i < count; i++) {
      xencons_putk(buf[i]);
    }

    left -= count;
    offset += count;
    xencons_flush();
  }

  m_ptr->m_type = result;
  send(m_ptr->m_source, m_ptr);
}

/*===========================================================================*
 *				do_new_kmess				     *
 *===========================================================================*/
PUBLIC void do_xen_kmess(m)
     message *m;
{
  /* Notification for a new kernel message. */
  struct kmessages kmess;	/* kmessages structure */
  static int prev_next = 0;	/* previous next seen */
  int size, next;
  int avail;
  int bytes;
  int r;

  /* Try to get a fresh copy of the buffer with kernel messages. */
  r = sys_getkmessages(&kmess);

  /* Print only the new part. Determine how many new bytes there are with 
   * help of the current and previous 'next' index. Note that the kernel
   * buffer is circular. This works fine if less then KMESS_BUF_SIZE bytes
   * is new data; else we miss % KMESS_BUF_SIZE here.  
   * Check for size being positive, the buffer might as well be emptied!
   */
  if (kmess.km_size > 0) {

    bytes =
      ((kmess.km_next + KMESS_BUF_SIZE) -
       prev_next) % KMESS_BUF_SIZE;
    r = prev_next;	/* start at previous old */

    while (bytes > 0) {
      xencons_putk(kmess.km_buf[(r % KMESS_BUF_SIZE)]);

      bytes--;
      r++;
    }
    xencons_flush();
  }

  /* Almost done, store 'next' so that we can determine what part of the
   * kernel messages buffer to print next time a notification arrives.
   */
  prev_next = kmess.km_next;
}

/**
 * Send a string directly to output, bypassing buffers.
 * used for debugging
 */
PUBLIC void xencons_print(char *str)
{
  int i, count;
  ctrl_msg_t *cmsg;
  message wmsg;

  wmsg.m_source = TTY_PROC_NR;
  wmsg.m_type = CTRLIF_SEND_BLOCK;
  cmsg = (ctrl_msg_t *)&wmsg.m9_msg;
  cmsg->type = CMSG_CONSOLE;
  cmsg->subtype = CMSG_CONSOLE_DATA;
  for (i = 0; i < M9_CTRL_MSG; i++) {
    cmsg->msg[i] = str[i];
    cmsg->length = i + 1;
    if (str[i] == 0) {
      break;
    }
  }
  sendrec(CTRLIF, &wmsg);
}

#endif				/* NR_XEN_CONS > 0 */
