// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2012 Stephen Warren
 */

#include <mbox.h>
#include <compat.h>
#include <debug.h>

#define ROUND(a, b)		(((a) + (b) - 1) & ~((b) - 1))

#define PAD_COUNT(s, pad) (((s) - 1) / (pad) + 1)
#define PAD_SIZE(s, pad) (PAD_COUNT(s, pad) * pad)

#define ALLOC_ALIGN_BUFFER_PAD(type, name, size, align, pad)		\
	char __##name[ROUND(PAD_SIZE((size) * sizeof(type), pad), align)  \
		      + (align - 1)];					\
									\
	type *name = (type *)ALIGN((uintptr_t)__##name, align)

#define ALLOC_ALIGN_BUFFER(type, name, size, align)		\
	ALLOC_ALIGN_BUFFER_PAD(type, name, size, align, 1)

#define ALLOC_CACHE_ALIGN_BUFFER(type, name, size)			\
	ALLOC_ALIGN_BUFFER(type, name, size, ARCH_DMA_MINALIGN)

struct msg_notify_vl805_reset {
	struct bcm2835_mbox_hdr hdr;
	struct bcm2835_mbox_tag_pci_dev_addr dev_addr;
	ULONG end_tag;
};

/*
 * On the Raspberry Pi 4, after a PCI reset, VL805's (the xHCI chip) firmware
 * may either be loaded directly from an EEPROM or, if not present, by the
 * SoC's VideoCore. This informs VideoCore that VL805 needs its firmware
 * loaded.
 */
int bcm2711_notify_vl805_reset(void)
{
	ALLOC_CACHE_ALIGN_BUFFER(struct msg_notify_vl805_reset,
				 msg_notify_vl805_reset, 1);
	int ret;

	BCM2835_MBOX_INIT_HDR(msg_notify_vl805_reset);
	BCM2835_MBOX_INIT_TAG(&msg_notify_vl805_reset->dev_addr,
			      NOTIFY_XHCI_RESET);

	/*
	 * The pci device address is expected like this:
	 *
	 *   PCI_BUS << 20 | PCI_SLOT << 15 | PCI_FUNC << 12
	 *
	 * But since RPi4's PCIe setup is hardwired, we know the address in
	 * advance.
	 */
	msg_notify_vl805_reset->dev_addr.body.req.dev_addr = 0x100000;

	ret = bcm2835_mbox_call_prop(BCM2835_MBOX_PROP_CHAN,
				     &msg_notify_vl805_reset->hdr);
	if (ret) {
		Kprintf("[mailbox] Failed to load vl805's firmware, %ld\n", ret);
		return -EIO;
	}

	delay_us(200);

	return 0;
}
