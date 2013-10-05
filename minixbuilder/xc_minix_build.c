/******************************************************************************
 * xc_minix_build.c
 * derived from xc_plan9_build.c
 */

#include "xc_private.h"

#define DEBUG 1
#ifdef DEBUG
#define DPRINTF(x) printf x; fflush(stdout);
#else
#define DPRINTF(x)
#endif

/** use this instead of free. prevents double frees happening */
#define dfree(ptr) do { free(ptr) ; ptr = NULL; } while (0)

#include <unistd.h>
#include "minixa.out.h"
#include "minix.h"

/* for some reason this isn't being found in unistd */
extern ssize_t pread (int __fd, void *__buf, size_t __nbytes,
		      __off_t __offset);

void setup_codeseg(struct segdesc_s *gdt_ptr, unsigned long cs);
void setup_dataseg(struct segdesc_s *gdt_ptr, unsigned long ds);
void setup_seg(struct segdesc_s *gdt_ptr, int index, unsigned long base, unsigned long size);

/** Protection levels for pages */
#define L1_PROT (_PAGE_PRESENT|_PAGE_RW|_PAGE_ACCESSED)
#define L2_PROT (_PAGE_PRESENT|_PAGE_RW|_PAGE_ACCESSED|_PAGE_DIRTY|_PAGE_USER)

/** Constant defines where the virtual address space should start */
#define VIRT_ADDR_START 0x00000000

#define INITIAL_CODE_SEGMENT 1
#define INITIAL_DATA_SEGMENT 2


/**
 * Get the total number of pages allocated to a domain
 */
static long
get_tot_pages(int xc_handle, u32 domid)
{
  dom0_op_t op;
  op.cmd = DOM0_GETDOMAININFO;
  op.u.getdomaininfo.domain = (domid_t) domid;
  op.u.getdomaininfo.ctxt = NULL;
  return (do_dom0_op(xc_handle, &op) < 0) ?
    -1 : op.u.getdomaininfo.tot_pages;
}

/**
 * Get the frame number that should contain the shared_info structure
 */
static unsigned long
get_shared_info_frame(int xc_handle, u32 domid)
{
  dom0_op_t op;
  op.cmd = DOM0_GETDOMAININFO;
  op.u.getdomaininfo.domain = (domid_t) domid;
  op.u.getdomaininfo.ctxt = NULL;
  return (do_dom0_op(xc_handle, &op) < 0) ?
    -1 : op.u.getdomaininfo.shared_info_frame;
}

/**
   do this function properly later maybe
   if may not have any funtion within xen. from what i see it's used to
   load the harddrive task. maybe needed, for virtual block device,
   but until then, always return 1
*/
int
selected(char *name)
{
  return 1;
}

/**
   This function will need a bit of testing to get right
   @return the size of the process on disk
*/
u32
proc_size(image_header_t *hdr)
{
  /**
     Size occupied on disk by each process consists of image_header,
     text size and data size. this probably wont work. will need debugging
  */
  return align(hdr->process.a_text, SECTOR_SIZE)
    + align(hdr->process.a_data, SECTOR_SIZE);
}

/**
   Read the clicksize from the kernel image
*/
u16
get_clicksize(const int fd, off_t offset)
{
  u16 click_shift = 0;
  if (!pread(fd, &click_shift, sizeof(click_shift), offset + CLICK_OFF)) {
    PERROR("Error reading click shift");
    return 0;
  }
	
  if (click_shift < HCLICK_SHIFT || click_shift > 16) {
    PERROR("Bad clickshift");
    return 0;
  }
  printf("click_shift: %x\n", click_shift);
  return 1 << click_shift;
}

/**
   Read the boot flags from the kernel image
*/
u16
get_flags(const int fd, off_t offset)
{
  u16 flags = 0;
  if (!pread(fd, &flags, sizeof(flags), offset + FLAGS_OFF)) {
    PERROR("Error reading flags");
    return -1;
  }
	
  return flags;
}

/**
   Read the magic number from the kernel image
*/
u16
get_magic_number(const int fd, off_t offset)
{
  u16 magic = 0;
  if (!pread(fd, &magic, sizeof(magic), offset + FLAGS_OFF)) {
    PERROR("Error reading kernel magic number");
    return 0;
  }
	
  return magic;
}

/**
   Converts a page frame number to a virtual address.
   @see setup_page_tables()
*/
unsigned long
pfn_to_vaddr(int pfn)
{
  return VIRT_ADDR_START | (pfn << PAGE_SHIFT);
}

/**
   Converts a virtual address to a page frame number
*/
unsigned long
vaddr_to_pfn(int vaddr)
{
  return (vaddr & ~VIRT_ADDR_START) >> PAGE_SHIFT;
}

/**
   Process size in memory
   @return the amount of memory a process will occupy in memory
*/
static unsigned long
proc_size_in_memory(const image_header_t hdr, u16 click_size, u16 flags)
{
  unsigned long stack_size = 0;

  if (flags & K_CHMEM) {
    stack_size = hdr.process.a_total - (hdr.process.a_data
					+ hdr.process.a_bss);
    if (!(hdr.process.a_flags & A_SEP)) {
      stack_size -= hdr.process.a_text;
    }
  } 

  return	align(hdr.process.a_text, click_size)
    + align(hdr.process.a_data, click_size)
    + align(hdr.process.a_bss, click_size)
    + align(stack_size, click_size);
}
	
/**
   Copied data to the domain memory. Will always start to copy on
   a new page.
   @return return the virtual address of the first page used or -1 on error.
*/
static unsigned long
copy_to_domain_memory(int xc_handle, const u32 domid,
		      void *data, long size,
		      unsigned long *pages, const unsigned long nr_pages,
		      unsigned long *pg_alloc) {
  unsigned long virt_addr = 0, addr = 0;

  virt_addr = pfn_to_vaddr(*pg_alloc);
	
  while (size > 0) {
    addr = (unsigned long)xc_map_foreign_range(xc_handle, domid,
					       PAGE_SIZE, PROT_READ|PROT_WRITE,
					       pages[(*pg_alloc)++]);
    if (addr == 0) {
      PERROR("Error mapping process memory");
      virt_addr = -1;
      goto copy_error;
    }
    memcpy((void*)addr, data, (size > PAGE_SIZE) ? PAGE_SIZE : size);
    munmap((void*)addr, PAGE_SIZE);

    size -= PAGE_SIZE;
    data += PAGE_SIZE;
  }
 copy_error:
  return virt_addr;
}

/**
   Read minix kernel image specified by image_name into memory of new domain
   @return array of processes contained in kernel image
*/
static int
load_minix_image(int xc_handle, const u32 domid, const char *image_name,
		 unsigned long *pages, unsigned long *pg_alloc,
		 minix_start_info_t *msi)
{
  image_header_t hdr;
  int fd = 0, i = 0, banner = 0, ret = -1;
  off_t fileoffset = 0, image_size = 0;
  u16 click_size = 0, flags = 0, magic = 0;
  char *buf = NULL, *bufptr = NULL;
  unsigned long base = 0, size_in_mem = 0;
  unsigned long text_offset = 0, data_offset = 0, bss_offset = 0, stack_size = 0;
  image_header_t *headers = msi->setup_info.procheaders;
  process_t *process_list = msi->setup_info.processes;
	
  if (!(fd = open(image_name, 0))) { /* no flags on creation */
    PERROR("Error opening image file");
    goto load_error;
  }

  /* get size of image file */
  image_size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);

  /**
     Loop through all processes in image file
     and load them into memory
  */
  for (i = 0; fileoffset < image_size; i++) {
    DPRINTF(("Loading image %i\n", i));
    if (i == PROCESS_MAX) {
      PERROR("Too many processes in image file");
      goto load_error;
    }

    /**
       read in headers. dont stop until a good one is found
       then proceed
    */
    while (1) {
      if (!pread(fd, (void*)&hdr, sizeof(hdr), fileoffset)) {
	PERROR("Error reading header");
	goto load_error;
      }

      /** header has been read, so advance offset */
      fileoffset += align(sizeof(image_header_t), SECTOR_SIZE);
			
      /* check the magic number of the process, and dont use a bad one */
      if (CHECKMAGIC(hdr.process)) {
	PERROR("Bad Magic number on executable");
	goto load_error;
      }

      /* check if we are to use this process. if not skip */
      if (selected(hdr.name)) {
	break;
      }

      /* bad process, skip it */
      fileoffset += proc_size(&hdr);
    }

    /* make sure we're trying to run on the correct arch.
       Xen only runs on 386 machines so we can only use a
       386 minix image
    */
    DPRINTF(("Checking image to make sure it is of the correct type\n"));
    if (hdr.process.a_cpu != A_I80386) {
      PERROR("Bad kernel image. Not for 386 architecture");
      goto load_error;
    }

		
    /** get clickshift, flags and magic number */
    if (i == KERNEL) {
      if ((click_size = get_clicksize(fd, fileoffset)) == 0)
	goto load_error;
      if ((flags = get_flags(fd, fileoffset)) == (u16)-1)
	goto load_error;
      if ((magic = get_magic_number(fd, fileoffset)) == 0)
	goto load_error;
    }

    DPRINTF(("Allocating memory for image buffer\n"));
    size_in_mem = proc_size_in_memory(hdr, click_size, flags);

    buf = (char*)malloc(size_in_mem);
    if (buf == NULL) { 
      PERROR("Error allocating memory for process\n"); 
      goto load_error; 
    }
    bufptr = buf; 

    DPRINTF(("Reading image into buffer"));
    /** copy text into buf */
    text_offset = 0; 
    if (!pread(fd, bufptr, hdr.process.a_text, fileoffset)) { 
      PERROR("Error reading in text section\n"); 
      goto load_error; 
    }
		
    fileoffset += align(hdr.process.a_text, SECTOR_SIZE); 
    bufptr += align(hdr.process.a_text, click_size); 

    /** copy data into buf */
    data_offset = bufptr - buf; 
    if (!pread(fd, bufptr, hdr.process.a_data, fileoffset)) { 
      PERROR("Error reading in data section\n"); 
      goto load_error; 
    }
    fileoffset += align(hdr.process.a_data, SECTOR_SIZE); 
    bufptr += align(hdr.process.a_data, click_size); 

    /** zero out the bss */ 
    bss_offset = bufptr - buf; 
    memset(bufptr, 0, hdr.process.a_bss); 
    bufptr += align(hdr.process.a_bss, click_size); 

    DPRINTF(("Copying the buffer to the domain memory\n"));
    base = copy_to_domain_memory(xc_handle, domid, buf, 
				 size_in_mem, 
				 pages, msi->start_info.nr_pages, 
				 pg_alloc); 
    dfree(buf);

    if (base == -1) {
      PERROR("Error copying buf to domain memory\n");
      goto load_error;
    }

    process_list[i].entry = hdr.process.a_entry;
    process_list[i].cs = base + text_offset;
    process_list[i].ds = base + data_offset;
    process_list[i].data = base + data_offset;
    process_list[i].end = base + hdr.process.a_total;

    /** copy header into memory */
    DPRINTF(("Updating header object with correct values\n"))
      hdr.process.a_syms = base;
    memcpy(&headers[i], &hdr, sizeof(image_header_t));
		
    /** print process info for the user's benefit */
    if (!banner) {
      printf("     cs       ds     text     data      bss");
      if (flags & K_CHMEM) printf("    stack");
      putchar('\n');
      banner = 1;
    }		

    if (flags & K_CHMEM) {
      stack_size = hdr.process.a_total - hdr.process.a_data
	- hdr.process.a_bss;
      if (!(hdr.process.a_flags & A_SEP)) stack_size -= hdr.process.a_text;
    } else {
      stack_size = 0;
    }
		
    printf("%07lx  %07lx %8ld %8ld %8ld",
	   (unsigned long)process_list[i].cs,
	   (unsigned long)process_list[i].ds,
	   hdr.process.a_text, hdr.process.a_data,
	   hdr.process.a_bss
	   );
    if (flags & K_CHMEM) printf(" %8ld", stack_size);
    printf("  %s\n", hdr.name);
  }
	
  /* success! ret is set as such */
  ret = 0;
	
 load_error:
  /**
     something has gone wrong. cleanup
  */       
  if (fd) {
    close(fd);
  }
  dfree(buf);
		
  return ret;
}

/**
 * Load the list of page frames that have been allocated to this VM
 */
static int get_pfn_list(int xc_handle,
                        u32 domid, 
                        unsigned long *pfn_buf, 
                        unsigned long max_pfns)
{
  dom0_op_t op;
  int ret;
  op.cmd = DOM0_GETMEMLIST;
  op.u.getmemlist.domain   = (domid_t)domid;
  op.u.getmemlist.max_pfns = max_pfns;
  op.u.getmemlist.buffer   = pfn_buf;
	
  if ( mlock(pfn_buf, max_pfns * sizeof(unsigned long)) != 0 )
    return -1;
	
  ret = do_dom0_op(xc_handle, &op);
	
  (void)munlock(pfn_buf, max_pfns * sizeof(unsigned long));
	
  return (ret < 0) ? -1 : op.u.getmemlist.num_pfns;
}

/**
   Setup page tables for minix. Page table for minix must be a
   1-1 mapping of page table entries(pte) to page frames. This is
   so the hardware will never fire a page fault, because minix hasnt
   a clue in hell how to handle that.
   The virtual address space must start from 64M onwards as the first
   64M is reserved by xen.
   This function also sets up the GDT frames for minix as it can't do it
   itself (no notion of page tables)
**/
static int
setup_page_table(int xc_handle, u32 domid,
		 unsigned long *pages, unsigned long *pg_alloc,
		 unsigned long *pt_frame,
		 minix_start_info_t *msi)
{
  int i = 0;
  unsigned long va_start = VIRT_ADDR_START;
  unsigned long first_non_kernel = 0;
	
  /* variables needed for pagetable setup */
  l1_pgentry_t *pg_tab=NULL, *pg_tab_entry=NULL;
  l2_pgentry_t *pg_dir=NULL, *pg_dir_entry=NULL;
  unsigned long pg_dir_frame = 0, pg_tab_frame = 0;
  unsigned long first_pg_alloc = 0; 
  unsigned long *ptm_map, *ptm_map_e = NULL;
  unsigned long first_gdt_pfn = 0;
  unsigned long shared_info_frame = get_shared_info_frame(xc_handle, domid);
  unsigned long *gdt_ptr = NULL;
  mmu_t *mmu = NULL;

  image_header_t *first_non_kernel_header = &msi->setup_info.procheaders[KERNEL+1];
  first_non_kernel = vaddr_to_pfn(first_non_kernel_header->process.a_syms);
	
  DPRINTF(("First non kernel page: %lx\n", first_non_kernel));
  /*
    work out how many page frames will be needed to hold
    page tables

    1 is for page required for page dir. Theres only ever one page dir.
  */
  msi->start_info.nr_pt_frames = 1 +
    (sizeof(l1_pgentry_t)*msi->start_info.nr_pages + PAGE_SIZE - 1)/PAGE_SIZE + 1;
	
  if ( (mmu = init_mmu_updates(xc_handle, domid)) == NULL ) {
    PERROR("Could not initialise mmu");
    goto error_out;
  }
	
  /** initialise the gdt frames */
  msi->setup_info.gdt_vaddr = pfn_to_vaddr(*pg_alloc);
  first_gdt_pfn = *pg_alloc;
  for (i = 0; i < NR_GDT_MF; i++) {
    msi->setup_info.gdt_mfns[i] = pages[(*pg_alloc)++];
    if ((gdt_ptr = xc_map_foreign_range(xc_handle, domid, PAGE_SIZE,
					PROT_READ|PROT_WRITE,
					msi->setup_info.gdt_mfns[i])) == NULL) {
      PERROR("Error mapping GDT frame");
      goto error_out;
    }
    memset(gdt_ptr, 0x00000000, PAGE_SIZE);
    munmap(gdt_ptr, PAGE_SIZE);
  }
	
  /* Allocate page to act as page dir */
  first_pg_alloc = *pg_alloc;
  msi->start_info.pt_base = pfn_to_vaddr(*pg_alloc);
  pg_dir_frame = pages[(*pg_alloc)++] << PAGE_SHIFT;
  *pt_frame = pg_dir_frame;
	
  /* initialise page dir */
  if ( (pg_dir = xc_map_foreign_range(xc_handle, domid, PAGE_SIZE, PROT_READ|PROT_WRITE,
				      pg_dir_frame >> PAGE_SHIFT)) == NULL) {
    PERROR("Error mapping directory frame");
    goto error_out;
  }
  memset(pg_dir, 0, PAGE_SIZE);

  pg_dir_entry = &pg_dir[l2_table_offset(va_start)];

  /**
     Loop through all the pages that have been allocated to me.
     put them all in a 2 level page table structure.
  */
  for (i = 0; i < msi->start_info.nr_pages; i++) {
    /**
       If the current page table entry needs a new entry in the
       page dir, initialise that page dir */
    if ( ((unsigned long)pg_tab_entry & (PAGE_SIZE-1)) == 0 ) {
      pg_tab_frame = pages[(*pg_alloc)++] << PAGE_SHIFT;
      if (pg_tab != NULL) {
	munmap(pg_tab, PAGE_SIZE);
      }

      if ( (pg_tab = xc_map_foreign_range(xc_handle, domid, PAGE_SIZE,
					  PROT_READ|PROT_WRITE,
					  pg_tab_frame >> PAGE_SHIFT)) == NULL) {
	munmap(pg_dir, PAGE_SIZE);
	PERROR("Error mapping page table frame");
	goto error_out;
      }

      memset(pg_tab, 0, PAGE_SIZE);

      pg_tab_entry = &pg_tab[l1_table_offset(va_start + (i << PAGE_SHIFT))];
      *pg_dir_entry++ = pg_tab_frame | L2_PROT;
    }

    *pg_tab_entry = (pages[i] << PAGE_SHIFT) | L1_PROT;
    if (i >= first_non_kernel) {
      *pg_tab_entry |= _PAGE_USER;
    }

    /* sets the frames containing page table as read only
       also contains page_dir and gdt as i slipped them in there */
    if ((i >= first_pg_alloc && i < (first_pg_alloc + msi->start_info.nr_pt_frames))
	|| ((i >= first_gdt_pfn && i <= first_gdt_pfn + NR_GDT_MF))) {
      *pg_tab_entry &= ~_PAGE_RW;
    }
    pg_tab_entry++;
  }

  printf("Last usable page: %lx\n", pfn_to_vaddr(i));
	
  /**
   * place the shared info in accessible memory
   */
  msi->shared_info = (shared_info_t *)FIXED_SHARED_INFO;
  pg_dir_entry = &pg_dir[l2_table_offset(FIXED_SHARED_INFO)];
  pg_tab_frame = pages[(*pg_alloc)++] << PAGE_SHIFT;
  if (pg_tab != NULL) {
    munmap(pg_tab, PAGE_SIZE);
  }
  if ( (pg_tab = xc_map_foreign_range(xc_handle, domid, PAGE_SIZE,
				      PROT_READ|PROT_WRITE,
				      pg_tab_frame >> PAGE_SHIFT)) == NULL) {
    munmap(pg_dir, PAGE_SIZE);
    PERROR("Error mapping page table frame");
    goto error_out;
  }
  memset(pg_tab, 0, PAGE_SIZE);

	
  pg_tab_entry = &pg_tab[l1_table_offset(FIXED_SHARED_INFO)];
  *pg_dir_entry = pg_tab_frame | L2_PROT | _PAGE_USER;
  *pg_tab_entry =	(shared_info_frame << PAGE_SHIFT) | L1_PROT;

  munmap(pg_tab, PAGE_SIZE);
  munmap(pg_dir, PAGE_SIZE);

  /**
     pin down page dir as page dir, so hypervisor protects it correctly
  */ 
  if (add_mmu_update(xc_handle, mmu,
		     pg_dir_frame | MMU_EXTENDED_COMMAND, MMUEXT_PIN_L2_TABLE)) {
    PERROR("Error pinning down page dir frame");
    goto error_out;
  }

  /** build phys->machine and machine->map maps */
  msi->start_info.mfn_list = pfn_to_vaddr(*pg_alloc);
  ptm_map = ptm_map_e = xc_map_foreign_range(xc_handle, domid, PAGE_SIZE,
					     PROT_READ|PROT_WRITE, pages[(*pg_alloc)++]);

  for (i = 0; i < (XEN_DESC_LIMIT >> PAGE_SHIFT); i++) {
    if (i < msi->start_info.nr_pages) {
      if (add_mmu_update(xc_handle, mmu,
			 (pages[i] << PAGE_SHIFT) |
			 MMU_MACHPHYS_UPDATE, i)) {
	PERROR("Error updating phy-mach mapping, fail on page %d", i);
	munmap(ptm_map, PAGE_SIZE);
	goto error_out;
      }
      *ptm_map_e++ = pages[i];
    } else if (i <= (FIXED_SHARED_INFO >> PAGE_SHIFT) + 40) {
      if (add_mmu_update(xc_handle, mmu,
			 (shared_info_frame << PAGE_SHIFT) |
			 MMU_MACHPHYS_UPDATE, FIXED_SHARED_INFO >> PAGE_SHIFT)) {
	PERROR("Error updating phy-mach mapping, fail on page %d", i);
	munmap(ptm_map, PAGE_SIZE);
	goto error_out;
      }
      *ptm_map_e++ = shared_info_frame;
    } else {
      *ptm_map_e++ = 0x0;
    }
			
    if (((unsigned long)ptm_map_e & (PAGE_SIZE-1)) == 0) {
      munmap(ptm_map, PAGE_SIZE);
      ptm_map = ptm_map_e = xc_map_foreign_range(xc_handle, domid, PAGE_SIZE,
						 PROT_READ|PROT_WRITE,
						 pages[(*pg_alloc)++]);
    }
  }
  munmap(ptm_map, PAGE_SIZE);

  /**
     Cleanup up mmu and finish updates
  */
  if (finish_mmu_updates(xc_handle, mmu)) {
    PERROR("Couldn't finish mmu updates");
    goto error_out;
  }
  dfree(mmu);
	
  return 0;
 error_out:
  dfree(mmu);
	
  return -1;
}

/**
   Setup start info structure
   This must be after all other pages are allocated, because the start of freemem
   is calculated here.
*/
static int
setup_start_info(int xc_handle, u32 domid, unsigned long *pages,
		 unsigned long *pg_alloc, minix_start_info_t *msi)
{
  int i = 0;
  shared_info_t *shared_info;
  unsigned long shared_info_frame = get_shared_info_frame(xc_handle, domid);
  unsigned long msi_vaddr;

  msi->start_info.shared_info  = shared_info_frame << PAGE_SHIFT; /* machine addr */
  /* shared_info page starts its life empty. */
  shared_info = xc_map_foreign_range(
				     xc_handle, domid, PAGE_SIZE, PROT_READ|PROT_WRITE, shared_info_frame);
  if (shared_info == NULL) {
    PERROR("Error mapping shared info struct");
    return -1;
  }
  memset(shared_info, 0, sizeof(shared_info_t));
  /* Mask all upcalls... */
  for ( i = 0; i < MAX_VIRT_CPUS; i++ )
    shared_info->vcpu_data[i].evtchn_upcall_mask = 1;
  munmap(shared_info, PAGE_SIZE);

  msi->setup_info.msi_vaddr = pfn_to_vaddr(*pg_alloc);
  msi->setup_info.fmem_vaddr = pfn_to_vaddr(*pg_alloc+KERNEL_STACK_PAGES);
	
  msi_vaddr = copy_to_domain_memory(xc_handle, domid, msi, sizeof(minix_start_info_t),
				    pages, msi->start_info.nr_pages, pg_alloc);
  if (msi->setup_info.msi_vaddr != msi_vaddr) {
    PERROR("minix_start_info is not where it was expected. Aborting.");
    return -1;
  }
  return 0;
}

/**
 * Sets up the default code and data segments of the kernel.
 * A lot of the descriptor code is nicked from protect.c in minix.
 */
void
build_seg(struct segdesc_s *gdt_ptr, int index, unsigned long base, unsigned long size)
{
  gdt_ptr[index].base_low = base;
  gdt_ptr[index].base_middle = base >> BASE_MIDDLE_SHIFT;
  gdt_ptr[index].base_high = base >> BASE_HIGH_SHIFT;

  --size;	  /* convert to a limit, 0 size means 4G */
  if (size > BYTE_GRAN_MAX) {
    gdt_ptr[index].limit_low = size >> PAGE_GRAN_SHIFT;
    gdt_ptr[index].granularity = GRANULAR | (size >>
					     (PAGE_GRAN_SHIFT + GRANULARITY_SHIFT));
  } else {
    gdt_ptr[index].limit_low = size;
    gdt_ptr[index].granularity = size >> GRANULARITY_SHIFT;
  }
  gdt_ptr[index].granularity |= DEFAULT;	/* means BIG for data seg */
}

static int
setup_gdt(int xc_handle, u32 domid, minix_start_info_t *msi)
{
  struct segdesc_s *gdt_ptr;

  if ((gdt_ptr = xc_map_foreign_range(xc_handle, domid, PAGE_SIZE,
				      PROT_READ|PROT_WRITE,
				      msi->setup_info.gdt_mfns[0])) == NULL) {
    PERROR("Error mapping GDT frame");
    goto error_out;
  }

  build_seg(gdt_ptr, CS_INDEX,
	    msi->setup_info.processes[KERNEL].cs,
	    msi->setup_info.fmem_vaddr - (msi->setup_info.processes[KERNEL].cs) - 1);
  gdt_ptr[CS_INDEX].access = (INTR_PRIVILEGE << DPL_SHIFT)
    | (PRESENT | SEGMENT | EXECUTABLE | READABLE);

	
  build_seg(gdt_ptr, DS_INDEX,
	    msi->setup_info.processes[KERNEL].ds, 0); 
  //msi->setup_info.fmem_vaddr - (msi->setup_info.processes[KERNEL].ds) - 1);
  gdt_ptr[DS_INDEX].access = (INTR_PRIVILEGE << DPL_SHIFT) | (PRESENT | SEGMENT | WRITEABLE);

  munmap(gdt_ptr, PAGE_SIZE);
  return 0;
	
 error_out:
  return -1;
}


/**
 * Builds a minix domain
 * - Loads image into memory
 * - Sets up page tables
 * - Sets up start_info structure
 * - Sets up GDT
 * - Executes domain
 */
int
xc_minix_build(int xc_handle,
	       u32 domid,
	       const char *image_name,
	       const char *cmdline,
	       unsigned int control_evtchn, unsigned long flags)
{
  dom0_op_t launch_op, op;
  int rc, i;
  full_execution_context_t st_ctxt, *ctxt = &st_ctxt;
	
  unsigned long *pages = NULL;
  unsigned long pt_frame = 0, pg_alloc = 0;

  minix_start_info_t msi;
  memset(&msi, 0, sizeof(msi));

  /* get the number of page frames allocated to this domain */
  if ( (msi.start_info.nr_pages = get_tot_pages(xc_handle, domid)) < 0 ) {
    PERROR("Could not find total pages for domain");
    goto error_out;
  }

  /**
     page array needs its space allocated before it can be filled
  */
  if ( (pages = malloc(msi.start_info.nr_pages * sizeof(unsigned long))) == NULL) {
    PERROR("Could not allocate memory");
    goto error_out;
  }

  /**
     read in array of page frames
  */
  if (get_pfn_list(xc_handle, domid, pages, msi.start_info.nr_pages)
      != msi.start_info.nr_pages) {
    PERROR("Could not get the page frame list");
    goto error_out;
  }

  /**
     Load array of processes into domain memory
  */
  if ( (load_minix_image(xc_handle, domid, image_name,
			 pages, &pg_alloc, &msi)) != 0) {
    PERROR("Invalid kernel image");
    goto error_out;
  }

  /**
     Setup page table
  */
  if (setup_page_table(xc_handle, domid, pages, &pg_alloc, &pt_frame, &msi) != 0) {
    PERROR("Error setting up page tables");
    goto error_out;
  }

  /**
     Setup the start info and shared info structures.
     pg_alloc is incremented as pages are used again.
  */
  msi.start_info.flags = flags;
  msi.start_info.domain_controller_evtchn = control_evtchn;
  strncpy((char*)msi.start_info.cmd_line, cmdline, MAX_CMDLINE);
  msi.start_info.cmd_line[MAX_CMDLINE-1] = '\0';

  if (setup_start_info(xc_handle, domid, pages, &pg_alloc, &msi) != 0) {
    PERROR("Error setting up start info structure");
    goto error_out;
  }

  /**
     Setup gdt
  */
  if (setup_gdt(xc_handle, domid, &msi) != 0) {
    PERROR("Error setting up the default GDT");
    goto error_out;
  }
	
  if ( mlock(&st_ctxt, sizeof(st_ctxt) ) ) {   
    PERROR("Unable to mlock ctxt");
    goto error_out;
  }
	
  op.cmd = DOM0_GETDOMAININFO;
  op.u.getdomaininfo.domain = (domid_t)domid;
  op.u.getdomaininfo.ctxt = ctxt;
  if ( (do_dom0_op(xc_handle, &op) < 0) || 
       ((u16)op.u.getdomaininfo.domain != domid) ) {
    PERROR("Could not get info on domain");
    goto error_out;
  }

  if ( !(op.u.getdomaininfo.flags & DOMFLAGS_PAUSED) ||
       (ctxt->pt_base != 0) ) {
    ERROR("Domain is already constructed");
    goto error_out;
  }

  ctxt->flags = 0;
  ctxt->pt_base = pt_frame;
  /*
   * Initial register values:
   *  DS,ES,FS,GS = FLAT_GUESTOS_DS
   *       CS:EIP = FLAT_GUESTOS_CS:start_pc
   *       SS:ESP = FLAT_GUESTOS_DS:start_stack
   *          ESI = start_info
   *       [EAX, EBX, ECX,EDX,EDI,EBP are zero]
   *       EFLAGS = IF | 2 (bit 1 is reserved and should always be 1)
   */
  ctxt->cpu_ctxt.ds = FLAT_GUESTOS_DS;
  ctxt->cpu_ctxt.es = FLAT_GUESTOS_DS;
  ctxt->cpu_ctxt.fs = FLAT_GUESTOS_DS;
  ctxt->cpu_ctxt.gs = FLAT_GUESTOS_DS;
  ctxt->cpu_ctxt.ss = FLAT_GUESTOS_DS;
  ctxt->cpu_ctxt.cs = FLAT_GUESTOS_CS;
  ctxt->cpu_ctxt.eip = msi.setup_info.processes[KERNEL].cs
    + msi.setup_info.processes[KERNEL].entry;

  ctxt->cpu_ctxt.esp = msi.setup_info.msi_vaddr + KERNEL_STACK_SIZE;
  ctxt->cpu_ctxt.esi = msi.setup_info.msi_vaddr;
  ctxt->cpu_ctxt.eflags = (1<<9) | (1<<2);
	
  /* FPU is set up to default initial state. */
  memset(ctxt->fpu_ctxt, 0, sizeof(ctxt->fpu_ctxt));
	
  /* Virtual IDT is empty at start-of-day. */
  for ( i = 0; i < 256; i++ )
    {
      ctxt->trap_ctxt[i].vector = i;
      ctxt->trap_ctxt[i].cs     = FLAT_GUESTOS_CS;
    }
  ctxt->fast_trap_idx = 0;
	
  /* No LDT. */
  ctxt->ldt_ents = 0;
	
  /* Use the default Xen-provided GDT. */
  ctxt->gdt_ents = 0;
	
  /* Ring 1 stack is the initial stack. */
  ctxt->guestos_ss  = FLAT_GUESTOS_DS;
  ctxt->guestos_esp = msi.setup_info.msi_vaddr + KERNEL_STACK_SIZE;

  /* No debugging. */
  memset(ctxt->debugreg, 0, sizeof(ctxt->debugreg));
	
  /* No callback handlers. */
  ctxt->event_callback_cs     = FLAT_GUESTOS_CS;
  ctxt->event_callback_eip    = 0;
  ctxt->failsafe_callback_cs  = FLAT_GUESTOS_CS;
  ctxt->failsafe_callback_eip = 0;

  memset( &launch_op, 0, sizeof(launch_op) );
	
  launch_op.u.builddomain.domain   = (domid_t)domid;
  launch_op.u.builddomain.ctxt = ctxt;

  launch_op.cmd = DOM0_BUILDDOMAIN;

  DPRINTF(("***CPU Context***\n"));
  DPRINTF(("CmdLine: %s\n", cmdline));
  DPRINTF(("EAX: %lx\t", ctxt->cpu_ctxt.eax));
  DPRINTF(("EBX: %lx\t", ctxt->cpu_ctxt.ebx));
  DPRINTF(("ECX: %lx\t", ctxt->cpu_ctxt.ecx));
  DPRINTF(("EDX: %lx\n", ctxt->cpu_ctxt.edx));
  DPRINTF(("EBP: %lx\t", ctxt->cpu_ctxt.ebp));
  DPRINTF(("ESI: %lx\t", ctxt->cpu_ctxt.esi));
  DPRINTF(("ESP: %lx\t", ctxt->cpu_ctxt.esp));
  DPRINTF(("EDI: %lx\n", ctxt->cpu_ctxt.edi));
  DPRINTF(("EFLAGS: %lx\t", ctxt->cpu_ctxt.eflags));
  DPRINTF(("_UNUSED: %lx\n", ctxt->cpu_ctxt._unused));

  DPRINTF(("EIP: %lx\t", ctxt->cpu_ctxt.eip));
  DPRINTF(("CS: %lx\n", ctxt->cpu_ctxt.cs));

  DPRINTF(("SS: %lx\t", ctxt->cpu_ctxt.ss));
  DPRINTF(("ES: %lx\t", ctxt->cpu_ctxt.es));
  DPRINTF(("DS: %lx\t", ctxt->cpu_ctxt.ds));
  DPRINTF(("FS: %lx\t", ctxt->cpu_ctxt.fs));
  DPRINTF(("GS: %lx\n", ctxt->cpu_ctxt.gs));
  DPRINTF(("***End CPU Context***\n"));

  rc = do_dom0_op(xc_handle, &launch_op);

  dfree(pages);
	
  return rc;
 error_out:
  dfree(pages);
				
  return -1;
}

