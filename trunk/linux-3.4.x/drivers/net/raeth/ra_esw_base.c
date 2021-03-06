#include <linux/init.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/atomic.h>

#include "ra_eth_reg.h"
#include "ra_esw_reg.h"
#include "mii_mgr.h"
#include "ra_esw_base.h"
#include "ra_esw_mt7620.h"

#if defined (CONFIG_RALINK_MT7620) && !defined (CONFIG_MT7530_GSW)
typedef struct
{
	u32 tx_goct;
	u32 rx_goct;
	u16 tx_good;
	u16 rx_good;
	u16 tx_bad;
	u16 rx_bad;
	u16 tx_drop;
	u16 rx_drop_arl;
	u16 rx_drop_ctrl;
	u16 rx_filter;
} mib_threshold_t;

static mib_threshold_t mib_threholds[ESW_PORT_ID_MAX+1];
#endif

#if !defined (CONFIG_MT7530_GSW)
static atomic_t esw_isr_state = ATOMIC_INIT(0);
#endif

#if defined (CONFIG_RALINK_RT3052) || defined (CONFIG_RALINK_RT3352) || \
    defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_MT7628)
static atomic_t esw_ports_link_state = ATOMIC_INIT(0);
#endif

u32 esw_reg_get(u32 addr)
{
#if defined (CONFIG_MT7530_GSW)
	u32 data = 0;
	if (mii_mgr_read(MT7530_MDIO_ADDR, addr, &data))
		return data;
	printk("%s: FAILED at read from 0x%08X!\n", __FUNCTION__, addr);
	return 0;
#else
	return sysRegRead(RALINK_ETH_SW_BASE + addr);
#endif
}

void esw_reg_set(u32 addr, u32 data)
{
#if defined (CONFIG_MT7530_GSW)
	if (!mii_mgr_write(MT7530_MDIO_ADDR, addr, data))
		printk("%s: FAILED at write to 0x%08X!\n", __FUNCTION__, addr);
#else
	sysRegWrite(RALINK_ETH_SW_BASE + addr, data);
#endif
}

u32 esw_get_port_mib_tgoc(u32 port_id, u32 *HighPart)
{
	u32 mib_val = 0;
#if defined (CONFIG_MT7530_GSW)
	mib_val   = esw_reg_get(0x4048 + 0x100*port_id);
	*HighPart = esw_reg_get(0x404C + 0x100*port_id);
#elif defined (CONFIG_RALINK_MT7620)
	mib_val   = esw_reg_get(REG_ESW_MIB_TGOC_P0 + 0x100*port_id);
	*HighPart = mib_threholds[port_id].tx_goct;
#endif
	return mib_val;
}

u32 esw_get_port_mib_rgoc(u32 port_id, u32 *HighPart)
{
	u32 mib_val = 0;
#if defined (CONFIG_MT7530_GSW)
	mib_val   = esw_reg_get(0x40A8 + 0x100*port_id);
	*HighPart = esw_reg_get(0x40AC + 0x100*port_id);
#elif defined (CONFIG_RALINK_MT7620)
	mib_val   = esw_reg_get(REG_ESW_MIB_RGOC_P0 + 0x100*port_id);
	*HighPart = mib_threholds[port_id].rx_goct;
#endif
	return mib_val;
}

u32 esw_get_port_mib_tgpc(u32 port_id)
{
	u32 mib_val = 0;
#if defined (CONFIG_MT7530_GSW)
	mib_val  = esw_reg_get(0x4008 + 0x100*port_id); // Ucast
	mib_val += esw_reg_get(0x400C + 0x100*port_id); // Mcast
	mib_val += esw_reg_get(0x4010 + 0x100*port_id); // Bcast
#elif defined (CONFIG_RALINK_MT7620)
	mib_val  = esw_reg_get(REG_ESW_MIB_TGPC_P0 + 0x100*port_id) & 0xffff;
	mib_val |= (u32)mib_threholds[port_id].tx_good << 16;
#elif defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_MT7628)
	mib_val  = esw_reg_get(0x150 + 4*port_id) & 0xFFFF;
#endif
	return mib_val;
}

u32 esw_get_port_mib_rgpc(u32 port_id)
{
	u32 mib_val = 0;
#if defined (CONFIG_MT7530_GSW)
	mib_val  = esw_reg_get(0x4068 + 0x100*port_id); // Ucast
	mib_val += esw_reg_get(0x406C + 0x100*port_id); // Mcast
	mib_val += esw_reg_get(0x4040 + 0x100*port_id); // Bcast
#elif defined (CONFIG_RALINK_MT7620)
	mib_val  = esw_reg_get(REG_ESW_MIB_RGPC_P0 + 0x100*port_id) & 0xffff;
	mib_val |= (u32)mib_threholds[port_id].rx_good << 16;
#elif defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_MT7628) || \
      defined (CONFIG_RALINK_RT3052) || defined (CONFIG_RALINK_RT3352)
	mib_val  = esw_reg_get(0xE8 + 4*port_id) & 0xFFFF;
#endif
	return mib_val;
}

u32 esw_get_port_mib_tboc(u32 port_id)
{
	u32 mib_val = 0;
#if defined (CONFIG_MT7530_GSW)
	// nothing
#elif defined (CONFIG_RALINK_MT7620)
	mib_val = esw_reg_get(REG_ESW_MIB_TBOC_P0 + 0x100*port_id);
#endif
	return mib_val;
}

u32 esw_get_port_mib_rboc(u32 port_id)
{
	u32 mib_val = 0;
#if defined (CONFIG_MT7530_GSW)
	// nothing
#elif defined (CONFIG_RALINK_MT7620)
	mib_val = esw_reg_get(REG_ESW_MIB_RBOC_P0 + 0x100*port_id);
#endif
	return mib_val;
}

u32 esw_get_port_mib_tbpc(u32 port_id)
{
	u32 mib_val = 0;
#if defined (CONFIG_MT7530_GSW)
	mib_val  = esw_reg_get(0x4004 + 0x100*port_id);
#elif defined (CONFIG_RALINK_MT7620)
	mib_val  = esw_reg_get(REG_ESW_MIB_TGPC_P0 + 0x100*port_id) >> 16;
	mib_val |= (u32)mib_threholds[port_id].tx_bad << 16;
#elif defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_MT7628)
	mib_val  = esw_reg_get(0x150 + 4*port_id) >> 16;
#endif
	return mib_val;
}

u32 esw_get_port_mib_rbpc(u32 port_id)
{
	u32 mib_val = 0;
#if defined (CONFIG_MT7530_GSW)
	mib_val  = esw_reg_get(0x4078 + 0x100*port_id);
#elif defined (CONFIG_RALINK_MT7620)
	mib_val  = esw_reg_get(REG_ESW_MIB_RGPC_P0 + 0x100*port_id) >> 16;
	mib_val |= (u32)mib_threholds[port_id].rx_bad << 16;
#elif defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_MT7628) || \
      defined (CONFIG_RALINK_RT3052) || defined (CONFIG_RALINK_RT3352)
	mib_val  = esw_reg_get(0xE8 + 4*port_id) >> 16;
#endif
	return mib_val;
}

u32 esw_get_port_mib_tepc(u32 port_id)
{
	u32 mib_val = 0;
#if defined (CONFIG_MT7530_GSW)
	mib_val  = esw_reg_get(0x4000 + 0x100*port_id);
#elif defined (CONFIG_RALINK_MT7620)
	mib_val  = esw_reg_get(REG_ESW_MIB_TEPC_P0 + 0x100*port_id) & 0xffff;
	mib_val |= (u32)mib_threholds[port_id].tx_drop << 16;
#endif
	return mib_val;
}

u32 esw_get_port_mib_repc(u32 port_id)
{
	u32 mib_val = 0;
#if defined (CONFIG_MT7530_GSW)
	mib_val  = esw_reg_get(0x4060 + 0x100*port_id);
#elif defined (CONFIG_RALINK_MT7620)
	mib_val  = esw_reg_get(REG_ESW_MIB_REPC1_P0 + 0x100*port_id) >> 16;
	mib_val |= (u32)mib_threholds[port_id].rx_drop_ctrl << 16;
#endif
	return mib_val;
}

u32 esw_get_port_mib_rfpc(u32 port_id)
{
	u32 mib_val = 0;
#if defined (CONFIG_MT7530_GSW)
	mib_val  = esw_reg_get(0x4064 + 0x100*port_id);
#elif defined (CONFIG_RALINK_MT7620)
	mib_val  = esw_reg_get(REG_ESW_MIB_REPC2_P0 + 0x100*port_id) & 0xffff;
	mib_val |= (u32)mib_threholds[port_id].rx_filter << 16;
#endif
	return mib_val;
}

void esw_irq_init(void)
{
#if defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_MT7628) || \
    defined (CONFIG_RALINK_RT3052) || defined (CONFIG_RALINK_RT3352)
	u32 reg_val = (esw_reg_get(REG_ESW_POA) >> 25) & 0x1f;
	atomic_set(&esw_ports_link_state, reg_val);
#endif

#if !defined (CONFIG_MT7530_GSW)
	atomic_set(&esw_isr_state, 0);
#endif
}

void esw_irq_enable(void)
{
	u32 reg_val;
#if defined (CONFIG_RALINK_MT7620) && !defined (CONFIG_MT7530_GSW)
	u32 i;

	/* clear pending MIB int bits */
	for (i = 0; i <= ESW_PORT_ID_MAX; i++) {
		reg_val = esw_reg_get(REG_ESW_MIB_INTS_P0 + 0x100*i);
		if (reg_val)
			esw_reg_set(REG_ESW_MIB_INTS_P0 + 0x100*i, reg_val);
	}

	/* clear pending ACL int bits */
	reg_val = esw_reg_get(REG_ESW_AISR);
	if (reg_val)
		esw_reg_set(REG_ESW_AISR, reg_val);

	/* enable MIB int mask */
	for (i = 0; i <= ESW_PORT_ID_MAX; i++) {
		reg_val = esw_reg_get(REG_ESW_MIB_INTM_P0 + 0x100*i);
		esw_reg_set(REG_ESW_MIB_INTM_P0 + 0x100*i, (reg_val & ~(ESW_MIB_INT_ALL)));
	}
#endif

	/* clear pending int bits */
	reg_val = esw_reg_get(REG_ESW_ISR);
	if (reg_val)
		esw_reg_set(REG_ESW_ISR, reg_val);

	/* enable int mask */
	reg_val = esw_reg_get(REG_ESW_IMR);
#if defined (CONFIG_MT7530_GSW)
	reg_val |= ESW_INT_ALL;
#else
	reg_val &= ~(ESW_INT_ALL);
#endif
	esw_reg_set(REG_ESW_IMR, reg_val);

#if defined (CONFIG_RAETH_ESW) && !defined (CONFIG_RALINK_MT7621)
	/* enable global ESW interrupts */
	sysRegWrite(RALINK_INTENA, RALINK_INTCTL_ESW);
#endif
}

void esw_irq_disable(void)
{
	u32 reg_val;

	/* disable int mask */
	reg_val = esw_reg_get(REG_ESW_IMR);
#if defined (CONFIG_MT7530_GSW)
	reg_val = 0;
#else
	reg_val = 0xFFFF;
#endif
	esw_reg_set(REG_ESW_IMR, reg_val);
}

void esw_mib_init(void)
{
#if defined (CONFIG_RALINK_MT7620) && !defined (CONFIG_MT7530_GSW)
	memset(mib_threholds, 0, sizeof(mib_threholds));
#endif
}

#if defined (CONFIG_RALINK_MT7620) && !defined (CONFIG_MT7530_GSW)
static void gsw_event_acl(void)
{
	u32 val_aisr = esw_reg_get(REG_ESW_AISR);

	if (val_aisr)
		esw_reg_set(REG_ESW_AISR, val_aisr);
}

static void gsw_event_mib(void)
{
	u32 i, val_mibs;

	for (i = 0; i <= ESW_PORT_ID_MAX; i++) {
		val_mibs = esw_reg_get(REG_ESW_MIB_INTS_P0 + 0x100*i);
		if (val_mibs) {
			esw_reg_set(REG_ESW_MIB_INTS_P0 + 0x100*i, val_mibs);
			
			if (val_mibs & MSK_TX_GOOD_CNT)
				mib_threholds[i].tx_good++;
			if (val_mibs & MSK_RX_GOOD_CNT)
				mib_threholds[i].rx_good++;
			if (val_mibs & MSK_TX_GOCT_CNT)
				mib_threholds[i].tx_goct++;
			if (val_mibs & MSK_RX_GOCT_CNT)
				mib_threholds[i].rx_goct++;
			if (val_mibs & MSK_TX_BAD_CNT)
				mib_threholds[i].tx_bad++;
			if (val_mibs & MSK_RX_BAD_CNT)
				mib_threholds[i].rx_bad++;
			if (val_mibs & MSK_TX_DROP_CNT)
				mib_threholds[i].tx_drop++;
			if (val_mibs & MSK_RX_FILTER_CNT)
				mib_threholds[i].rx_filter++;
			if (val_mibs & MSK_RX_ARL_DROP_CNT)
				mib_threholds[i].rx_drop_arl++;
			if (val_mibs & MSK_RX_CTRL_DROP_CNT)
				mib_threholds[i].rx_drop_ctrl++;
		}
	}
}
#endif

#if !defined (CONFIG_RAETH_ESW_CONTROL) && !defined (CONFIG_RAETH_DHCP_TOUCH)
/* stub for esw link dispatcher */
void esw_link_status_changed(u32 port_id, int port_link)
{
	const char *port_state_desc = (port_link) ? "Up" : "Down";

	printk("ESW: Link Status Changed - Port%d Link %s\n", port_id, port_state_desc);
}
#endif

#if defined (CONFIG_RALINK_MT7620) || defined (CONFIG_MT7530_GSW)
static void gsw_event_link(u32 port_id)
{
	u32 reg_val = esw_reg_get(REG_ESW_MAC_PMSR_P0 + (port_id*0x100));

#if !defined (CONFIG_RAETH_ESW_CONTROL)
#if !defined (CONFIG_MT7530_GSW)
	/* MT7620 ESW need disable EEE on every link down */
	mt7620_esw_eee_on_link(port_id, reg_val & 0x1, 0);
#endif
#endif

	esw_link_status_changed(port_id, reg_val & 0x1);
}
#elif defined (CONFIG_RALINK_RT3052) || defined (CONFIG_RALINK_RT3352) || \
      defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_MT7628)
static void esw_event_link(void)
{
	u32 i, reg_poa[2], phy_link[2];

	reg_poa[0] = (esw_reg_get(REG_ESW_POA) >> 25) & 0x1f;
	reg_poa[1] = atomic_read(&esw_ports_link_state);

	if (reg_poa[0] == reg_poa[1])
		return;

	atomic_set(&esw_ports_link_state, reg_poa[0]);

	for (i = 0; i < 5; i++) {
		phy_link[0] = reg_poa[0] & (1UL<<i);
		phy_link[1] = reg_poa[1] & (1UL<<i);
		if (phy_link[0] != phy_link[1])
			esw_link_status_changed(i, (phy_link[0]) ? 1 : 0);
	}
}
#endif

static void esw_isr_wq_handler(struct work_struct *work)
{
	u32 val_isr;
#if defined (CONFIG_MT7530_GSW)
	val_isr = esw_reg_get(REG_ESW_ISR);
	if (val_isr)
		esw_reg_set(REG_ESW_ISR, val_isr);
#else
	val_isr = atomic_read(&esw_isr_state);
	atomic_set(&esw_isr_state, 0);
	val_isr |= esw_reg_get(REG_ESW_ISR);
#endif

#if defined (CONFIG_RALINK_MT7620) || defined (CONFIG_MT7530_GSW)
	if (val_isr & P0_LINK_CHG)
		gsw_event_link(0);
	if (val_isr & P1_LINK_CHG)
		gsw_event_link(1);
	if (val_isr & P2_LINK_CHG)
		gsw_event_link(2);
	if (val_isr & P3_LINK_CHG)
		gsw_event_link(3);
	if (val_isr & P4_LINK_CHG)
		gsw_event_link(4);
#elif defined (CONFIG_RALINK_RT3052) || defined (CONFIG_RALINK_RT3352) || \
      defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_MT7628)
	if (val_isr & PORT_ST_CHG)
		esw_event_link();
#endif
}

static DECLARE_WORK(esw_isr_wq, esw_isr_wq_handler);

/**
 * handle ESW interrupt
 */
irqreturn_t esw_interrupt(int irq, void *dev_id)
{
#if !defined (CONFIG_MT7530_GSW)
	u32 val_isr = sysRegRead(RALINK_ETH_SW_BASE + REG_ESW_ISR);

	if (!val_isr)
		return IRQ_NONE;

	sysRegWrite(RALINK_ETH_SW_BASE + REG_ESW_ISR, val_isr);

#if defined (CONFIG_RALINK_MT7620)
	if (val_isr & ACL_INT)
		gsw_event_acl();

	if (val_isr & MIB_INT)
		gsw_event_mib();

	if (!(val_isr & (P0_LINK_CHG|P1_LINK_CHG|P2_LINK_CHG|P3_LINK_CHG|P4_LINK_CHG)))
		return IRQ_HANDLED;
#else
	if (!(val_isr & PORT_ST_CHG))
		return IRQ_HANDLED;
#endif
	val_isr |= atomic_read(&esw_isr_state);
	atomic_set(&esw_isr_state, val_isr);
#endif

	/* do not touch MDIO registers in hardirq context */
	schedule_work(&esw_isr_wq);

	return IRQ_HANDLED;
}

void esw_irq_cancel_wq(void)
{
	cancel_work_sync(&esw_isr_wq);
}

