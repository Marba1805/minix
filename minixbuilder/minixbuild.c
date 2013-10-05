#include <xenctrl.h>
#include <xs.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>	
#include "minixbuild.h"

#define DPRINTF(x) { printf("%s -> %s\n", #x, x); }
#define DPRINTFI(x) { printf("%s -> %d\n", #x, x); }

void get_value(minixbuild_ctxt_t *ctxt, const char *name, char *dest, int len)
{
  char buf[200];
  char *bufptr;
  int num;

  snprintf(buf, 200, "/vm/%s/chainloader/%s", ctxt->uuid, name);
  bufptr = (char*)xs_read(ctxt->xen_store,
			  ctxt->xen_store_transaction,
			  (const char*)buf, &num);
  if (bufptr == NULL) {
    fprintf(stderr, "Error reading value for '%s' from xen store\n.", buf);
    cleanup(ctxt, ENOENT);
  }

  strncpy(dest, bufptr, len);
  free(bufptr);
}

int get_info(minixbuild_ctxt_t *ctxt)
{
  int ret;
  char buf[100];
	
  ret = xc_domain_getinfo(ctxt->xen_handle, ctxt->domid, 1, &ctxt->dominfo);
  if (ret != 1) {
    fprintf(stderr, "Couldn't retrieve domain information\n");
    cleanup(ctxt, EINVAL);
  }

  get_value(ctxt, "chainloader", ctxt->chainloader, MAX_CHAINLOADER);
  get_value(ctxt, "kernel", ctxt->kernel, MAX_KERNEL);
  get_value(ctxt, "store_evtchn", buf, 100);
  ctxt->store_evtchn = atoi(buf);
  get_value(ctxt, "console_evtchn", buf, 100);
  ctxt->console_evtchn = atoi(buf);
  get_value(ctxt, "cmdline", ctxt->cmdline, MAX_GUEST_CMDLINE);
  get_value(ctxt, "ramdisk", ctxt->ramdisk, MAX_RAMDISK);
  get_value(ctxt, "vcpus", buf, 100);
  ctxt->vcpus = atoi(buf);
  get_value(ctxt, "features", ctxt->features, MAX_FEATURES);

  DPRINTF(ctxt->chainloader);
  DPRINTF(ctxt->kernel);
  DPRINTFI(ctxt->store_evtchn);
  DPRINTFI(ctxt->console_evtchn);
  DPRINTF(ctxt->cmdline);
  DPRINTF(ctxt->ramdisk);
  DPRINTFI(ctxt->vcpus);
  DPRINTF(ctxt->features);
}

int main(int argc, const char *argv[])
{
  int ret, i;
  minixbuild_ctxt_t ctxt;
  int num;
  char **entries = NULL;
  char *vmpath;
  char buf[100];

  memset(&ctxt, 0, sizeof(minixbuild_ctxt_t));
	
  /* put check here that it is being called from xend */
  if (argc != 3) {
    fprintf(stderr, "Wrong number of arguments.\nUsage: %s <domid> <uuid>\n", argv[0]);
    exit(EINVAL);
  }
  ctxt.domid = atoi(argv[1]);
  strncpy(ctxt.uuid, argv[2], MAX_UUID);
	
  ctxt.xen_handle = xc_interface_open();
  if (ctxt.xen_handle == -1) {
    fprintf(stderr, "Couldn't connect to hypervisor\n");
    cleanup(&ctxt, ECONNREFUSED);
  }

  ctxt.xen_store = xs_daemon_open();
  if (ctxt.xen_store == NULL) {
    fprintf(stderr, "Couldn't connect to the xen store\n");
    cleanup(&ctxt, ECONNREFUSED);
  }
  ctxt.xen_store_transaction = xs_transaction_start(ctxt.xen_store);
  if (ctxt.xen_store_transaction == 0) {
    fprintf(stderr, "Couldn't start xen store transaction\n");
    cleanup(&ctxt, EAGAIN);
  }

  get_info(&ctxt);
	
  cleanup(&ctxt, 0);
}

void cleanup(minixbuild_ctxt_t *ctxt, int err)
{
  int ret;
	
  if (ctxt->xen_handle != 0) {
    xc_interface_close(ctxt->xen_handle);
    ctxt->xen_handle = 0;
  }

  if (ctxt->xen_store_transaction != 0) {
    xs_transaction_end(ctxt->xen_store, ctxt->xen_store_transaction,
		       false);
    ctxt->xen_store_transaction = 0;
  }
	
  if (ctxt->xen_store != NULL) {
    xs_daemon_close(ctxt->xen_store);
    ctxt->xen_store = NULL;
  }

  //	exit(err);
  exit(-1);
}
