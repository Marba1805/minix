/******************************************************************************
 * ctrl_if.h
 *
 * Management functions for special interface to the domain controller.
 *
 * Copyright (c) 2004, K A Fraser
 * Copyright (c) 2006, I Kelly
 */
#ifndef CTRL_IF_H
#define CTRL_IF_H

#include <xen/xen.h>
#include <xen/domain_controller.h>

typedef control_msg_t ctrl_msg_t;

#define CALLBACK_IN_BLOCKING_CONTEXT  1

#define CTRLIF_REG_HND                1
#define CTRLIF_UNREG_HND              2
#define CTRLIF_SEND_BLOCK             3
#define CTRLIF_SEND_NOBLOCK           4
#define CTRLIF_SEND_RESPONSE          5
#define CTRLIF_NOP                    6

#endif
