// license:BSD-3-Clause
// copyright-holders:David Haywood, MetalliC

#ifndef MAME_MACHINE_ATMEL_ARM_AIC_H
#define MAME_MACHINE_ATMEL_ARM_AIC_H

#pragma once


u32  __fastcall arm_aic_regs_map_r(u32 offset);
void __fastcall arm_aic_regs_map_w(u32 offset, u32 data);

void arm_aic_device_start(void (__fastcall *irq_callback)(s32));
void arm_aic_device_scan(s32 action);
void arm_aic_device_reset();

void arm_aic_set_irq(int line, int state);


#endif
