/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2012,2015 Stephen Warren
 */

#ifndef _BCM2835_MSG_H
#define _BCM2835_MSG_H

#include <compat.h>

/**
 * bcm2711_load_vl805_firmware() - get vl805's firmware loaded
 *
 * Return: 0 if OK, -EIO on error
 */
int bcm2711_notify_vl805_reset(void);

#endif
