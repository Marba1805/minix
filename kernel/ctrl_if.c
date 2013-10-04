/**
 * ctrl_if.h
 *
 * The control interface is a kernel task, handling all communication
 * with the virtual xen hardware such as console, virtual block device,
 * virtual network device.
 *
 * Code snippets taken from Linux, FreeBSD and NetBSD implementations
 *
 * Copyright (c) 2006, I Kelly
 */
#include "kernel.h"
#include "protect.h"
#include <xen/ctrl_if.h>

/* function declarations for private functions */
FORWARD _PROTOTYPE(void ctrl_if_rx_deferred, (void));
FORWARD _PROTOTYPE(int ctrl_if_deliver_message, (ctrl_msg_t * msg));
FORWARD _PROTOTYPE(void ctrl_if_notify_controller, (void));
FORWARD _PROTOTYPE(void ctrl_if_tx_tasklet, (unsigned long data));
FORWARD _PROTOTYPE(void ctrl_if_rx_deferred, (void));
FORWARD _PROTOTYPE(void ctrl_if_rx_tasklet, (unsigned long data));
FORWARD _PROTOTYPE(void ctrl_if_interrupt, (unsigned int irq, struct stackframe_s *regs));
FORWARD _PROTOTYPE(int ctrl_if_do_hard_int, (void));
FORWARD _PROTOTYPE(int ctrl_if_send_message_noblock, (ctrl_msg_t * msg));
FORWARD _PROTOTYPE(int ctrl_if_send_message_block, (ctrl_msg_t * msg));
FORWARD _PROTOTYPE(void ctrl_if_send_response, (ctrl_msg_t * msg));
FORWARD _PROTOTYPE(int ctrl_if_register_receiver,
		   (u8_t type, unsigned int process, unsigned int flags));
FORWARD _PROTOTYPE(void ctrl_if_unregister_receiver,
		   (u8_t type, unsigned int process));
FORWARD _PROTOTYPE(void ctrl_if_init, (void));

/* static */
PRIVATE int ctrl_if_evtchn = -1;
PRIVATE int ctrl_if_irq;

/*
  prod = produced
  cons = consumed
*/
PRIVATE CONTROL_RING_IDX ctrl_if_tx_resp_cons;
PRIVATE CONTROL_RING_IDX ctrl_if_rx_req_cons;

/* the kernel should be registering handlers, so set unassigned to it */
#define HANDLER_UNASSIGNED -1

/* Incoming message requests. */
/* Primary message type -> message handler. */
PRIVATE unsigned int ctrl_if_rxmsg_handler[256];
/* Primary message type -> callback in process context? */
PRIVATE unsigned long ctrl_if_rxmsg_blocking_context[256 / sizeof(unsigned long)];

/* Queue up messages to be handled in process context. */
PRIVATE ctrl_msg_t ctrl_if_rxmsg_deferred[CONTROL_RING_SIZE];
PRIVATE CONTROL_RING_IDX ctrl_if_rxmsg_deferred_prod;
PRIVATE CONTROL_RING_IDX ctrl_if_rxmsg_deferred_cons;

/**
 * Macro returns a pointer to the control interface structure,
 * which is half way through the machine interface frame
 */
#define get_ctrl_if() ((control_if_t *)				\
		       (((char *)hypervisor_shared_info)	\
			+ 2048))

#define TX_FULL(_c)							\
  (((_c)->tx_req_prod - ctrl_if_tx_resp_cons) == CONTROL_RING_SIZE)

/**
 * Deliver a control interface message to its registered listener
 * if it has one.
 */
PRIVATE int ctrl_if_deliver_message(msg)
     ctrl_msg_t * msg;
{
  unsigned int type;
  message minix_msg;
  int i;
  char *msgcp = (char *) msg;

  for (i = 0; i < M9_CTRL_MSG; i++) {
    minix_msg.m9_msg[i] = msgcp[i];
  }
  type = msg->type;

  minix_msg.m_source = CTRLIF;
  minix_msg.m_type = HARD_INT;

  if (ctrl_if_rxmsg_handler[type] != HANDLER_UNASSIGNED) {
    /*		do {*/
    i = lock_send(ctrl_if_rxmsg_handler[type],
		  &minix_msg);
    if (i == ENOTREADY) {
      ctrl_if_rxmsg_deferred[MASK_CONTROL_IDX(ctrl_if_rxmsg_deferred_prod)]
	= *msg;
      ctrl_if_rxmsg_deferred_prod++;
    }
    /*		} while (i != OK);*/
    return i;
  } else {
    return 0;
  }
}

/**
 * Notify xen that new messages have been added to ctrl_if rings
 */
PRIVATE void ctrl_if_notify_controller()
{
  notify_evtchn(ctrl_if_evtchn);
}

/**
 * Do some final processing on any messages on the ctrl_if which we transmitted
 */
PRIVATE void ctrl_if_tx_tasklet(data)
     unsigned long data;
{
  control_if_t *ctrl_if = get_ctrl_if();
  ctrl_msg_t msg;
  int was_full = TX_FULL(ctrl_if);
  CONTROL_RING_IDX rp;

  rp = ctrl_if->tx_resp_prod;
  x86_barrier();		/* Ensure we see all requests up to 'rp'. */

  while (ctrl_if_tx_resp_cons != rp) {
    /*msg = ctrl_if->tx_ring[MASK_CONTROL_IDX(ctrl_if_tx_resp_cons)]; */

    /*
     * Step over the message in the ring /after/ finishing reading it. As 
     * soon as the index is updated then the message may get blown away.
     */
    /*x86_barrier(); */
    ctrl_if_tx_resp_cons++;
  }
}

/**
 * Deliver any deferred messages.
 */
PRIVATE void ctrl_if_rx_deferred()
{
  ctrl_msg_t *msg;
  message minix_msg;
  CONTROL_RING_IDX dp;

  dp = ctrl_if_rxmsg_deferred_prod;
  x86_barrier();		/* Ensure we see all deferred requests up to 'dp'. */

  while (ctrl_if_rxmsg_deferred_cons != dp) {
    msg = &ctrl_if_rxmsg_deferred[MASK_CONTROL_IDX(ctrl_if_rxmsg_deferred_cons)];

    ctrl_if_deliver_message(msg);

    ctrl_if_rxmsg_deferred_cons++;
  }
}

/**
 * Process any ctrl if messages we have received since last interrupt
 */
PRIVATE void ctrl_if_rx_tasklet(data)
     unsigned long data;
{
  control_if_t *ctrl_if = get_ctrl_if();
  ctrl_msg_t *msg;
  unsigned int type;

  CONTROL_RING_IDX rp, dp;

  dp = ctrl_if_rxmsg_deferred_prod;
  rp = ctrl_if->rx_req_prod;
  x86_barrier();		/* Ensure we see all requests up to 'rp'. */

  while (ctrl_if_rx_req_cons != rp) {
    msg = &ctrl_if->rx_ring[MASK_CONTROL_IDX(ctrl_if_rx_req_cons)];
    type = msg->type;

    if (x86_atomic_test_bit(type,
			    (unsigned long *)&ctrl_if_rxmsg_blocking_context)) {
      ctrl_if_rxmsg_deferred[MASK_CONTROL_IDX(dp++)] = *msg;
    } else {
      ctrl_if_deliver_message(msg);
    }

    ctrl_if_rx_req_cons++;
    ctrl_if->rx_resp_prod++;
    ctrl_if_notify_controller();
  }
}

/**
 * Called by hypervisor_callback. Tell the CTRLIF process that it needs
 * to handle new received messages by sending a HARD_INT message to it.
 */
char in_cif_interrupt = 0;
PRIVATE void ctrl_if_interrupt(irq, regs)
     unsigned int irq;
     struct stackframe_s *regs;
{
  if (in_cif_interrupt)
    return;
			
  in_cif_interrupt = 1;
  lock_notify(HARDWARE, CTRLIF);
  in_cif_interrupt = 0;
}

/**
 * HARD_INT messages received, so check if there's an unprocessed messages
 * and if so, request that they be handled.
 */
PRIVATE int ctrl_if_do_hard_int()
{
  control_if_t *ctrl_if = get_ctrl_if();

  if (ctrl_if_tx_resp_cons != ctrl_if->tx_resp_prod) {
    ctrl_if_tx_tasklet(0);
  }
	
  if (ctrl_if_rxmsg_deferred_cons != ctrl_if_rxmsg_deferred_prod) {
    ctrl_if_rx_deferred();
  }
	
  if (ctrl_if_rx_req_cons != ctrl_if->rx_req_prod) {
    ctrl_if_rx_tasklet(0);
  }
	
  return 0;
}

/**
 * Send a message to the control interface. Dont block.
 */ 
PRIVATE int ctrl_if_send_message_noblock(msg)
     ctrl_msg_t * msg;
{
  control_if_t *ctrl_if = get_ctrl_if();
  unsigned long flags;
  int i;
  int s;

  if (TX_FULL(ctrl_if)) {

    if (ctrl_if_tx_resp_cons != ctrl_if->tx_resp_prod)
      ctrl_if_tx_tasklet(0);

    return EAGAIN;
  }

  ctrl_if->tx_ring[MASK_CONTROL_IDX(ctrl_if->tx_req_prod)] = *msg;

  x86_barrier();	/* Write the message before letting the controller peek at it. */
  ctrl_if->tx_req_prod++;

  ctrl_if_notify_controller();

  return 0;
}

/**
 * Send a message to the control interface. Block until it sends.
 */
PRIVATE int ctrl_if_send_message_block(msg)
     ctrl_msg_t * msg;
{
  int rc;

  while ((rc = ctrl_if_send_message_noblock(msg)) == EAGAIN) {
    hypervisor_yield();
  }

  return rc;
}

/**
 * Put a message on the messages received ring.
 */
PRIVATE void ctrl_if_send_response(msg)
     ctrl_msg_t * msg;
{
  unsigned long flags;
  control_if_t *ctrl_if = get_ctrl_if();
  ctrl_msg_t *dmsg;

  /*
   * NB. The response may the original request message, modified in-place.
   * In this situation we may have src==dst, so no copying is required.
   */
  dmsg = &ctrl_if->rx_ring[MASK_CONTROL_IDX(ctrl_if->rx_resp_prod)];
  if (dmsg != msg) {
    *dmsg = *msg;
  }

  x86_barrier();		/* Write the message before letting the controller peek at it. */
  ctrl_if->rx_resp_prod++;

  ctrl_if_notify_controller();
}

/**
 * Register a process to recieve control interface messages of a certain type.
 */
PRIVATE int ctrl_if_register_receiver(u8_t type, unsigned int process, unsigned int flags)
{
  unsigned long _flags;
  int inuse;

  inuse = (ctrl_if_rxmsg_handler[type] != HANDLER_UNASSIGNED);

  xen_kprintf("Registering reciever %x for type %x\n", process,
	      type);

  if (inuse) {
    xen_kprintf("Receiver %x already established for control "
		"messages of type %x.\n",
		ctrl_if_rxmsg_handler[type], type);
  } else {
    ctrl_if_rxmsg_handler[type] = process;
    x86_atomic_clear_bit(type,
			 (unsigned long *)&ctrl_if_rxmsg_blocking_context);
    if (flags == CALLBACK_IN_BLOCKING_CONTEXT) {
      x86_atomic_set_bit(type,
			 (unsigned long *)&ctrl_if_rxmsg_blocking_context);
    }
  }

  return !inuse;
}

/**
 * Unregister a receiver process.
 */
PRIVATE void ctrl_if_unregister_receiver(u8_t type, unsigned int process)
{
  unsigned long flags;

  if (ctrl_if_rxmsg_handler[type] != process) {
    xen_kprintf("Receiver %x is not registered for control "
		"messages of type %x.\n", process, type);
  } else {
    ctrl_if_rxmsg_handler[type] = HANDLER_UNASSIGNED;
  }

  ctrl_if_rx_deferred();
}

/**
 * Initialise the control interface
 */
PRIVATE void ctrl_if_init()
{
  int i;
  control_if_t *ctrl_if = get_ctrl_if();

  for (i = 0; i < 256; i++)
    ctrl_if_rxmsg_handler[i] = HANDLER_UNASSIGNED;

  /* Sync up with shared indexes. */
  ctrl_if_tx_resp_cons = ctrl_if->tx_resp_prod;
  ctrl_if_rx_req_cons = ctrl_if->rx_resp_prod;

  ctrl_if_evtchn =
    hypervisor_start_info->start_info.domain_controller_evtchn;

  ctrl_if_irq = bind_evtchn_to_irq(ctrl_if_evtchn);

  add_irq_handler(ctrl_if_irq, ctrl_if_interrupt);
  enable_irq_handler(ctrl_if_irq);
}

/**
 * Main loop of control interface task.
 */
PUBLIC void ctrl_if_task()
{
  /* Main program of ctrl task. 
   */
  message m;		/* message buffer for both input and output */
  int result = 0;		/* result returned by the handler */
	
  ctrl_if_init();

  while (TRUE) {
    /* Go get a message. */
    receive(ANY, &m);
    /*		xen_kprintf("recieving message %x\n", m.m_type);*/
    /* Handle the request. Only clock ticks are expected. */
    switch (m.m_type) {
    case HARD_INT:
      /*			xen_kprintf("hard int");*/
      ctrl_if_do_hard_int();
      break;
    case CTRLIF_REG_HND:
      /*
	type = m.m5_c1
	process = m.m5_i1
	flags = m.m5_i2 */
      result = ctrl_if_register_receiver(m.m5_c1, m.m5_i1, m.m5_i2);
      break;
    case CTRLIF_UNREG_HND:
      /*
	type = m.m5_c1
	process = m.m5_i1
      */
      ctrl_if_unregister_receiver(m.m5_c1, m.m5_i1);
      break;
    case CTRLIF_SEND_BLOCK:
      result = ctrl_if_send_message_block((ctrl_msg_t *)&m.m9_msg);
      /*			xen_kprintf("Sent ok\n");*/
      break;
    case CTRLIF_SEND_NOBLOCK:
      result = ctrl_if_send_message_noblock((ctrl_msg_t *)&m.m9_msg);
      break;
    case CTRLIF_SEND_RESPONSE:
      ctrl_if_send_response((ctrl_msg_t *)&m.m9_msg);
      break;
    case CTRLIF_NOP:
      xen_kprintf(m.m9_msg);
      break;
    default:	/* illegal request type */
      xen_kprintf("Message type: %x\n", m.m_type);
    }

    if (m.m_type != HARD_INT) {
      m.m_type = 0;
      /*			xen_kprintf("returning message to %x\n", m.m_source);*/
      lock_send(m.m_source, &m);
    }
  }
}
