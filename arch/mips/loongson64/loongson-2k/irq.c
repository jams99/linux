/*
 * =====================================================================================
 *
 *       Filename:  irq.c
 *
 *    Description:  irq handle
 *
 *        Version:  1.0
 *        Created:  03/16/2017 10:52:40 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  hp (Huang Pei), huangpei@loongson.cn
 *        Company:  Loongson Corp.
 *
 * =====================================================================================
 */
#include <linux/irq.h>
#include <asm/irq_cpu.h>
#include <loongson-2k/2k1000.h>
#include <asm/mipsregs.h>
#include <irq.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/module.h>

struct irq_domain *icu_domain;
static int irqbalance = 1;
module_param(irqbalance, int, 0664);
static unsigned int int_auto[2], int_bounce[2];
u64 ls_msi_irq_mask = 0;
#if 0
static int param_set_intparam(const char *val, struct kernel_param *kp)
{
	unsigned long base;
	int i;
	*(volatile int *)kp->arg = simple_strtoul(val,0,0);
	for(i=0;i<2;i++)
	{
		base = CKSEG1ADDR(CONF_BASE) + i * 0x40 + INT_LO_OFF ;

		ls64_conf_write32(int_auto[i], (void *)(base + INT_AUTO_OFF));
		ls64_conf_write32(int_bounce[i], (void *)(base + INT_BCE_OFF));
	}

 return 0;
}
#endif


int param_get_intparam(char *buffer, const struct kernel_param *kp)
{
	/* Y and N chosen as being relatively non-coder friendly */
	return sprintf(buffer, "0x%x", *(unsigned int *)kp->arg);
}
#if 0
module_param_call(int_auto0, param_set_intparam, param_get_intparam, &int_auto[0], 0644);
module_param_call(int_auto1, param_set_intparam, param_get_intparam, &int_auto[1], 0644);
module_param_call(int_bounce0, param_set_intparam, param_get_intparam, &int_bounce[0], 0644);
module_param_call(int_bounce1, param_set_intparam, param_get_intparam, &int_bounce[1], 0644);
#endif



/* ip7 take perf/timer */
/* ip6 take smp */
/* ip5 take off-chip msi irq */
/* ip4 take on-chip irq */
DEFINE_RAW_SPINLOCK(ls2k_irq_lock);
void ls64_ipi_interrupt(struct pt_regs *regs);

void ls_unmask_icu_irq(struct irq_data * data)
{
	struct irq_chip *chip;
	unsigned int index;
	unsigned long base;
	unsigned long flags;

	//printk("%s:%d irq:%d \r\n",__FUNCTION__,__LINE__,data->irq);

	raw_spin_lock_irqsave(&ls2k_irq_lock, flags);
	chip = data->chip;
	if (data->hwirq >= LS64_MSI_IRQ_BASE)
		index = data->hwirq - LS64_MSI_IRQ_BASE;
	else
		index = data->hwirq - LS2K_IRQ_BASE;
	base = CKSEG1ADDR(CONF_BASE) + (index >= 32) * 0x40 + INT_LO_OFF ;
	ls64_conf_write32(1 << (index & 0x1f), (void *)(base + INT_SET_OFF));
	raw_spin_unlock_irqrestore(&ls2k_irq_lock, flags);

	//printk("%s:%d \r\n",__FUNCTION__,__LINE__);
}

void ls_mask_icu_irq(struct irq_data * data)
{
	struct irq_chip *chip;
	unsigned int index;
	unsigned long base;
	unsigned long flags;

	raw_spin_lock_irqsave(&ls2k_irq_lock, flags);
	chip = data->chip;
	if (data->hwirq >= LS64_MSI_IRQ_BASE)
		index = data->hwirq - LS64_MSI_IRQ_BASE;
	else
		index = data->hwirq - LS2K_IRQ_BASE;
	base = CKSEG1ADDR(CONF_BASE) + (index >= 32) * 0x40 + INT_LO_OFF;
	ls64_conf_write32(1 << (index & 0x1f), (void *)(base + INT_CLR_OFF));
	raw_spin_unlock_irqrestore(&ls2k_irq_lock, flags);
}

int ls_set_affinity_icu_irq(struct irq_data *data, const struct cpumask *affinity,
		bool force)
{
	cpumask_t tmask;
	unsigned int cpu;
        volatile unsigned char *entry;
	unsigned long *mask;
	unsigned long base;
	unsigned int index;
	int off, sel;
	unsigned long flags;
	raw_spin_lock_irqsave(&ls2k_irq_lock, flags);
	if (data->hwirq >= LS64_MSI_IRQ_BASE)
		index = data->hwirq - LS64_MSI_IRQ_BASE;
	else
		index = data->hwirq - LS2K_IRQ_BASE;
	off = (index & 0x1f);
	sel = (index >> 5);
	base = CKSEG1ADDR(CONF_BASE) + sel * 0x40 + INT_LO_OFF ;

	cpumask_copy(&tmask, affinity);

	for_each_cpu(cpu, affinity) {
		if (!cpu_online(cpu))
			//cpu_clear(cpu, tmask);
			cpumask_clear_cpu(cpu, &tmask);
	}

	if (cpumask_empty(&tmask))
		//cpu_set(0, tmask);
		cpumask_set_cpu(cpu, &tmask);

	cpumask_copy(data->common->affinity, &tmask);

	mask = cpumask_bits(&tmask);
	entry = (void *)(base  + off);
	switch(*mask&3)
	{
		case 1:
			int_auto[sel] &= ~(1 << off);
			int_bounce[sel] &= ~(1 << off);
			writeb((readb(entry) & 0xf0)|0x01, entry);
		break;
		case 2:
			int_auto[sel] &= ~(1 << off);
			int_bounce[sel] &= ~(1 << off);
			writeb((readb(entry) & 0xf0)|0x02, entry);
		break;
		case 3:
			if(irqbalance&1)
				int_auto[sel] |= (1 << off);
			else
				int_auto[sel] &= ~(1 << off);

			if(irqbalance&2)
				int_bounce[sel] |= (1 << off);
			else
				int_bounce[sel] &= ~(1 << off);

			if(irqbalance == 0)
				int_bounce[sel] |= (1 << off);

			writeb((readb(entry) & 0xf0)|0x03, entry);
		break;
	}

	ls64_conf_write32(int_auto[sel], (void *)(base + INT_AUTO_OFF));
	ls64_conf_write32(int_bounce[sel], (void *)(base + INT_BCE_OFF));
	raw_spin_unlock_irqrestore(&ls2k_irq_lock, flags);

	return IRQ_SET_MASK_OK_NOCOPY;
}


static struct irq_chip ls64_irq_chip = {
	.name		= "ls64soc",
	/*.irq_ack	= mask_icu_irq,*/
	/*.irq_eoi	= unmask_icu_irq,*/
	.irq_unmask	= ls_unmask_icu_irq,
	.irq_mask	= ls_mask_icu_irq,
	.irq_set_affinity	= ls_set_affinity_icu_irq,
};

extern u64 ls_msi_irq_mask;
//asmlinkage void plat_irq_dispatch(void)
void mach_irq_dispatch(unsigned int cp0_cause)
{
	unsigned long hi;
	unsigned long lo;
	unsigned long irq_status;
	unsigned long irq_masked;

	if (cp0_cause & STATUSF_IP7) {
		do_IRQ(MIPS_CPU_IRQ_BASE+7);
	}
#ifdef CONFIG_SMP
	else if (cp0_cause & STATUSF_IP6) {
		ls64_ipi_interrupt(NULL);
	}
#endif
	else if (cp0_cause & (STATUSF_IP4 | STATUSF_IP5)) {
	int i = (read_c0_ebase() & 0x3ff);
	unsigned long addr = CKSEG1ADDR(CONF_BASE);
	hi = ls64_conf_read32((void*)(addr + INTSR1_OFF + (i << 8)));
	lo = ls64_conf_read32((void*)(addr + INTSR0_OFF + (i << 8)));
	irq_status = ((hi << 32) | lo);

	if (cp0_cause & STATUSF_IP5) {
		hi = (irq_status & ls_msi_irq_mask);
		if ((i = __fls(hi)) != -1) {
			do_IRQ(i + LS64_MSI_IRQ_BASE);
			hi = (hi ^ (1UL << i));
		}
		else spurious_interrupt();
	}
	else if (cp0_cause & STATUSF_IP4) {
		hi = ls64_conf_read32((void*)(addr + INT_HI_OFF + INT_EN_OFF));
		lo = ls64_conf_read32((void*)(addr + INT_LO_OFF + INT_EN_OFF));
		irq_masked = ((hi << 32) | lo);
		lo = (irq_status & irq_masked & ~ls_msi_irq_mask);
		if ((i = __fls(lo)) != -1) {
			//printk("\r\n %s:%d i:%d \r\n",__FUNCTION__,__LINE__,i);
			//llc
			//need modify get hwirq from domain
			do_IRQ(i + LS2K_IRQ_BASE);
			lo = (lo ^ (1UL << i));
		}
		else spurious_interrupt();

	}
	}
}

void set_irq_attr(int irq, unsigned int imask, unsigned int core_mask, int mode)
{
	unsigned int index;
	unsigned long base;
	unsigned int ret;
	int hi;
	int au;
	int bounce;

	if (irq >= LS64_MSI_IRQ_BASE)
		index = irq - LS64_MSI_IRQ_BASE;
	else
		index = irq - LS2K_IRQ_BASE;

	hi = (index >= 32);
	index = index & 0x1f;
	au = (mode & 0x1);
	bounce = ((mode >> 1) & 0x1);
	base = CKSEG1ADDR(CONF_BASE) + INT_LO_OFF + hi * 0x40;

	ls64_conf_write8(imask << 4 | core_mask, (void *)(base + index));

	ret = ls64_conf_read32((void*)(base + INT_BCE_OFF)) & ~(1 << index);
	ret |= (bounce << index);
	ls64_conf_write32(ret, (void *)(base + INT_BCE_OFF));

	ret = ls64_conf_read32((void*)(base + INT_AUTO_OFF)) & ~(1 << index);
	ret |= (au << index);
	ls64_conf_write32(ret, (void *)(base + INT_AUTO_OFF));
}

void __init setup_irq_default(void)
{
#if 0
	unsigned int i;
	int core_id = (read_c0_ebase() & 0x3ff);

	for (i = LS2K_IRQ_BASE; i < LS2K_IRQ_BASE + 64; i++) {
		//printk("before irq_set_chip_and_handler\r\n");
		irq_set_chip_and_handler(i, &ls64_irq_chip, handle_level_irq);
		//printk("after irq_set_chip_and_handler\r\n");
		set_irq_attr(i, 1 << (STATUSB_IP4 - 10), 1 << core_id, 0);
	}
#endif

	ls64_conf_write32(ls_msi_irq_mask, (void *)(CKSEG1ADDR(CONF_BASE) + INT_LO_OFF + INT_EDG_OFF ));
	ls64_conf_write32((ls_msi_irq_mask >> 32)|(0x1f << 12), (void *)(CKSEG1ADDR(CONF_BASE) + INT_LO_OFF + INT_EDG_OFF + 0x40));
}


static int icu_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
    int core_id = (read_c0_ebase() & 0x3ff);

    if (hw < LS2K_IRQ_BASE)
		return 0;

	irq_set_chip_and_handler(irq, &ls64_irq_chip, handle_level_irq);
	set_irq_attr(hw, 1 << (STATUSB_IP4 - 10), 1 << core_id, 0);

    return 0;
}

static const struct irq_domain_ops irq_domain_ops = {
    .xlate = irq_domain_xlate_onetwocell,
    .map = icu_map,
};


int __init gs264_icu_of_init(struct device_node *node, struct device_node *parent)
{
    mips_cpu_irq_init();

    icu_domain = irq_domain_add_legacy(node, NR_IRQS - LS2K_IRQ_BASE, LS2K_IRQ_BASE, 8, &irq_domain_ops, 0);

    set_c0_status(STATUSF_IP4 | STATUSF_IP5 | STATUSF_IP6);
    setup_irq_default();

    return 0;
}

static struct of_device_id __initdata of_irq_ids[] = {
    { .compatible = "loongson,2k-icu", .data = gs264_icu_of_init },
    {},
};

void  __init mach_init_irq(void)
{
	of_irq_init(of_irq_ids);
}
