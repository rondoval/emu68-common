// SPDX-License-Identifier: MPL-2.0 OR GPL-2.0+
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <bcm_gpio.h>
#include <iomem.h>

void gpioSetPull(tGpioRegs *pGpio, u8 index, tGpioPull ePull)
{
	u32 reg_index = index / 16;
	u32 reg_shift = ((u32)index % 16) * 2;
	u32 clear_mask = ~((u32)0b11 << reg_shift);
	u32 write_mask = (u32)ePull << reg_shift;

	mmio_write32((mmio_read32(&pGpio->GPIO_PUP_PDN_CNTRL_REG[reg_index]) & clear_mask) | write_mask, &pGpio->GPIO_PUP_PDN_CNTRL_REG[reg_index]);
}

void gpioSetAlternate(tGpioRegs *pGpio, u8 index, tGpioAlternativeFunction eAlternativeFunction)
{
	u32 reg_index = index / 10;
	u32 reg_shift = ((u32)index % 10) * 3;
	u32 clear_mask = ~((u32)0b111 << reg_shift);
	u32 write_mask = (u32)eAlternativeFunction << reg_shift;

	mmio_write32((mmio_read32(&pGpio->GPFSEL[reg_index]) & clear_mask) | write_mask, &pGpio->GPFSEL[reg_index]);
}

void gpioSetLevel(tGpioRegs *pGpio, u8 index, u8 state)
{
	u32 reg_index = index / 32;
	u32 reg_shift = index % 32;
	u32 reg_state = (u32)1 << reg_shift;
	if (state)
	{
		mmio_write32(reg_state, &pGpio->GPSET[reg_index]);
	}
	else
	{
		mmio_write32(reg_state, &pGpio->GPCLR[reg_index]);
	}
}
