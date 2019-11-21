// SPDX-License-Identifier: GPL-2.0
#include <linux/dma-direct.h>
#include <linux/init.h>
#include <linux/swiotlb.h>

dma_addr_t __phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return paddr;
}

phys_addr_t __dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	return daddr;
}

void __init plat_swiotlb_setup(void)
{
	swiotlb_init(1);
}
