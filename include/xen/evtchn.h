/******************************************************************************
 * event_channel.h
 * 
 * Event channels between domains.
 * 
 * Copyright (c) 2003-2004, K A Fraser.
 */

#ifndef EVTCHN_H
#define EVTCHN_H

#include <xen/xen.h>

/*
 * EVTCHNOP_alloc_unbound: Allocate a fresh local port and prepare
 * it for binding to <dom>.
 */
#define EVTCHNOP_alloc_unbound    6
typedef struct {
  /* IN parameters */
  domid_t dom;		/*  0 */
  u16_t __pad;
  /* OUT parameters */
  u32_t port;		/*  4 */
} evtchn_alloc_unbound_t;	/* 8 bytes */

/*
 * EVTCHNOP_bind_interdomain: Construct an interdomain event channel between
 * <dom1> and <dom2>. Either <port1> or <port2> may be wildcarded by setting to
 * zero. On successful return both <port1> and <port2> are filled in and
 * <dom1,port1> is fully bound to <dom2,port2>.
 * 
 * NOTES:
 *  1. A wildcarded port is allocated from the relevant domain's free list
 *     (i.e., some port that was previously EVTCHNSTAT_closed). However, if the
 *     remote port pair is already fully bound then a port is not allocated,
 *     and instead the existing local port is returned to the caller.
 *  2. If the caller is unprivileged then <dom1> must be DOMID_SELF.
 *  3. If the caller is unprivileged and <dom2,port2> is EVTCHNSTAT_closed
 *     then <dom2> must be DOMID_SELF.
 *  4. If either port is already bound then it must be bound to the other
 *     specified domain and port (if not wildcarded).
 *  5. If either port is awaiting binding (EVTCHNSTAT_unbound) then it must
 *     be awaiting binding to the other domain, and the other port pair must
 *     be closed or unbound.
 */
#define EVTCHNOP_bind_interdomain 0
typedef struct {
  /* IN parameters. */
  domid_t dom1, dom2;	/*  0,  2 */
  /* IN/OUT parameters. */
  u32_t port1, port2;	/*  4,  8 */
} evtchn_bind_interdomain_t;	/* 12 bytes */

/*
 * EVTCHNOP_bind_virq: Bind a local event channel to IRQ <irq>.
 * NOTES:
 *  1. A virtual IRQ may be bound to at most one event channel per domain.
 */
#define EVTCHNOP_bind_virq        1
typedef struct {
  /* IN parameters. */
  u32_t virq;		/*  0 */
  /* OUT parameters. */
  u32_t port;		/*  4 */
} evtchn_bind_virq_t;		/* 8 bytes */

/*
 * EVTCHNOP_bind_pirq: Bind a local event channel to IRQ <irq>.
 * NOTES:
 *  1. A physical IRQ may be bound to at most one event channel per domain.
 *  2. Only a sufficiently-privileged domain may bind to a physical IRQ.
 */
#define EVTCHNOP_bind_pirq        2
typedef struct {
  /* IN parameters. */
  u32_t pirq;		/*  0 */
#define BIND_PIRQ__WILL_SHARE 1
  u32_t flags;		/* BIND_PIRQ__* *//*  4 */
  /* OUT parameters. */
  u32_t port;		/*  8 */
} evtchn_bind_pirq_t;		/* 12 bytes */

/*
 * EVTCHNOP_close: Close the communication channel which has an endpoint at
 * <dom, port>. If the channel is interdomain then the remote end is placed in
 * the unbound state (EVTCHNSTAT_unbound), awaiting a new connection.
 * NOTES:
 *  1. <dom> may be specified as DOMID_SELF.
 *  2. Only a sufficiently-privileged domain may close an event channel
 *     for which <dom> is not DOMID_SELF.
 */
#define EVTCHNOP_close            3
typedef struct {
  /* IN parameters. */
  domid_t dom;		/*  0 */
  u16_t __pad;
  u32_t port;		/*  4 */
  /* No OUT parameters. */
} evtchn_close_t;		/* 8 bytes */

/*
 * EVTCHNOP_send: Send an event to the remote end of the channel whose local
 * endpoint is <DOMID_SELF, local_port>.
 */
#define EVTCHNOP_send             4
typedef struct {
  /* IN parameters. */
  u32_t local_port;	/*  0 */
  /* No OUT parameters. */
} evtchn_send_t;		/* 4 bytes */

/*
 * EVTCHNOP_status: Get the current status of the communication channel which
 * has an endpoint at <dom, port>.
 * NOTES:
 *  1. <dom> may be specified as DOMID_SELF.
 *  2. Only a sufficiently-privileged domain may obtain the status of an event
 *     channel for which <dom> is not DOMID_SELF.
 */
#define EVTCHNOP_status           5
typedef struct {
  /* IN parameters */
  domid_t dom;		/*  0 */
  u16_t __pad;
  u32_t port;		/*  4 */
  /* OUT parameters */
#define EVTCHNSTAT_closed       0	/* Channel is not in use.                 */
#define EVTCHNSTAT_unbound      1	/* Channel is waiting interdom connection. */
#define EVTCHNSTAT_interdomain  2	/* Channel is connected to remote domain. */
#define EVTCHNSTAT_pirq         3	/* Channel is bound to a phys IRQ line.   */
#define EVTCHNSTAT_virq         4	/* Channel is bound to a virtual IRQ line */
  u32_t status;		/*  8 */
  union {			/* 12 */
    struct {
      domid_t dom;	/* 12 */
    } unbound;	/* EVTCHNSTAT_unbound */
    struct {
      domid_t dom;	/* 12 */
      u16_t __pad;
      u32_t port;	/* 16 */
    } interdomain;	/* EVTCHNSTAT_interdomain */
    u32_t pirq;	/* EVTCHNSTAT_pirq        *//* 12 */
    u32_t virq;	/* EVTCHNSTAT_virq        *//* 12 */
  } u;
} evtchn_status_t;		/* 20 bytes */

typedef struct {
  u32_t cmd;		/* EVTCHNOP_* *//*  0 */
  u32_t __reserved;	/*  4 */
  union {			/*  8 */
    evtchn_alloc_unbound_t alloc_unbound;
    evtchn_bind_interdomain_t bind_interdomain;
    evtchn_bind_virq_t bind_virq;
    evtchn_bind_pirq_t bind_pirq;
    evtchn_close_t close;
    evtchn_send_t send;
    evtchn_status_t status;
    u8_t __dummy[24];
  } u;
} evtchn_op_t;			/* 32 bytes */

#define NR_IRQS  128

enum ev_status {
  EV_UNINITIALISED = -1,
  EV_DISABLED = 0,
  EV_ENABLED
};

typedef struct {
  void (*handler) (unsigned int, struct stackframe_s *);
  enum ev_status status;
} irq_handler_t;

#endif
