#ifndef MINIXBUILD_H
#define MINIXBUILD_H

#include <xen/xen.h>
#include <xenctrl.h>
#include <xs.h>

#define MAX_UUID 64
#define MAX_CHAINLOADER 1024
#define MAX_KERNEL 1024
#define MAX_RAMDISK 1024
#define MAX_FEATURES 1024



typedef struct {
  /* structure for communicating with xen */
  int xen_handle;
  struct xs_handle *xen_store;
  xs_transaction_t xen_store_transaction;

  /* information about the domain to create */
  int domid;
  char uuid[MAX_UUID];
  xc_dominfo_t dominfo;

  /* information about the chainloader */
  char chainloader[MAX_CHAINLOADER];
  char kernel[MAX_KERNEL];
  int store_evtchn;
  int store_mfn;
  int console_evtchn;
  int console_mfn;
  char cmdline[MAX_GUEST_CMDLINE];
  char ramdisk[MAX_RAMDISK];
  int vcpus;
  char features[MAX_FEATURES];
} minixbuild_ctxt_t;

void cleanup(minixbuild_ctxt_t*, int);

#endif
