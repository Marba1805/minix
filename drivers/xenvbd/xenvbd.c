/**
 * Minix xen block device
 *
 * Copyright (c) 2006, Ivan Kelly
 */
#include "../drivers.h"
#include "../libdriver/driver.h"
#include "../../kernel/const.h"
#include "../../kernel/config.h"
#include "../../kernel/type.h"
#include "assert.h"
#include <xen/xen.h>
#include <xen/domain_controller.h>
#include <xen/ctrl_if.h>
#include <xen/blkif.h>


#define NR_DEVS 1
PRIVATE struct device x_geom[NR_DEVS];	/* base and size of each device */
PRIVATE int x_seg[NR_DEVS];	/* segment index of each device */
PRIVATE int x_device;		/* current device */

PRIVATE struct kinfo kinfo;	/* kernel information */
PRIVATE struct machine machine;	/* machine information */


/**
 * Buffer is 2 PAGES in size, because it must be block aligned
 */
PRIVATE u8_t blk_ring_buf[PAGE_SIZE*2];
PRIVATE blkif_ring_t *blk_ring = NULL;
PRIVATE memory_t blk_ring_shmem_frame = 0;

extern int errno;		/* error number for PM calls */

FORWARD _PROTOTYPE(char *x_name, (void));
FORWARD _PROTOTYPE(struct device *x_prepare, (int device));
FORWARD _PROTOTYPE(int x_transfer,
		   (int proc_nr, int opcode, off_t position, iovec_t * iov,
		    unsigned nr_req));
FORWARD _PROTOTYPE(int x_do_open, (struct driver * dp, message * m_ptr));
FORWARD _PROTOTYPE(int x_do_close, (struct driver *dp, message *m_ptr));

FORWARD _PROTOTYPE(void x_init, (void));
FORWARD _PROTOTYPE(int x_ioctl, (struct driver * dp, message * m_ptr));
FORWARD _PROTOTYPE(void x_geometry, (struct partition * entry));
FORWARD _PROTOTYPE(int x_hw_int, (struct driver *dp, message *m_ptr));

/* Entry points to this driver. */
PRIVATE struct driver x_dtab = {
  x_name,			/* current device's name */
  x_do_open,		/* open or mount */
  x_do_close,			/* nothing on a close */
  x_ioctl,		/* specify ram disk geometry */
  x_prepare,		/* prepare for I/O on a given minor device */
  x_transfer,		/* do the I/O */
  nop_cleanup,		/* no need to clean up */
  x_geometry,		/* memory device "geometry" */
  nop_signal,		/* system signals */
  nop_alarm,
  nop_cancel,
  nop_select,
  NULL,
  x_hw_int
};

#define click_to_round_k(n)						\
  ((unsigned) ((((unsigned long) (n) << CLICK_SHIFT) + 512) / 1024))

#define sys_getvtom(dst, nr)	sys_getinfo(GET_VTOM, dst, 0,0, nr)

int buf_count = 0;		/* # characters in the buffer */
char print_buf[80];		/* output is buffered here */

void kputc(c)
     int c;
{
  /* Accumulate another character.  If 0 or buffer full, print it. */
  message m;

  if ((c == 0 && buf_count > 0) || buf_count == sizeof(print_buf)) {
    int procs[] = OUTPUT_PROCS_ARRAY;
    int p;

    for (p = 0; procs[p] != NONE; p++) {
      /* Send the buffer to this output driver. */
      m.DIAG_BUF_COUNT = buf_count;
      m.DIAG_PRINT_BUF = print_buf;
      m.DIAG_PROC_NR = SELF;
      m.m_type = DIAGNOSTICS;

      (void) _sendrec(procs[p], &m);

    }
    buf_count = 0;

    /* If the output fails, e.g., due to an ELOCKED, do not retry output
     * at the FS as if this were a normal user-land printf(). This may
     * result in even worse problems.
     */
  }

  if (c != 0) {
    /* Append a single character to the output buffer. */
    print_buf[buf_count++] = c;
  }
}

/*===========================================================================*
 *				   main 				     *
 *===========================================================================*/
PUBLIC int main(void)
{
  /* Main program. Initialize the memory driver and start the main loop. */
  x_init();

  printf("init'd\n");
  driver_task(&x_dtab);

  return (OK);
}

/*===========================================================================*
 *				 x_name					     *
 *===========================================================================*/
PRIVATE char *x_name()
{
  /* Return a name for the current device. */
  static char name[] = "xenvbd";
  printf("x_name\n");
  return name;
}

/*===========================================================================*
 *				x_prepare				     *
 *===========================================================================*/
PRIVATE struct device *x_prepare(device)
     int device;
{
  printf("x_prepare\n");
#if 0
  /* Prepare for I/O on a device: check if the minor device number is ok. */
  if (device < 0 || device >= NR_DEVS)
    return (NIL_DEV);
  x_device = device;

#endif
  return (&x_geom[device]);
}

/*===========================================================================*
 *				m_transfer				     *
 *===========================================================================*/
PRIVATE int x_transfer(proc_nr, opcode, position, iov, nr_req)
     int proc_nr;			/* process doing the request */
     int opcode;			/* DEV_GATHER or DEV_SCATTER */
     off_t position;			/* offset on device to read or write */
     iovec_t *iov;			/* pointer to read or write request vector */
     unsigned nr_req;		/* length of request vector */
{
  /* Read or write one the driver's minor devices. */
  phys_bytes mem_phys;
  int seg;
  unsigned count, left, chunk;
  vir_bytes user_vir;
  struct device *dv;
  unsigned long dv_size;
  int s;

  printf("x_transfer\n");
#if 0
  /* Get minor device number and check for /dev/null. */
  dv = &m_geom[m_device];
  dv_size = cv64ul(dv->dv_size);

  while (nr_req > 0) {

    /* How much to transfer and where to / from. */
    count = iov->iov_size;
    user_vir = iov->iov_addr;

    switch (m_device) {

      /* No copying; ignore request. */
    case NULL_DEV:
      if (opcode == DEV_GATHER)
	return (OK);	/* always at EOF */
      break;

      /* Virtual copying. For RAM disk, kernel memory and boot device. */
    case RAM_DEV:
    case KMEM_DEV:
    case BOOT_DEV:
      if (position >= dv_size)
	return (OK);	/* check for EOF */
      if (position + count > dv_size)
	count = dv_size - position;
      seg = m_seg[m_device];

      if (opcode == DEV_GATHER) {	/* copy actual data */
	sys_vircopy(SELF, seg, position, proc_nr,
		    D, user_vir, count);
      } else {
	sys_vircopy(proc_nr, D, user_vir, SELF,
		    seg, position, count);
      }
      break;

      /* Physical copying. Only used to access entire memory. */
    case MEM_DEV:
      if (position >= dv_size)
	return (OK);	/* check for EOF */
      if (position + count > dv_size)
	count = dv_size - position;
      mem_phys = cv64ul(dv->dv_base) + position;

      if (opcode == DEV_GATHER) {	/* copy data */
	sys_physcopy(NONE, PHYS_SEG, mem_phys,
		     proc_nr, D, user_vir, count);
      } else {
	sys_physcopy(proc_nr, D, user_vir,
		     NONE, PHYS_SEG, mem_phys,
		     count);
      }
      break;

      /* Null byte stream generator. */
    case ZERO_DEV:
      if (opcode == DEV_GATHER) {
	left = count;
	while (left > 0) {
	  chunk =
	    (left >
	     ZERO_BUF_SIZE) ? ZERO_BUF_SIZE
	    : left;
	  if (OK !=
	      (s =
	       sys_vircopy(SELF, D,
			   (vir_bytes)
			   dev_zero, proc_nr,
			   D, user_vir,
			   chunk)))
	    report("MEM",
		   "sys_vircopy failed",
		   s);
	  left -= chunk;
	  user_vir += chunk;
	}
      }
      break;

      /* Unknown (illegal) minor device. */
    default:
      return (EINVAL);
    }

    /* Book the number of bytes transferred. */
    position += count;
    iov->iov_addr += count;
    if ((iov->iov_size -= count) == 0) {
      iov++;
      nr_req--;
    }

  }
#endif
  return (OK);
}

/*===========================================================================*
 *				x_do_open				     *
 *===========================================================================*/
PRIVATE int x_do_open(dp, m_ptr)
     struct driver *dp;
     message *m_ptr;
{
  /* Check device number on open. */
  printf("x_do_open\n");
#if 0
  if (x_prepare(m_ptr->DEVICE) == NIL_DEV)
    return (ENXIO);
#endif
  return (OK);
}

/*===========================================================================*
 *				x_do_close				     *
 *===========================================================================*/
PRIVATE int x_do_close(dp, m_ptr)
     struct driver *dp;
     message *m_ptr;
{
}

/*===========================================================================*
 *				x_init					     *
 *===========================================================================*/
PRIVATE void x_init()
{
  /* Initialize this task. All minor devices are initialized one by one. */
  phys_bytes ramdev_size;
  phys_bytes ramdev_base;
  message regmsg, m;
  ctrl_msg_t *cmsg;
  blkif_fe_driver_status_t *statmsg;
  int i, s;
  long mf = 0;

  printf("Starting VBD\n");

  printf("buf addr: %x\n", &blk_ring_buf);
  blk_ring = (blkif_ring_t*)((((unsigned long)&blk_ring_buf)+PAGE_SIZE) & ~(PAGE_SIZE-1));
  printf("blk_ring_addr: %x\n", blk_ring);
  if ((s = sys_getvtom(&blk_ring, blk_ring)) != OK) {
    panic("XENVBD", "Couldn't convert virtual address to machine frame", s);
  }
  printf("blk_ring_addr: %x\n", blk_ring);


  /** connect to evtchn */
  regmsg.m_source = DRVR_PROC_NR;
  regmsg.m_type = CTRLIF_REG_HND;
  regmsg.m5_c1 = CMSG_BLKIF_FE;
  regmsg.m5_i1 = DRVR_PROC_NR;
  sendrec(CTRLIF, &regmsg);

  m.m_source = DRVR_PROC_NR;
  m.m_type = CTRLIF_SEND_BLOCK;
  cmsg = &m.m9_msg;
  cmsg->type = CMSG_BLKIF_FE;
  cmsg->subtype = CMSG_BLKIF_FE_DRIVER_STATUS;
  cmsg->length = sizeof(blkif_fe_driver_status_t);
  statmsg = &cmsg->msg;
  statmsg->status = BLKIF_DRIVER_STATUS_UP;
  sendrec(CTRLIF, &m);
}

/*===========================================================================*
 *				m_ioctl					     *
 *===========================================================================*/
PRIVATE int x_ioctl(dp, m_ptr)
     struct driver *dp;		/* pointer to driver structure */
     message *m_ptr;			/* pointer to control message */
{
  /* I/O controls for the memory driver. Currently there is one I/O control:
   * - MIOCRAMSIZE: to set the size of the RAM disk.
   */
  struct device *dv;

  printf("x_ioctl\n");
#if 0
  switch (m_ptr->REQUEST) {
  case MIOCRAMSIZE:{
    /* FS wants to create a new RAM disk with the given size. */
    phys_bytes ramdev_size;
    phys_bytes ramdev_base;
    message m;
    int s;

    /* Only FS can create RAM disk, and only on RAM disk device. */
    if (m_ptr->PROC_NR != FS_PROC_NR)
      return (EPERM);
    if (m_ptr->DEVICE != RAM_DEV)
      return (EINVAL);
    if ((dv = m_prepare(m_ptr->DEVICE)) == NIL_DEV)
      return (ENXIO);

    /* Try to allocate a piece of memory for the RAM disk. */
    ramdev_size = m_ptr->POSITION;
    if (allocmem(ramdev_size, &ramdev_base) < 0) {
      report("MEM", "warning, allocmem failed",
	     errno);
      return (ENOMEM);
    }

    /* Store the values we got in the data store so we can retrieve
     * them later on, in the unfortunate event of a crash.
     */
    m.DS_KEY = MEMORY_MAJOR;
    m.DS_VAL_L1 = ramdev_size;
    m.DS_VAL_L2 = ramdev_base;
    if (OK !=
	(s = _taskcall(DS_PROC_NR, DS_PUBLISH, &m))) {
      panic("MEM",
	    "Couldn't store RAM disk details at DS.",
	    s);
    }
    printf
      ("MEM stored size %u and base %u at DS, status %d\n",
       ramdev_size, ramdev_base, s);

    if (OK !=
	(s =
	 sys_segctl(&m_seg[RAM_DEV], (u16_t *) & s,
		    (vir_bytes *) & s, ramdev_base,
		    ramdev_size))) {
      panic("MEM",
	    "Couldn't install remote segment.",
	    s);
    }

    dv->dv_base = cvul64(ramdev_base);
    dv->dv_size = cvul64(ramdev_size);
    break;
  }

  default:
    return (do_diocntl(&m_dtab, m_ptr));
  }
#endif
  return (OK);
}

/*===========================================================================*
 *				m_geometry				     *
 *===========================================================================*/
PRIVATE void x_geometry(entry)
     struct partition *entry;
{
  printf("x_geometry\n");
#if 0
  /* Memory devices don't have a geometry, but the outside world insists. */
  entry->cylinders =
    div64u(m_geom[m_device].dv_size, SECTOR_SIZE) / (64 * 32);
  entry->heads = 64;
  entry->sectors = 32;
#endif
}

PRIVATE int x_hw_int(dp, m_ptr)
     struct driver *dp;
     message *m_ptr;
{
  printf("x_hw_int\n");
}
