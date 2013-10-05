#include "minixbuild.h"
#include <string.h>

void ctxt_init(minixbuild_ctxt_t *ctxt)
{
  ctxt->xen_handle = 0;
  ctxt->xen_store = NULL;
  ctxt->xen_store_transaction = 0;

  ctxt->domid = -1;
  memset(ctxt->uuid, 0, MAX_UUID);
  memset(ctxt->dominfo, 0, sizeof(xc_dominfo_t));
	
}

	
