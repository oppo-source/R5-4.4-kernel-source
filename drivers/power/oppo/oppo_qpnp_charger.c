/*******************************************************************************
* Copyright (c)  2014- 2014  Guangdong OPPO Mobile Telecommunications Corp., Ltd
* VENDOR_EDIT
* Description: oppo-qpnp-charger.
*           To disable PMIC charge when use smb358/bq24196.
* Version   : 0.1
* Date      : 2014-09-17
* Author    : lfc@oppo.com
*             
*******************************************************************************/

#define pr_fmt(fmt)	"OPPO_QPNP_CHG: %s: " fmt, __func__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/spmi.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/alarmtimer.h>
#include <linux/bitops.h>

#include <linux/delay.h>

#define OPPO_QPNP_CHARGER_DEV_NAME			"qcom,qpnp-linear-charger"
#define CHG_CTRL_REG						0x49
#define CHG_FORCE_BATT_ON					BIT(0)	
#define CHG_EN_MASK							(BIT(7) | BIT(0))
#define PERP_SUBTYPE_REG					0x05

/* Linear peripheral subtype values */
#define LBC_CHGR_SUBTYPE			0x15
#define LBC_BAT_IF_SUBTYPE			0x16
#define LBC_USB_PTH_SUBTYPE			0x17
#define LBC_MISC_SUBTYPE			0x18

#define BAT_IF_BPD_CTRL_REG			0x48
#define BATT_BPD_OFFMODE_EN			BIT(3)

#define BATT_ID_EN				BIT(0)
#define BAT_IF_BTC_CTRL				0x49
#define BTC_COMP_EN_MASK			BIT(7)

struct oppo_qpnp_chg_chip {
	struct device			*dev;
	struct spmi_device		*spmi;
	u16				chgr_base;
	u16				bat_if_base;
	u16				usb_chgpth_base;
	u16				misc_base;
	spinlock_t			hw_access_lock;

};

static int __oppo_qpnp_chg_read(struct spmi_device *spmi, u16 base,
			u8 *val, int count)
{
	int rc = 0;

	rc = spmi_ext_register_readl(spmi->ctrl, spmi->sid, base, val, count);
	if (rc)
		pr_err("SPMI read failed base=0x%02x sid=0x%02x rc=%d\n",
				base, spmi->sid, rc);

	return rc;
}

static int __oppo_qpnp_chg_write(struct spmi_device *spmi, u16 base,
			u8 *val, int count)
{
	int rc;

	rc = spmi_ext_register_writel(spmi->ctrl, spmi->sid, base, val,
					count);
	if (rc)
		pr_err("SPMI write failed base=0x%02x sid=0x%02x rc=%d\n",
				base, spmi->sid, rc);

	return rc;
}

static int oppo_qpnp_chg_read(struct oppo_qpnp_chg_chip *chip, u16 base,
			u8 *val, int count)
{
	int rc = 0;
	struct spmi_device *spmi = chip->spmi;
	unsigned long flags;

	if (base == 0) {
		pr_err("base cannot be zero base=0x%02x sid=0x%02x rc=%d\n",
				base, spmi->sid, rc);
		return -EINVAL;
	}

	spin_lock_irqsave(&chip->hw_access_lock, flags);
	rc = __oppo_qpnp_chg_read(spmi, base, val, count);
	spin_unlock_irqrestore(&chip->hw_access_lock, flags);

	return rc;
}

static int oppo_qpnp_chg_write(struct oppo_qpnp_chg_chip *chip, u16 base,
			u8 *val, int count)
{
	int rc = 0;
	struct spmi_device *spmi = chip->spmi;
	unsigned long flags;

	if (base == 0) {
		pr_err("base cannot be zero base=0x%02x sid=0x%02x rc=%d\n",
				base, spmi->sid, rc);
		return -EINVAL;
	}

	spin_lock_irqsave(&chip->hw_access_lock, flags);
	rc = __oppo_qpnp_chg_write(spmi, base, val, count);
	spin_unlock_irqrestore(&chip->hw_access_lock, flags);

	return rc;
}

static int oppo_qpnp_chg_masked_write(struct oppo_qpnp_chg_chip *chip, u16 base,
				u8 mask, u8 val)
{
	int rc;
	u8 reg_val;
	struct spmi_device *spmi = chip->spmi;
	unsigned long flags;

	spin_lock_irqsave(&chip->hw_access_lock, flags);
	rc = __oppo_qpnp_chg_read(spmi, base, &reg_val, 1);
	if (rc) {
		pr_err("spmi read failed: addr=%03X, rc=%d\n", base, rc);
		goto out;
	}
	pr_debug("addr = 0x%x read 0x%x\n", base, reg_val);

	reg_val &= ~mask;
	reg_val |= val & mask;

	pr_debug("writing to base=%x val=%x\n", base, reg_val);

	rc = __oppo_qpnp_chg_write(spmi, base, &reg_val, 1);
	if (rc)
		pr_err("spmi write failed: addr=%03X, rc=%d\n", base, rc);

out:
	spin_unlock_irqrestore(&chip->hw_access_lock, flags);
	return rc;
}

static int oppo_qpnp_chg_disable(struct oppo_qpnp_chg_chip *chip)
{
	int rc;
	u8 reg;

	reg = CHG_FORCE_BATT_ON;
	rc = oppo_qpnp_chg_masked_write(chip, chip->chgr_base + CHG_CTRL_REG,
							CHG_EN_MASK, reg);
	/* disable BTC */
	rc |= oppo_qpnp_chg_masked_write(chip, chip->bat_if_base + BAT_IF_BTC_CTRL,
							BTC_COMP_EN_MASK, 0);
	/* Enable BID and disable THM based BPD */
	reg = BATT_ID_EN | BATT_BPD_OFFMODE_EN;
	rc |= oppo_qpnp_chg_write(chip, chip->bat_if_base + BAT_IF_BPD_CTRL_REG,
								&reg, 1);
	return rc;
}


static int oppo_qpnp_chg_probe(struct spmi_device *spmi)
{
	struct oppo_qpnp_chg_chip *chip;
	struct spmi_resource *spmi_resource;
	struct resource *resource;
	int rc = 0;
	u8 subtype;

	chip = devm_kzalloc(&spmi->dev, sizeof(struct oppo_qpnp_chg_chip),
				GFP_KERNEL);
	if (!chip) {
		pr_err("memory allocation failed.\n");
		return -ENOMEM;
	}

	chip->dev = &spmi->dev;
	chip->spmi = spmi;
	dev_set_drvdata(&spmi->dev, chip);
	device_init_wakeup(&spmi->dev, 1);
	spin_lock_init(&chip->hw_access_lock);

	spmi_for_each_container_dev(spmi_resource, spmi) {
		if (!spmi_resource) {
			pr_err("spmi resource absent\n");
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
							IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
						spmi->dev.of_node->full_name);
			rc = -ENXIO;
			goto fail_chg_enable;
		}

		rc = oppo_qpnp_chg_read(chip, resource->start + PERP_SUBTYPE_REG,
					&subtype, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			goto fail_chg_enable;
		}

		switch (subtype) {
		case LBC_CHGR_SUBTYPE:
			chip->chgr_base = resource->start;
			break;
			
		case LBC_USB_PTH_SUBTYPE:
			break;
			
		case LBC_BAT_IF_SUBTYPE:
			chip->bat_if_base = resource->start;
			break;
			
		case LBC_MISC_SUBTYPE:
			chip->misc_base = resource->start;
			break;
			
		default:
			pr_err("Invalid peripheral subtype=0x%x\n", subtype);
			rc = -EINVAL;
		}
	}
	
	rc = oppo_qpnp_chg_disable(chip);
	if(rc)
		goto fail_chg_enable;
	
	pr_info("%s success\n",__func__);
	return 0;

fail_chg_enable:
	dev_set_drvdata(&spmi->dev, NULL);
	return rc;
}

static int oppo_qpnp_chg_remove(struct spmi_device *spmi)
{
	dev_set_drvdata(&spmi->dev, NULL);
	return 0;
}

static struct of_device_id oppo_qpnp_chg_match_table[] = {
	{ .compatible = OPPO_QPNP_CHARGER_DEV_NAME, },
	{}
};

static struct spmi_driver oppo_qpnp_chg_driver = {
	.probe		= oppo_qpnp_chg_probe,
	.remove		= oppo_qpnp_chg_remove,
	.driver		= {
		.name		= OPPO_QPNP_CHARGER_DEV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= oppo_qpnp_chg_match_table,
	},
};

/*
 * qpnp_lbc_init() - register spmi driver for qpnp-chg
 */
static int __init oppo_qpnp_chg_init(void)
{
	return spmi_driver_register(&oppo_qpnp_chg_driver);
}
module_init(oppo_qpnp_chg_init);

static void __exit oppo_qpnp_chg_exist(void)
{
	spmi_driver_unregister(&oppo_qpnp_chg_driver);
}
module_exit(oppo_qpnp_chg_exist);

MODULE_DESCRIPTION("OPPO QPNP charger driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" OPPO_QPNP_CHARGER_DEV_NAME);


