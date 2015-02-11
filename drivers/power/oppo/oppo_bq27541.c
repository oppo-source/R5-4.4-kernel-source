/*******************************************************************************
* Copyright (c)  2014- 2014  Guangdong OPPO Mobile Telecommunications Corp., Ltd
* VENDOR_EDIT
* Description: Source file for CBufferList.
*           To allocate and free memory block safely.
* Version   : 0.0
* Date      : 2014-07-30
* Author    : Dengnanwei @Bsp.charge
*             Lijiada @Bsp.charge
* ---------------------------------- Revision History: -------------------------
* <version>           <date>          < author >              <desc>
* Revision 0.0        2014-07-30      Dengnanwei @Bsp.charge
*                                     Lijiada @Bsp.charge
* Modified to be suitable to the new coding rules in all functions.
*******************************************************************************/

#define OPPO_BQ27541_PAR
#include <oppo_inc.h>

#ifdef VENDOR_EDIT
extern char *BQ27541_HMACSHA1_authenticate(char *Message,char *Key,char *result);
#endif //VENDOR_EDIT

/* OPPO 2013-12-20 liaofuchun add for fastchg firmware update */
#ifdef OPPO_USE_FAST_CHARGER
extern unsigned char Pic16F_firmware_data[];
extern int pic_fw_ver_count;
extern int pic_need_to_up_fw;
extern int pic_have_updated;
#endif
/* OPPO 2013-12-20 liaofuchun add end */


static DEFINE_IDR(battery_id);
static DEFINE_MUTEX(battery_mutex);

//static struct opchg_bms_charger *bq27541_di;
static int coulomb_counter;
static spinlock_t lock; /* protect access to coulomb_counter */
static int bq27541_i2c_txsubcmd(u8 reg, unsigned short subcmd,
		struct opchg_bms_charger *di);

#ifdef OPPO_USE_FAST_CHARGER
int opchg_bq27541_gpio_pinctrl_init(struct opchg_bms_charger *di);
int opchg_bq27541_parse_dt(struct opchg_bms_charger *di);

int opchg_set_gpio_val(int gpio , u8 val);
int opchg_get_gpio_val(int gpio);
int opchg_set_gpio_dir_output(int gpio , u8 val);
int opchg_set_gpio_dir_intput(int gpio);

int opchg_set_clock_active(struct opchg_bms_charger *di);
int opchg_set_clock_sleep(struct opchg_bms_charger *di);
int opchg_set_data_active(struct opchg_bms_charger *di);
int opchg_set_data_sleep(struct opchg_bms_charger *di);

int opchg_set_switch_fast_charger(void);
int opchg_set_switch_normal_charger(void);
int opchg_set_switch_earphone(void);

int opchg_set_reset_active(struct opchg_bms_charger  *di);
#endif

static int bq27541_read(u8 reg, int *rt_value, int b_single,
			struct opchg_bms_charger *di)
{
	return di->bus->read(reg, rt_value, b_single, di);
}

/*
 * Return the battery temperature in tenths of degree Celsius
 * Or < 0 if something fails.
 */
static int bq27541_battery_temperature(struct opchg_bms_charger *di)
{
	int ret;
	int temp = 0;
	static int count = 0;

	if(atomic_read(&di->suspended) == 1) {
		return di->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
	}

	if(di->alow_reading == true) {
		ret = bq27541_read(BQ27541_REG_TEMP, &temp, 0, di);
		if (ret) {
			count++;
			dev_err(di->dev, "error reading temperature\n");
			if(count > 1) {
				count = 0;
				/* jingchun.wang@Onlinerd.Driver, 2014/01/22  Add for it report bad status when plug out battery */
				di->temp_pre = -400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
				return -400;
			} else {
				return di->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
			}
		}
		count = 0;
	} else {
		return di->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;	
	}

	di->temp_pre = temp;

	return temp + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
}


static int bq27541_remaining_capacity(struct opchg_bms_charger *di)
{
	int ret;
	int cap = 0;

	if(di->alow_reading == true) {
		ret = bq27541_read(BQ27541_REG_RM, &cap, 0, di);
		if (ret) {
			dev_err(di->dev, "error reading capacity.\n");
			return ret;
		}
	}

	return cap;
}

static int bq27541_soc_calibrate(struct opchg_bms_charger *di, int soc)
{
	union power_supply_propval ret = {0,};
	unsigned int soc_calib;
	int counter_temp = 0;
	
	if(!di->batt_psy){
		di->batt_psy = power_supply_get_by_name("battery");
		di->soc_pre = soc;
	}
	if(di->batt_psy){
		di->batt_psy->get_property(di->batt_psy,POWER_SUPPLY_PROP_STATUS, &ret);
	
		if(ret.intval == POWER_SUPPLY_STATUS_CHARGING || ret.intval == POWER_SUPPLY_STATUS_FULL) { // is charging
			if(abs(soc - di->soc_pre) >= 2) 
			{
				di->saltate_counter++;
				#if 0
				if(di->saltate_counter < CAPACITY_SALTATE_COUNTER)
					return di->soc_pre;
				else
					di->saltate_counter = 0;
				#else
				if(opchg_chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP)
				{
					if(opchg_chip->fastcharger == 1)
					{
						counter_temp = CAPACITY_SALTATE__FAST_COUNTER_20S;//20s
					}
					else
					{
						counter_temp = CAPACITY_SALTATE__AC_COUNTER_50S;//50s
					}
				}
				else
				{
					counter_temp = CAPACITY_SALTATE__USB_COUNTER_1MIN;//1min
				}
				
				if(di->saltate_counter < counter_temp)
				{
					return di->soc_pre;
				}
				else
				{
					di->saltate_counter = 0;
				}
				#endif
			}
			else
			{
				di->saltate_counter = 0;
			}
		
			if(soc > di->soc_pre) {
				soc_calib = di->soc_pre + 1;
			}
			/*
			else if(soc < (di->soc_pre - 2)) {
				//jingchun.wang@Onlinerd.Driver, 2013/04/14  Add for allow soc fail when charging. //
				soc_calib = di->soc_pre - 1;
			}
			*/
			else
			{
				soc_calib = di->soc_pre;
			}
			
			/* jingchun.wang@Onlinerd.Driver, 2013/12/12  Add for set capacity to 100 when full in normal temp */
			if(ret.intval == POWER_SUPPLY_STATUS_FULL) {
				if(soc > 94) {
					soc_calib = 100;
				}
			}
		}
		else{   // not charging
			if ((abs(soc - di->soc_pre) >= 2) || (di->soc_pre > 80)
					|| (di->batt_vol_pre <= 3300 * 1000 && di->batt_vol_pre > 2500 * 1000)) {//sjc1118 add for batt_vol is too low but soc is not jumping
				di->saltate_counter++;
				if(di->soc_pre == 100) {
					counter_temp = CAPACITY_SALTATE_COUNTER_FULL;//6
				} else if (di->soc_pre > 95) {
					counter_temp = CAPACITY_SALTATE_COUNTER_95;///3
				} else if (di->soc_pre > 90) {
					counter_temp = CAPACITY_SALTATE_COUNTER_90;///2
				} else if(di->soc_pre > 80) {
					counter_temp = CAPACITY_SALTATE_COUNTER_80;///1.5
				} else {
					
					#if 0
					counter_temp = CAPACITY_SALTATE_COUNTER_NOT_CHARGING;///1min
					#else
					if(opchg_chip->bat_instant_vol <LOW_POWER_VOLTAGE_3600MV)
					{
						counter_temp = CAPACITY_SALTATE_COUNTER_LOW_VOLTAGE_30S;///30s
					}
					else if(opchg_chip->bat_instant_vol <LOW_POWER_VOLTAGE_3500MV)
					{
						counter_temp = CAPACITY_SALTATE_COUNTER_LOW_VOLTAGE_15S;///15s
					}
					else
					{
						counter_temp = CAPACITY_SALTATE_COUNTER_NOT_CHARGING;///1min
					}
					#endif
				}
				
				if(di->saltate_counter < counter_temp)
					return di->soc_pre;
				else
					di->saltate_counter = 0;
			}
			else
			{
				di->saltate_counter = 0;
			}
			
			if(soc < di->soc_pre)
			{
				soc_calib = di->soc_pre - 1;
			}
			else if (di->batt_vol_pre <= 3300 * 1000 && di->batt_vol_pre > 2500 * 1000 && di->soc_pre > 0)//sjc1118 add for batt_vol is too low but soc is not jumping
			{
					if(di->soc_pre >= 1)
					{
						soc_calib = di->soc_pre - 1;
					}				
			}
			else
			{
					soc_calib = di->soc_pre;
			}
		}
	}	
	else{
		soc_calib = soc;
	}
	if(soc >= 100)
		soc_calib = 100;
	di->soc_pre = soc_calib;
	pr_info("soc:%d, soc_calib:%d\n", soc, soc_calib);
	return soc_calib;
}

static int bq27541_battery_soc(struct opchg_bms_charger *di, bool raw)
{
	int ret;
	int soc = 0;

#ifdef OPCHARGER_DEBUG
	pr_err("oppo_check_pre_soc  soc_pre=%d\n",di->soc_pre);
#endif
	if(atomic_read(&di->suspended) == 1) {
		return di->soc_pre;
	}

	if(di->alow_reading == true) {
		ret = bq27541_read(BQ27541_REG_SOC, &soc, 0, di);
		if (ret) {
			dev_err(di->dev, "error reading soc.ret:%d\n",ret);
			goto read_soc_err;
		}
		#ifdef OPCHARGER_DEBUG 
			pr_err("oppo_check_soc_read_value  soc_init=%d\n",soc);
		#endif
	} else {
		if(di->soc_pre)
			return di->soc_pre;
		else
			return 0;
	}

	if(raw == true) {
		if(soc > 90) {
			soc += 2;
		}
		if(soc <= di->soc_pre) {
			di->soc_pre = soc;
		}
	}
	soc = bq27541_soc_calibrate(di,soc);
#ifdef OPCHARGER_DEBUG
	pr_err("oppo_check_soc_check_value  soc_check=%d\n",soc);
#endif
	return soc;
	
read_soc_err:
	if(di->soc_pre)
		return di->soc_pre;
	else
		return 0;
}

static int bq27541_average_current(struct opchg_bms_charger *di)
{
	int ret;
	int curr = 0;

	if(atomic_read(&di->suspended) == 1) {
		return -di->current_pre;
	}

	if(di->alow_reading == true) {
		ret = bq27541_read(BQ27541_REG_AI, &curr, 0, di);
		if (ret) {
			dev_err(di->dev, "error reading current.\n");
			return ret;
		}
	} else {
		return -di->current_pre;
	}
	// negative current
	if(curr&0x8000)
		curr = -((~(curr-1))&0xFFFF);
	di->current_pre = curr;
	return -curr;
}

/*
 * Return the battery Voltage in milivolts
 * Or < 0 if something fails.
 */
static int bq27541_battery_voltage(struct opchg_bms_charger *di)
{
	int ret;
	int volt = 0;

	if(atomic_read(&di->suspended) == 1) {
		return di->batt_vol_pre;
	}

	if(di->alow_reading == true) {
		ret = bq27541_read(BQ27541_REG_VOLT, &volt, 0, di);
		if (ret) {
			dev_err(di->dev, "error reading voltage,ret:%d\n",ret);
			return ret;
		}
	} else {
		return di->batt_vol_pre;
	}

	di->batt_vol_pre = volt * 1000;

	return volt * 1000;
}

static void bq27541_cntl_cmd(struct opchg_bms_charger *di,
				int subcmd)
{
	bq27541_i2c_txsubcmd(BQ27541_REG_CNTL, subcmd, di);
}

/*
 * i2c specific code
 */
static int bq27541_i2c_txsubcmd(u8 reg, unsigned short subcmd,
		struct opchg_bms_charger *di)
{
	struct i2c_msg msg;
	unsigned char data[3];
	int ret;

	if (!di->client)
		return -ENODEV;

	memset(data, 0, sizeof(data));
	data[0] = reg;
	data[1] = subcmd & 0x00FF;
	data[2] = (subcmd & 0xFF00) >> 8;

	msg.addr = di->client->addr;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = data;

	ret = i2c_transfer(di->client->adapter, &msg, 1);
	if (ret < 0)
		return -EIO;

	return 0;
}

static int bq27541_chip_config(struct opchg_bms_charger *di)
{
	int flags = 0, ret = 0;

	bq27541_cntl_cmd(di, BQ27541_SUBCMD_CTNL_STATUS);
	udelay(66);
	ret = bq27541_read(BQ27541_REG_CNTL, &flags, 0, di);
	if (ret < 0) {
		dev_err(di->dev, "error reading register %02x ret = %d\n",
			 BQ27541_REG_CNTL, ret);
		return ret;
	}
	udelay(66);

	bq27541_cntl_cmd(di, BQ27541_SUBCMD_ENABLE_IT);
	udelay(66);

	if (!(flags & BQ27541_CS_DLOGEN)) {
		bq27541_cntl_cmd(di, BQ27541_SUBCMD_ENABLE_DLOG);
		udelay(66);
	}

	return 0;
}

static void bq27541_coulomb_counter_work(struct work_struct *work)
{
	int value = 0, temp = 0, index = 0, ret = 0;
	struct opchg_bms_charger *di;
	unsigned long flags;
	int count = 0;

	di = container_of(work, struct opchg_bms_charger, counter);

	/* retrieve 30 values from FIFO of coulomb data logging buffer
	 * and average over time
	 */
	do {
		ret = bq27541_read(BQ27541_REG_LOGBUF, &temp, 0, di);
		if (ret < 0)
			break;
		if (temp != 0x7FFF) {
			++count;
			value += temp;
		}
		/* delay 66uS, waiting time between continuous reading
		 * results
		 */
		udelay(66);
		ret = bq27541_read(BQ27541_REG_LOGIDX, &index, 0, di);
		if (ret < 0)
			break;
		udelay(66);
	} while (index != 0 || temp != 0x7FFF);

	if (ret < 0) {
		dev_err(di->dev, "Error reading datalog register\n");
		return;
	}

	if (count) {
		spin_lock_irqsave(&lock, flags);
		coulomb_counter = value/count;
		spin_unlock_irqrestore(&lock, flags);
	}
}


static int bq27541_get_battery_mvolts(void)
{
	return bq27541_battery_voltage(bq27541_di);
}

static int bq27541_get_battery_temperature(void)
{
	return bq27541_battery_temperature(bq27541_di);
}
static int bq27541_is_battery_present(void)
{
	return 1;
}
static int bq27541_is_battery_temp_within_range(void)
{
	return 1;
}
static int bq27541_is_battery_id_valid(void)
{
	return 1;
}

/* OPPO 2013-08-24 wangjc Add begin for add adc interface. */
#ifdef VENDOR_EDIT
static int bq27541_get_batt_remaining_capacity(void)
{
	return bq27541_remaining_capacity(bq27541_di);
}

static int bq27541_get_battery_soc(void)
{
	return bq27541_battery_soc(bq27541_di, false);
}


static int bq27541_get_average_current(void)
{
	return bq27541_average_current(bq27541_di);
}

//wangjc add for authentication
static int bq27541_is_battery_authenticated(void)
{
	if(bq27541_di) {
		return bq27541_di->is_authenticated;
	}
	return false;
}

static int bq27541_fast_chg_started(void)
{
	if(bq27541_di) {
		return bq27541_di->fast_chg_started;
	}
	return false;
}

static int bq27541_fast_switch_to_normal(void)
{
	if(bq27541_di) {
		//pr_err("%s fast_switch_to_normal:%d\n",__func__,bq27541_di->fast_switch_to_normal);
		return bq27541_di->fast_switch_to_normal;
	}
	return false;
}

static int bq27541_set_switch_to_noraml_false(void)
{
	if(bq27541_di) {
		bq27541_di->fast_switch_to_normal = false;
	}

	return 0;
}

static int bq27541_get_fast_low_temp_full(void)
{
	if(bq27541_di) {
		return bq27541_di->fast_low_temp_full;
	}
	return false;
}

static int bq27541_set_fast_low_temp_full_false(void)
{
	if(bq27541_di) {
		return bq27541_di->fast_low_temp_full = false;
	}
	return 0;
}
#endif
/* OPPO 2013-08-24 wangjc Add end */

/* OPPO 2013-12-12 liaofuchun add for set/get fastchg allow begin*/
static int bq27541_fast_normal_to_warm(void)
{
	if(bq27541_di) {
		//pr_err("%s fast_switch_to_normal:%d\n",__func__,bq27541_di->fast_switch_to_normal);
		return bq27541_di->fast_normal_to_warm;
	}
	return 0;
}

static int bq27541_set_fast_normal_to_warm_false(void)
{
	if(bq27541_di) {
		bq27541_di->fast_normal_to_warm = false;
	}

	return 0;
}

static int bq27541_set_fast_chg_allow(int enable)
{
	if(bq27541_di) {
		bq27541_di->fast_chg_allow = enable;
	}
	return 0;
}

static int bq27541_get_fast_chg_allow(void)
{
	if(bq27541_di) {
		return bq27541_di->fast_chg_allow;
	}
	return 0;
}

static int bq27541_get_fast_chg_ing(void)
{
	if(bq27541_di) {
			return bq27541_di->fast_chg_ing;
		}
	return 0;
}

/* OPPO 2013-12-12 liaofuchun add for set/get fastchg allow end */

static struct qpnp_battery_gauge bq27541_batt_gauge = {
	.get_battery_mvolts		= bq27541_get_battery_mvolts,
	.get_battery_temperature	= bq27541_get_battery_temperature,
	.is_battery_present		= bq27541_is_battery_present,
	.is_battery_temp_within_range	= bq27541_is_battery_temp_within_range,
	.is_battery_id_valid		= bq27541_is_battery_id_valid,
/* OPPO 2013-09-30 wangjc Add begin for add new interface */
#ifdef VENDOR_EDIT
	.get_batt_remaining_capacity = bq27541_get_batt_remaining_capacity,
	.get_battery_soc			= bq27541_get_battery_soc,
	.get_average_current		= bq27541_get_average_current,
	//wangjc add for authentication
	.is_battery_authenticated	= bq27541_is_battery_authenticated,
	.fast_chg_started			= bq27541_fast_chg_started,
	.fast_switch_to_normal		= bq27541_fast_switch_to_normal,
	.set_switch_to_noraml_false	= bq27541_set_switch_to_noraml_false,
	.set_fast_chg_allow			= bq27541_set_fast_chg_allow,
	.get_fast_chg_allow			= bq27541_get_fast_chg_allow,
	.fast_normal_to_warm		= bq27541_fast_normal_to_warm,
	.set_normal_to_warm_false	= bq27541_set_fast_normal_to_warm_false,
	.get_fast_chg_ing			= bq27541_get_fast_chg_ing,
	.get_fast_low_temp_full		= bq27541_get_fast_low_temp_full,
	.set_low_temp_full_false	= bq27541_set_fast_low_temp_full_false,
#endif
/* OPPO 2013-09-30 wangjc Add end */
};

static bool bq27541_authenticate(struct i2c_client *client);
static int bq27541_batt_type_detect(struct i2c_client *client);

static void bq27541_hw_config(struct work_struct *work)
{
	int ret = 0, flags = 0, type = 0, fw_ver = 0;
	struct opchg_bms_charger *di;

	di  = container_of(work, struct opchg_bms_charger, hw_config.work);
	ret = bq27541_chip_config(di);
	if (ret) {
		dev_err(di->dev, "Failed to config Bq27541\n");
		di->retry_count--;
		if(di->retry_count > 0) {
			schedule_delayed_work(&di->hw_config, HZ);
		}
		return;
	}
	
	qpnp_battery_gauge_register(&bq27541_batt_gauge);
	bq27541_cntl_cmd(di, BQ27541_SUBCMD_CTNL_STATUS);
	udelay(66);
	bq27541_read(BQ27541_REG_CNTL, &flags, 0, di);
	bq27541_cntl_cmd(di, BQ27541_SUBCMD_DEVCIE_TYPE);
	udelay(66);
	bq27541_read(BQ27541_REG_CNTL, &type, 0, di);
	bq27541_cntl_cmd(di, BQ27541_SUBCMD_FW_VER);
	udelay(66);
	bq27541_read(BQ27541_REG_CNTL, &fw_ver, 0, di);

	di->is_authenticated = bq27541_authenticate(di->client);
	di->battery_type = bq27541_batt_type_detect(di->client);
	dev_info(di->dev, "DEVICE_TYPE is 0x%02X, FIRMWARE_VERSION is 0x%02X\n",
			type, fw_ver);
	dev_info(di->dev, "Complete bq27541 configuration 0x%02X\n", flags);
}

static int bq27541_read_i2c(u8 reg, int *rt_value, int b_single,
			struct opchg_bms_charger *di)
{
	struct i2c_client *client = di->client;
/* OPPO 2013-12-09 wangjc Modify begin for use standard i2c interface */
#ifndef VENDOR_EDIT
	struct i2c_msg msg[1];
#else
	struct i2c_msg msg[2];
#endif
/* OPPO 2013-12-09 wangjc Modify end */
	unsigned char data[2];
	int err;

	if (!client->adapter)
		return -ENODEV;
	
	mutex_lock(&battery_mutex);
/* OPPO 2013-12-09 wangjc Modify begin for use standard i2c interface */
#ifndef VENDOR_EDIT
	msg->addr = client->addr;
	msg->flags = 0;
	msg->len = 1;
	msg->buf = data;

	data[0] = reg;
	err = i2c_transfer(client->adapter, msg, 1);

	if (err >= 0) {
		if (!b_single)
			msg->len = 2;
		else
			msg->len = 1;

		msg->flags = I2C_M_RD;
		err = i2c_transfer(client->adapter, msg, 1);
		if (err >= 0) {
			if (!b_single)
				*rt_value = get_unaligned_le16(data);
			else
				*rt_value = data[0];

			mutex_unlock(&battery_mutex);

			return 0;
		}
	}
#else	
	/* Write register */
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = data;

	data[0] = reg;

	/* Read data */
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	if (!b_single)
		msg[1].len = 2;
	else
		msg[1].len = 1;
	msg[1].buf = data;

	err = i2c_transfer(client->adapter, msg, 2);
	if (err >= 0) {
		if (!b_single)
			*rt_value = get_unaligned_le16(data);
		else
			*rt_value = data[0];

		mutex_unlock(&battery_mutex);

		return 0;
	}
#endif
/* OPPO 2013-12-09 wangjc Modify end */
	mutex_unlock(&battery_mutex);

	return err;
}

#ifdef CONFIG_BQ27541_TEST_ENABLE
static int reg;
static int subcmd;
static ssize_t bq27541_read_stdcmd(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret;
	int temp = 0;
	struct platform_device *client;
	struct opchg_bms_charger *di;

	client = to_platform_device(dev);
	di = platform_get_drvdata(client);

	if (reg <= BQ27541_REG_ICR && reg > 0x00) {
		ret = bq27541_read(reg, &temp, 0, di);
		if (ret)
			ret = snprintf(buf, PAGE_SIZE, "Read Error!\n");
		else
			ret = snprintf(buf, PAGE_SIZE, "0x%02x\n", temp);
	} else
		ret = snprintf(buf, PAGE_SIZE, "Register Error!\n");

	return ret;
}

static ssize_t bq27541_write_stdcmd(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int cmd;

	sscanf(buf, "%x", &cmd);
	reg = cmd;
	return ret;
}

static ssize_t bq27541_read_subcmd(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret;
	int temp = 0;
	struct platform_device *client;
	struct opchg_bms_charger *di;

	client = to_platform_device(dev);
	di = platform_get_drvdata(client);

	if (subcmd == BQ27541_SUBCMD_DEVCIE_TYPE ||
		 subcmd == BQ27541_SUBCMD_FW_VER ||
		 subcmd == BQ27541_SUBCMD_HW_VER ||
		 subcmd == BQ27541_SUBCMD_CHEM_ID) {

		bq27541_cntl_cmd(di, subcmd); /* Retrieve Chip status */
		udelay(66);
		ret = bq27541_read(BQ27541_REG_CNTL, &temp, 0, di);

		if (ret)
			ret = snprintf(buf, PAGE_SIZE, "Read Error!\n");
		else
			ret = snprintf(buf, PAGE_SIZE, "0x%02x\n", temp);
	} else
		ret = snprintf(buf, PAGE_SIZE, "Register Error!\n");

	return ret;
}

static ssize_t bq27541_write_subcmd(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int cmd;

	sscanf(buf, "%x", &cmd);
	subcmd = cmd;
	return ret;
}

static DEVICE_ATTR(std_cmd, S_IRUGO|S_IWUGO, bq27541_read_stdcmd,
	bq27541_write_stdcmd);
static DEVICE_ATTR(sub_cmd, S_IRUGO|S_IWUGO, bq27541_read_subcmd,
	bq27541_write_subcmd);
static struct attribute *fs_attrs[] = {
	&dev_attr_std_cmd.attr,
	&dev_attr_sub_cmd.attr,
	NULL,
};
static struct attribute_group fs_attr_group = {
	.attrs = fs_attrs,
};

static struct platform_device this_device = {
	.name			= "bq27541-test",
	.id			= -1,
	.dev.platform_data	= NULL,
};
#endif

#ifdef VENDOR_EDIT
/*OPPO 2013-09-18 liaofuchun add begin for bq27541 authenticate */
#define BLOCKDATACTRL	0X61
#define DATAFLASHBLOCK	0X3F
#define AUTHENDATA		0X40
#define AUTHENCHECKSUM	0X54
#define MESSAGE_LEN		20
#define KEY_LEN			16

/* OPPO 2014-02-25 sjc Modify begin for FIND7OP not use authenticate */
#ifndef CONFIG_OPPO_DEVICE_FIND7
static bool bq27541_authenticate(struct i2c_client *client)
{
	return true;
}
#else
static bool bq27541_authenticate(struct i2c_client *client)
{
	char recv_buf[MESSAGE_LEN]={0x0};
	char send_buf[MESSAGE_LEN]={0x0};
	char result[MESSAGE_LEN]={0x0};
	char Key[KEY_LEN]={0x77,0x30,0xa1,0x28,0x0a,0xa1,0x13,0x20,0xef,0xcd,0xab,0x89,0x67,0x45,0x23,0x01};
	char checksum_buf[1] ={0x0};
	char authen_cmd_buf[1] = {0x00};
	int i,rc;
	pr_info("%s Enter\n",__func__);

	// step 0: produce 20 bytes random data and checksum
	get_random_bytes(send_buf,20);	
	for(i = 0;i < 20;i++){
		checksum_buf[0] = checksum_buf[0] + send_buf[i];
	}
	checksum_buf[0] = 0xff - (checksum_buf[0]&0xff);

	/* step 1: unseal mode->write 0x01 to blockdatactrl
	authen_cmd_buf[0] = 0x01;
	rc = i2c_smbus_write_i2c_block_data(client,BLOCKDATACTRL,1,&authen_cmd_buf[0]);
	}	*/
	
	// step 1: seal mode->write 0x00 to dataflashblock
	rc = i2c_smbus_write_i2c_block_data(client,DATAFLASHBLOCK,1,&authen_cmd_buf[0]);
	if( rc < 0 ){
		pr_info("%s i2c write error\n",__func__);
		return false;
	}
	// step 2: write 20 bytes to authendata_reg
	i2c_smbus_write_i2c_block_data(client,AUTHENDATA,MESSAGE_LEN,&send_buf[0]);
	msleep(1);
	// step 3: write checksum to authenchecksum_reg for compute
	i2c_smbus_write_i2c_block_data(client,AUTHENCHECKSUM,1,&checksum_buf[0]);
	msleep(50);
	// step 4: read authendata
	i2c_smbus_read_i2c_block_data(client,AUTHENDATA,MESSAGE_LEN,&recv_buf[0]);
	// step 5: phone do hmac(sha1-generic) algorithm
	BQ27541_HMACSHA1_authenticate(send_buf,Key,result);
	// step 6: compare recv_buf from bq27541 and result by phone
	rc = strncmp(recv_buf,result,MESSAGE_LEN);
	if(rc == 0){
		pr_info("bq27541_authenticate success\n");
		return true;
	}
	pr_info("bq27541_authenticate error,dump buf:\n");
	for(i = 0;i < 20;i++){
		pr_info("BQ27541 send_buf[%d]:0x%x,recv_buf[%d]:0x%x = result[%d]:0x%x\n",i,send_buf[i],i,recv_buf[i],i,result[i]);
	}
	return false;
}
#endif //CONFIG_OPPO_DEVICE_FIND7OP
/* OPPO 2014-02-25 sjc Modify end */
#endif //CONFIG_VENDOR_EDIT

#ifdef VENDOR_EDIT
//Fuchun.Liao@EXP.Driver,2014/01/10 add for check battery type
#define BATTERY_2700MA		0
#define BATTERY_3000MA		1
#define TYPE_INFO_LEN		8

#ifndef CONFIG_OPPO_DEVICE_FIND7OP
/* jingchun.wang@Onlinerd.Driver, 2014/03/10  Modify for 14001 */
static int bq27541_batt_type_detect(struct i2c_client *client)
{
	char blockA_cmd_buf[1] = {0x01};
	char rc = 0;
	char recv_buf[TYPE_INFO_LEN] = {0x0};
	int i = 0;
	
	rc = i2c_smbus_write_i2c_block_data(client,DATAFLASHBLOCK,1,&blockA_cmd_buf[0]);
	if( rc < 0 ){
		pr_info("%s i2c write error\n",__func__);
		return 0;
	}
	msleep(30);	//it is needed
	i2c_smbus_read_i2c_block_data(client,AUTHENDATA,TYPE_INFO_LEN,&recv_buf[0]);
	if((recv_buf[0] == 0x01) && (recv_buf[1] == 0x09) && (recv_buf[2] == 0x08) && (recv_buf[3] == 0x06))
		rc = BATTERY_2700MA;
	else if((recv_buf[0] == 0x02) && (recv_buf[1] == 0x00) && (recv_buf[2] == 0x01) && (recv_buf[3] == 0x03))
		rc = BATTERY_3000MA;
	else {
		for(i = 0;i < TYPE_INFO_LEN;i++)
			pr_info("%s error,recv_buf[%d]:0x%x\n",__func__,i,recv_buf[i]);
		rc =  BATTERY_2700MA;
	}
	pr_info("%s battery_type:%d\n",__func__,rc);
	return rc;
}
#else /*CONFIG_OPPO_DEVICE_FIND7OP*/
static int bq27541_batt_type_detect(struct i2c_client *client)
{
	return BATTERY_3000MA;
}
#endif /*CONFIG_OPPO_DEVICE_FIND7OP*/
#endif


int opchg_set_gpio_val(int gpio , u8 val)
{
	int rc=0;
	
    gpio_set_value(gpio,val);	
	return rc;
}

int opchg_get_gpio_val(int gpio)
{
	int rc=0;
	
    rc=gpio_get_value(gpio);	
	return rc;
}

int opchg_set_gpio_dir_output(int gpio , u8 val)
{
	int rc=0;
	
    rc=gpio_direction_output(gpio,val);	
	return rc;
}
int opchg_set_gpio_dir_intput(int gpio)
{
	int rc=0;
	
    rc=gpio_direction_input(gpio);	
	return rc;
}

/* OPPO 2013-12-12 liaofuchun add for fastchg */
#ifdef OPPO_USE_FAST_CHARGER
#if 0
#define AP_TX_EN	GPIO_CFG(0, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA)
#define AP_TX_DIS	GPIO_CFG(0, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA)
#define AP_RX_EN	GPIO_CFG(1,0,GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA)
#define AP_RX_DIS	GPIO_CFG(1,0,GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA)
#define AP_SWITCH_USB	GPIO_CFG(96, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA)
#endif

static irqreturn_t irq_rx_handler(int irq, void *dev_id)
{
	struct opchg_bms_charger *di = dev_id;
	//pr_info("%s\n", __func__);
	
	schedule_work(&di->fastcg_work);
	return IRQ_HANDLED;
}


static void fastcg_work_func(struct work_struct *work)
{
	int data = 0;
	int i;
	int bit = 0;
	int retval = 0;
	int ret_info = 0;
	static int fw_ver_info = 0;
	#if 0
	int volt = 0;
	int temp = 0;
	int soc = 0;
	int current_now = 0;
	#endif
	int remain_cap = 0;
	static bool isnot_power_on = 0;
	int rc =0	;

	
	free_irq(bq27541_di->irq, bq27541_di);
	for(i = 0; i < 7; i++) 
	{
		#if 1
		if(is_project(OPPO_14005)||is_project(OPPO_14023))
		{
			//rc =opchg_set_clock_active(opchg_pinctrl_chip);
			//usleep_range(1000,1000);
			//rc =opchg_set_clock_sleep(opchg_pinctrl_chip);
			//usleep_range(19000,19000);
			//bit = opchg_get_gpio_val(opchg_pinctrl_chip->opchg_data_gpio);
			rc =opchg_set_clock_active(bq27541_di);
			usleep_range(1000,1000);
			rc =opchg_set_clock_sleep(bq27541_di);
			usleep_range(19000,19000);
			bit = opchg_get_gpio_val(bq27541_di->opchg_data_gpio);
		}
		#else
			gpio_set_value(0, 0);
			gpio_tlmm_config(AP_TX_EN, GPIO_CFG_ENABLE);
			usleep_range(1000,1000);
			gpio_set_value(0, 1);
			gpio_tlmm_config(AP_TX_DIS, GPIO_CFG_ENABLE);
			usleep_range(19000,19000);
			bit = gpio_get_value(1);
		#endif

		data |= bit<<(6-i);	
		if((i == 2) && (data != 0x50) && (!fw_ver_info)){	//data recvd not start from "101"
			pr_err("%s data err:0x%x\n",__func__,data);
			
			if(bq27541_di->fast_chg_started == true) {
				bq27541_di->alow_reading = true;
				bq27541_di->fast_chg_started = false;
				#if 1
				if (opchg_chip->vooc_start_step > 0) {
				    opchg_chip->vooc_start_step = 1;
				}
				#endif
				bq27541_di->fast_chg_allow = false;
				bq27541_di->fast_switch_to_normal = false;
				bq27541_di->fast_normal_to_warm = false;
				bq27541_di->fast_chg_ing = false;
			#if 1
				//bq27541_di->opchg_fast_charger_enable=1;
				rc =opchg_set_switch_mode(NORMAL_CHARGER_MODE);
			#else
				gpio_set_value(96, 0);
				retval = gpio_tlmm_config(AP_SWITCH_USB, GPIO_CFG_ENABLE);
				if (retval) {
					pr_err("%s switch usb error %d\n", __func__, retval);
				}
			#endif				
						
				#ifdef OPCHARGER_DEBUG_FAST
				pr_info("%s data err:0x%x fastchg handshake fail\n", __func__,data);
				#endif
				power_supply_changed(bq27541_di->batt_psy);
			}
			goto out;
		}
	}
	pr_err("%s recv data:0x%x\n", __func__, data);

	if(data == VOOC_NOTIFY_FAST_PRESENT) {
		//request fast charging
		wake_lock(&bq27541_di->fastchg_wake_lock);
		pic_need_to_up_fw = 0;
		fw_ver_info = 0;
		bq27541_di->alow_reading = false;
		bq27541_di->fast_chg_started = true;
		bq27541_di->fast_chg_allow = false;
		bq27541_di->fast_normal_to_warm = false;
		
		mod_timer(&bq27541_di->watchdog,
		  jiffies + msecs_to_jiffies(10000));
		if(!isnot_power_on){
			isnot_power_on = 1;
			ret_info = 0x1;
		} else {
			ret_info = 0x2;
		}
		
		#ifdef OPCHARGER_DEBUG_FAST
		pr_info("%s fastchg handshake Success\n", __func__);
		#endif
	} 
	else if(data == VOOC_NOTIFY_FAST_ABSENT) 
	{
		//fast charge stopped
		bq27541_di->alow_reading = true;
		bq27541_di->fast_chg_started = false;
		#if 1
		if (opchg_chip->vooc_start_step > 0) {
		    opchg_chip->vooc_start_step = 1;
		}
		#endif
		bq27541_di->fast_chg_allow = false;
		bq27541_di->fast_switch_to_normal = false;
		bq27541_di->fast_normal_to_warm = false;
		bq27541_di->fast_chg_ing = false;
		//switch off fast chg
		rc = opchg_set_switch_mode(NORMAL_CHARGER_MODE);
		#ifdef OPCHARGER_DEBUG_FAST
		pr_info("%s fastchg stop unexpectly,switch off fastchg\n", __func__);
		#endif
	#if 1
		#ifdef OPPO_USE_FAST_CHARGER
		if(is_project(OPPO_14005)||is_project(OPPO_14023)) {
			#if 1
			rc = opchg_read_reg(opchg_chip, REG08_BQ24196_ADDRESS,&reg08_val);
			if (rc < 0) {
				pr_debug("oppo check fastcharger int Couldn't read %d rc = %d\n",REG08_BQ24196_ADDRESS, rc);
			}
			else {
				pr_debug("oppo check fastcharger int reg0x%x = 0x%x\n", REG08_BQ24196_ADDRESS,reg08_val);
			}
			
			if((reg08_val & REG08_BQ24196_CHARGING_STATUS_POWER_MASK) == REG08_BQ24196_CHARGING_STATUS_POWER_IN) {
				if (opchg_chip->chg_present == false) {
				//	rc = bq24196_chg_uv(opchg_chip, 0);
				}
			}
			else {
				if (opchg_chip->chg_present == true) {
		    		rc = bq24196_chg_uv(opchg_chip, 1);
		    		}
			} 
			#else
			rc = bq24196_chg_uv(opchg_chip, 1);	
			#endif
		}
		#endif
	#else
		
		gpio_set_value(96, 0);
		retval = gpio_tlmm_config(AP_SWITCH_USB, GPIO_CFG_ENABLE);
		if (retval) {
			pr_err("%s switch usb error %d\n", __func__, retval);
		}
	#endif		
		
		del_timer(&bq27541_di->watchdog);
		ret_info = 0x2;
	} 
	else if(data == VOOC_NOTIFY_ALLOW_READING_IIC) 
	{
		//ap with mcu handshake normal,enable fast charging
		// allow read i2c
		bq27541_di->alow_reading = true;
		bq27541_di->fast_chg_ing = true;
		
		#if 0
		volt = bq27541_get_battery_mvolts();
		temp = bq27541_get_battery_temperature();
		remain_cap = bq27541_get_batt_remaining_capacity();
		soc = bq27541_get_battery_soc();
		current_now = bq27541_get_average_current();
		pr_err("%s volt:%d,temp:%d,remain_cap:%d,soc:%d,current:%d\n",__func__,volt,temp,
			remain_cap,soc,current_now);	
		#else
		opchg_chip->bat_instant_vol=bq27541_get_battery_mvolts();
		opchg_chip->temperature= bq27541_get_battery_temperature();
		remain_cap = bq27541_get_batt_remaining_capacity();
		opchg_chip->bat_volt_check_point= bq27541_get_battery_soc();
		opchg_chip->charging_current= bq27541_get_average_current();
		//opchg_chip->charger_type = qpnp_charger_type_get(opchg_chip);
		pr_err("%s volt:%d,temp:%d,remain_cap:%d,soc:%d,current:%d,charger_type:%d\n",__func__,opchg_chip->bat_instant_vol,
			opchg_chip->temperature,remain_cap,opchg_chip->bat_volt_check_point,opchg_chip->charging_current,opchg_chip->charger_type);	
		#endif
		
		//don't read
		bq27541_di->alow_reading = false;
		mod_timer(&bq27541_di->watchdog,
			  jiffies + msecs_to_jiffies(10000));
		ret_info = 0x2;
	} 
	else if(data == VOOC_NOTIFY_NORMAL_TEMP_FULL)
	{
		//fastchg full,vbatt > 4350
#if 0	//lfc modify for it(set fast_switch_to_normal ture) is earlier than usb_plugged_out irq(set it false)
		bq27541_di->fast_switch_to_normal = true;
		bq27541_di->alow_reading = true;
		bq27541_di->fast_chg_started = false;
		bq27541_di->fast_chg_allow = false;
#endif
	#if 1	
		//bq27541_di->opchg_fast_charger_enable=1;
		rc =opchg_set_switch_mode(NORMAL_CHARGER_MODE);
	#else
		gpio_set_value(96, 0);
		retval = gpio_tlmm_config(AP_SWITCH_USB, GPIO_CFG_ENABLE);
		if (retval) {
			pr_err("%s switch usb error %d\n", __func__, retval);
		}
	#endif	
	
		#ifdef OPCHARGER_DEBUG_FAST
		pr_info("%s fast charging full in normal temperature\n", __func__);
		#endif

		del_timer(&bq27541_di->watchdog);
		ret_info = 0x2;
	}
	else if(data == VOOC_NOTIFY_LOW_TEMP_FULL)
	{
		//the  fast charging full in low temperature
		if (bq27541_di->battery_type == BATTERY_3000MA){	//13097 ATL battery
			//if temp:10~20 decigec,vddmax = 4250mv
			//switch off fast chg
			pr_info("%s fastchg low temp full,switch off fastchg,set GPIO96 0\n", __func__);

		#if 1
			//bq27541_di->opchg_fast_charger_enable=1;
			rc =opchg_set_switch_mode(NORMAL_CHARGER_MODE);
		#else
			gpio_set_value(96, 0);
			retval = gpio_tlmm_config(AP_SWITCH_USB, GPIO_CFG_ENABLE);
			if (retval) {
				pr_err("%s switch usb error %d\n", __func__, retval);
			}
		#endif					
		}
		#ifdef OPCHARGER_DEBUG_FAST
		pr_info("%s bq27541_di->battery_type = %d,fastchg low temp full\n", __func__,bq27541_di->battery_type);
		#endif
		del_timer(&bq27541_di->watchdog);
		ret_info = 0x2;
	}
	else if(data == VOOC_NOTIFY_BAD_CONNECTED)
	{
		//usb bad connected,stop fastchg
#if 0	//lfc modify for it(set fast_switch_to_normal ture) is earlier than usb_plugged_out irq(set it false)
		bq27541_di->alow_reading = true;
		bq27541_di->fast_chg_started = false;
		bq27541_di->fast_chg_allow = false;
		bq27541_di->fast_switch_to_normal = true;
#endif
	#if 1	
		rc =opchg_set_switch_mode(NORMAL_CHARGER_MODE);
	#else
		gpio_set_value(96, 0);
		retval = gpio_tlmm_config(AP_SWITCH_USB, GPIO_CFG_ENABLE);
		if (retval) {
			pr_err("%s switch usb error %d\n", __func__, retval);
		}
	#endif		
	
		#ifdef OPCHARGER_DEBUG_FAST
		pr_info("%s the battery is disconnect  stop fast cherging\n", __func__);
		#endif
		
		del_timer(&bq27541_di->watchdog);
		ret_info = 0x2;
	}
	else if(data == VOOC_NOTIFY_TEMP_OVER)
	{
		//fastchg temp over 45 or under 20

	#if 1
		//bq27541_di->opchg_fast_charger_enable=1;
		rc =opchg_set_switch_mode(NORMAL_CHARGER_MODE);
	#else	
		gpio_set_value(96, 0);
		retval = gpio_tlmm_config(AP_SWITCH_USB, GPIO_CFG_ENABLE);
		if (retval) {
			pr_err("%s switch usb error %d\n", __func__, retval);
		}
	#endif
		#ifdef OPCHARGER_DEBUG_FAST
		pr_info("%s fastchg temp > 49 or < 15,stop fast charging\n", __func__);
		#endif

		del_timer(&bq27541_di->watchdog);
		ret_info = 0x2;
	}
	else if(data == VOOC_NOTIFY_FIRMWARE_UPDATE)
	{
		//ready to get fw_ver
		fw_ver_info = 1;
		ret_info = 0x2;
		#ifdef OPCHARGER_DEBUG_FAST
		pr_info("%s is update the mcu firmware\n", __func__);
		#endif
	} 
	else if(fw_ver_info)
	{
		//fw in local is large than mcu1503_fw_ver
		if((!pic_have_updated) && (Pic16F_firmware_data[pic_fw_ver_count - 4] > data)){
			ret_info = 0x2;
			pic_need_to_up_fw = 1;	//need to update fw
		}else{
			ret_info = 0x1;
			pic_need_to_up_fw = 0;	//fw is already new,needn't to up
		}
		#ifdef OPCHARGER_DEBUG_FAST
		pr_info("local_fw:0x%x,need_to_up_fw:%d\n",Pic16F_firmware_data[pic_fw_ver_count - 4],pic_need_to_up_fw);
		#endif
		fw_ver_info = 0;
	} 
	else 
	{

	#if 1
		//bq27541_di->opchg_fast_charger_enable=1;
		rc =opchg_set_switch_mode(NORMAL_CHARGER_MODE);
		/*if (rc) {
			pr_err("%s data err(101xxxx) switch usb error %d\n", __func__, retval);
			goto out;	//avoid i2c conflict
		}*/
	#else
		gpio_set_value(96, 0);
		retval = gpio_tlmm_config(AP_SWITCH_USB, GPIO_CFG_ENABLE);
		if (retval) {
			pr_err("%s data err(101xxxx) switch usb error %d\n", __func__, retval);
			goto out;	//avoid i2c conflict
		}
	#endif		
		msleep(500);	//avoid i2c conflict
		//data err
		bq27541_di->alow_reading = true;
		bq27541_di->fast_chg_started = false;
		#if 1
		if (opchg_chip->vooc_start_step > 0) {
		    opchg_chip->vooc_start_step = 1;
		}
		#endif
		bq27541_di->fast_chg_allow = false;
		bq27541_di->fast_switch_to_normal = false;
		bq27541_di->fast_normal_to_warm = false;
		bq27541_di->fast_chg_ing = false;
		//data err
		#ifdef OPCHARGER_DEBUG_FAST
		pr_info("%s data=0x%x is data err(101xxxx),stop fast charging\n", __func__,data);
		#endif
		power_supply_changed(bq27541_di->batt_psy);
		goto out;
	}
	msleep(2);

	// set fast charging clock and data
#if 1
	if(is_project(OPPO_14005)||is_project(OPPO_14023))
	{
		rc =opchg_set_data_sleep(bq27541_di);
		for(i = 0; i < 3; i++) {
			if(i == 0){	//tell mcu1503 battery_type
				opchg_set_gpio_val(bq27541_di->opchg_data_gpio, ret_info >> 1);
			} else if(i == 1){
				opchg_set_gpio_val(bq27541_di->opchg_data_gpio, ret_info & 0x1);
			} else {
				opchg_set_gpio_val(bq27541_di->opchg_data_gpio,bq27541_di->battery_type);
			}

			rc =opchg_set_clock_active(bq27541_di);
			usleep_range(1000,1000);
			rc =opchg_set_clock_sleep(bq27541_di);
			usleep_range(19000,19000);
		}
	}
#else
	gpio_tlmm_config(AP_RX_DIS,1);
	gpio_direction_output(1, 0);
	for(i = 0; i < 3; i++) {
		if(i == 0){	//tell mcu1503 battery_type
			gpio_set_value(1, ret_info >> 1);
		} else if(i == 1){
			gpio_set_value(1, ret_info & 0x1);
		} else {
			gpio_set_value(1,bq27541_di->battery_type);
		}
		
		gpio_set_value(0, 0);
		gpio_tlmm_config(AP_TX_EN, GPIO_CFG_ENABLE);
		usleep_range(1000,1000);
		gpio_set_value(0, 1);
		gpio_tlmm_config(AP_TX_DIS, GPIO_CFG_ENABLE);
		usleep_range(19000,19000);
	}
#endif

out:
	#if 1
	if(is_project(OPPO_14005)||is_project(OPPO_14023))
	{
		//rc =opchg_set_data_active(opchg_pinctrl_chip);
		rc =opchg_set_data_active(bq27541_di);
	}
	#else
	gpio_tlmm_config(GPIO_CFG(1,0,GPIO_CFG_INPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),1);
	gpio_direction_input(1);
	#endif	
	
	//lfc add for it is faster than usb_plugged_out irq to send 0x5a(fast_chg full and usb bad connected) to AP
	if(data == VOOC_NOTIFY_NORMAL_TEMP_FULL || data == VOOC_NOTIFY_BAD_CONNECTED){
		usleep_range(180000,180000);
		bq27541_di->fast_switch_to_normal = true;
		bq27541_di->alow_reading = true;
		bq27541_di->fast_chg_started = false;
		#if 1
		if (opchg_chip->vooc_start_step > 0) {
		    opchg_chip->vooc_start_step = 1;
		}
		#endif
		bq27541_di->fast_chg_allow = false;
		bq27541_di->fast_chg_ing = false;
	}
	//fastchg temp over( > 45 or < 20)

	//lfc add to set fastchg vddmax = 4250mv during 10 ~ 20 decigec for ATL 3000mAH battery
	if(data == VOOC_NOTIFY_LOW_TEMP_FULL){
		if(bq27541_di->battery_type == BATTERY_3000MA){	//13097 ATL battery
			usleep_range(180000,180000);
			bq27541_di->fast_low_temp_full = true;
			bq27541_di->alow_reading = true;
			bq27541_di->fast_chg_started = false;
			#if 1
			if (opchg_chip->vooc_start_step > 0) {
			    opchg_chip->vooc_start_step = 1;
			}
			#endif
			bq27541_di->fast_chg_allow = false;
			bq27541_di->fast_chg_ing = false;
		}
	}
	//lfc add to set fastchg vddmax = 4250mv end
	
	if(data == VOOC_NOTIFY_TEMP_OVER){
		usleep_range(180000,180000);
		bq27541_di->fast_normal_to_warm = true;
		bq27541_di->alow_reading = true;
		bq27541_di->fast_chg_started = false;
		#if 1
		if (opchg_chip->vooc_start_step > 0) {
		    opchg_chip->vooc_start_step = 1;
		}
		#endif
		bq27541_di->fast_chg_allow = false;
		bq27541_di->fast_chg_ing = false;
	}
	
	#ifdef OPPO_USE_FAST_CHARGER
	if(pic_need_to_up_fw){
		msleep(500);
		del_timer(&bq27541_di->watchdog);

		#if 1
		rc = pic16f_fw_update(opchg_fast_charger_chip,false);
		#else
		//pic16f_fw_update(false);
		#endif
		pic_need_to_up_fw = 0;
		mod_timer(&bq27541_di->watchdog,
			  jiffies + msecs_to_jiffies(10000));
	}
	#endif
	
	retval = request_irq(bq27541_di->irq, irq_rx_handler, IRQF_TRIGGER_RISING, "mcu_data", bq27541_di);	//0X01:rising edge,0x02:falling edge
	if(retval < 0) {
	pr_err("%s request ap rx irq failed.\n", __func__);
	}
	if((data == VOOC_NOTIFY_FAST_PRESENT) || (data == VOOC_NOTIFY_ALLOW_READING_IIC)){
		power_supply_changed(bq27541_di->batt_psy);
	}

	if(data == VOOC_NOTIFY_LOW_TEMP_FULL){
		if(bq27541_di->battery_type == BATTERY_3000MA){
			power_supply_changed(bq27541_di->batt_psy);
			wake_unlock(&bq27541_di->fastchg_wake_lock);
		}
	}
		
	if((data == VOOC_NOTIFY_FAST_ABSENT) || (data == VOOC_NOTIFY_NORMAL_TEMP_FULL) 
					|| (data == VOOC_NOTIFY_BAD_CONNECTED) || (data == VOOC_NOTIFY_TEMP_OVER)){
		power_supply_changed(bq27541_di->batt_psy);
		wake_unlock(&bq27541_di->fastchg_wake_lock);
	}
}

#if 0
void di_watchdog(unsigned long data)
{
	struct opchg_bms_charger *di = (struct opchg_bms_charger *)data;

	//int rc = 0;
	
	pr_err("di_watchdog can't receive mcu data\n");
	di->alow_reading = true;
	di->fast_chg_started = false;
	di->fast_switch_to_normal = false;
	di->fast_low_temp_full = false;
	di->fast_chg_allow = false;
	di->fast_normal_to_warm = false;
	di->fast_chg_ing = false;
	//switch off fast chg
	pr_info("%s switch off fastchg\n", __func__);
	
#if 1
	bq27541_di->opchg_fast_charger_enable=1;
	//rc =opchg_set_switch_mode(NORMAL_CHARGER_MODE);
#else
	gpio_set_value(96, 0);
	ret = gpio_tlmm_config(AP_SWITCH_USB, GPIO_CFG_ENABLE);
	if (ret) {
		pr_info("%s switch usb error %d\n", __func__, ret);
	}
#endif	

	wake_unlock(&bq27541_di->fastchg_wake_lock);
}
#else
void di_watchdog(unsigned long data)
{
	pr_err("di_watchdog can't receive mcu data\n");
	bq27541_di->alow_reading = true;
	bq27541_di->fast_chg_started = false;
	#if 1
	if (opchg_chip->vooc_start_step > 0) {
	    opchg_chip->vooc_start_step = 0;
	}
	#endif
	bq27541_di->fast_switch_to_normal = false;
	bq27541_di->fast_low_temp_full = false;
	bq27541_di->fast_chg_allow = false;
	bq27541_di->fast_normal_to_warm = false;
	bq27541_di->fast_chg_ing = false;
	//switch off fast chg
	pr_info("%s switch off fastchg\n", __func__);

	opchg_set_switch_mode(NORMAL_CHARGER_MODE);
	wake_unlock(&bq27541_di->fastchg_wake_lock);
}
#endif
#endif
/* OPPO 2013-12-12 liaofuchun add for fastchg */

int opchg_bq27541_parse_dt(struct opchg_bms_charger *di)
{
    int rc=0;
	struct device_node *node = di->dev->of_node;

	// Parsing gpio swutch1
	di->opchg_swtich1_gpio = of_get_named_gpio(node, "qcom,charging_swtich1-gpio", 0);
	if(di->opchg_swtich1_gpio < 0 ){
		pr_err("chip->opchg_swtich1_gpio not specified\n");	
	}
	else
	{
		if( gpio_is_valid(di->opchg_swtich1_gpio) ){
			rc = gpio_request(di->opchg_swtich1_gpio, "charging-switch1-gpio");
			if(rc){
				pr_err("unable to request gpio [%d]\n", di->opchg_swtich1_gpio);
			}
		}
		pr_err("chip->opchg_swtich1_gpio =%d\n",di->opchg_swtich1_gpio);
	}

	// Parsing gpio swutch2
	if(get_PCB_Version()== HW_VERSION__10)
	{
		di->opchg_swtich2_gpio = of_get_named_gpio(node, "qcom,charging_swtich3-gpio", 0);
		if(di->opchg_swtich2_gpio < 0 ){
			pr_err("chip->opchg_swtich2_gpio not specified\n");	
		}
		else
		{
			if( gpio_is_valid(di->opchg_swtich2_gpio) ){
				rc = gpio_request(di->opchg_swtich2_gpio, "charging-switch3-gpio");
				if(rc){
					pr_err("unable to request gpio [%d]\n", di->opchg_swtich2_gpio);
				}
			}
			pr_err("chip->opchg_swtich2_gpio =%d\n",di->opchg_swtich2_gpio);
		}
	}
	else
	{
		di->opchg_swtich2_gpio = of_get_named_gpio(node, "qcom,charging_swtich2-gpio", 0);
		if(di->opchg_swtich2_gpio < 0 ){
			pr_err("chip->opchg_swtich2_gpio not specified\n");	
		}
		else
		{
			if( gpio_is_valid(di->opchg_swtich2_gpio) ){
				rc = gpio_request(di->opchg_swtich2_gpio, "charging-switch2-gpio");
				if(rc){
					pr_err("unable to request gpio [%d]\n", di->opchg_swtich2_gpio);
				}
			}
			pr_err("chip->opchg_swtich2_gpio =%d\n",di->opchg_swtich2_gpio);
		}
	}
	// Parsing gpio reset
	di->opchg_reset_gpio = of_get_named_gpio(node, "qcom,charging_reset-gpio", 0);
	if(di->opchg_reset_gpio < 0 ){
		pr_err("chip->opchg_reset_gpio not specified\n");	
	}
	else
	{
		if( gpio_is_valid(di->opchg_reset_gpio) ){
			rc = gpio_request(di->opchg_reset_gpio, "charging-reset-gpio");
			if(rc){
				pr_err("unable to request gpio [%d]\n", di->opchg_reset_gpio);
			}
		}
		pr_err("chip->opchg_reset_gpio =%d\n",di->opchg_reset_gpio);
	}

	// Parsing gpio clock
	di->opchg_clock_gpio = of_get_named_gpio(node, "qcom,charging_clock-gpio", 0);
	if(di->opchg_clock_gpio < 0 ){
		pr_err("chip->opchg_clock_gpio not specified\n");	
	}
	else
	{
		if( gpio_is_valid(di->opchg_clock_gpio) ){
			rc = gpio_request(di->opchg_clock_gpio, "charging-clock-gpio");
			if(rc){
				pr_err("unable to request gpio [%d]\n", di->opchg_clock_gpio);
			}
		}
		pr_err("chip->opchg_clock_gpio =%d\n",di->opchg_clock_gpio);
	}

	// Parsing gpio data
	di->opchg_data_gpio = of_get_named_gpio(node, "qcom,charging_data-gpio", 0);
	if(di->opchg_data_gpio < 0 ){
		pr_err("chip->opchg_data_gpio not specified\n");	
	}
	else
	{
		if( gpio_is_valid(di->opchg_data_gpio) ){
			rc = gpio_request(di->opchg_data_gpio, "charging-data-gpio");
			if(rc){
				pr_err("unable to request gpio [%d]\n", di->opchg_data_gpio);
			}
		}
		pr_err("chip->opchg_data_gpio =%d\n",di->opchg_data_gpio);
	}
    

#ifdef OPPO_USE_FAST_CHARGER
	if(is_project(OPPO_14005)||is_project(OPPO_14023))
	{
		opchg_gpio.opchg_swtich1_gpio=di->opchg_swtich1_gpio;
		opchg_gpio.opchg_swtich2_gpio=di->opchg_swtich2_gpio;
		opchg_gpio.opchg_reset_gpio=di->opchg_reset_gpio;
		opchg_gpio.opchg_clock_gpio=di->opchg_clock_gpio;
		opchg_gpio.opchg_data_gpio=di->opchg_data_gpio;
		//opchg_gpio.opchg_fastcharger=0;
		
		rc =opchg_bq27541_gpio_pinctrl_init(di);
	}
#endif
	return rc;
}
#ifdef OPPO_USE_FAST_CHARGER
int opchg_bq27541_gpio_pinctrl_init(struct opchg_bms_charger *di)
{

    di->pinctrl = devm_pinctrl_get(di->dev);
    if (IS_ERR_OR_NULL(di->pinctrl)) {
            pr_err("%s:%d Getting pinctrl handle failed\n",
            __func__, __LINE__);
         return -EINVAL;
	}

	// set switch1 is active and switch2 is active
	if(get_PCB_Version()== HW_VERSION__10)
	{
	    di->gpio_switch1_act_switch2_act = 
	        pinctrl_lookup_state(di->pinctrl, "switch1_act_switch3_act");
	    if (IS_ERR_OR_NULL(di->gpio_switch1_act_switch2_act)) {
	            pr_err("%s:%d Failed to get the active state pinctrl handle\n",
	            __func__, __LINE__);
	        return -EINVAL;
	    }

		// set switch1 is sleep and switch2 is sleep
	    di->gpio_switch1_sleep_switch2_sleep = 
	        pinctrl_lookup_state(di->pinctrl, "switch1_sleep_switch3_sleep");
	    if (IS_ERR_OR_NULL(di->gpio_switch1_sleep_switch2_sleep)) {
	            pr_err("%s:%d Failed to get the suspend state pinctrl handle\n",
	            __func__, __LINE__);
	        return -EINVAL;
	    }
	}
	else
	{
		di->gpio_switch1_act_switch2_act = 
	        pinctrl_lookup_state(di->pinctrl, "switch1_act_switch2_act");
	    if (IS_ERR_OR_NULL(di->gpio_switch1_act_switch2_act)) {
	            pr_err("%s:%d Failed to get the active state pinctrl handle\n",
	            __func__, __LINE__);
	        return -EINVAL;
	    }

		// set switch1 is sleep and switch2 is sleep
	    di->gpio_switch1_sleep_switch2_sleep = 
	        pinctrl_lookup_state(di->pinctrl, "switch1_sleep_switch2_sleep");
	    if (IS_ERR_OR_NULL(di->gpio_switch1_sleep_switch2_sleep)) {
	            pr_err("%s:%d Failed to get the suspend state pinctrl handle\n",
	            __func__, __LINE__);
	        return -EINVAL;
	    }
	}
	// set switch1 is active and switch2 is sleep
    di->gpio_switch1_act_switch2_sleep = 
        pinctrl_lookup_state(di->pinctrl, "switch1_act_switch2_sleep");
    if (IS_ERR_OR_NULL(di->gpio_switch1_act_switch2_sleep)) {
            pr_err("%s:%d Failed to get the state 2 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }

	// set switch1 is sleep and switch2 is active
    di->gpio_switch1_sleep_switch2_act = 
        pinctrl_lookup_state(di->pinctrl, "switch1_sleep_switch2_act");
    if (IS_ERR_OR_NULL(di->gpio_switch1_sleep_switch2_act)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }

	// set clock is active
	di->gpio_clock_active = 
        pinctrl_lookup_state(di->pinctrl, "clock_active");
    if (IS_ERR_OR_NULL(di->gpio_clock_active)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }
	
	// set clock is sleep
	di->gpio_clock_sleep = 
        pinctrl_lookup_state(di->pinctrl, "clock_sleep");
    if (IS_ERR_OR_NULL(di->gpio_clock_sleep)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }

	// set clock is active
	di->gpio_data_active = 
        pinctrl_lookup_state(di->pinctrl, "data_active");
    if (IS_ERR_OR_NULL(di->gpio_data_active)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }
	
	// set clock is sleep
	di->gpio_data_sleep = 
        pinctrl_lookup_state(di->pinctrl, "data_sleep");
    if (IS_ERR_OR_NULL(di->gpio_data_sleep)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }
	// set reset is atcive
	di->gpio_reset_active = 
        pinctrl_lookup_state(di->pinctrl, "reset_active");
    if (IS_ERR_OR_NULL(di->gpio_reset_active)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }
	// set reset is sleep
	di->gpio_reset_sleep = 
        pinctrl_lookup_state(di->pinctrl, "reset_sleep");
    if (IS_ERR_OR_NULL(di->gpio_reset_sleep)) {
            pr_err("%s:%d Failed to get the state 3 pinctrl handle\n",
            __func__, __LINE__);
        return -EINVAL;
    }
    return 0;
}

int opchg_set_clock_active(struct opchg_bms_charger  *di)
{
	int rc=0;

	if(is_project(OPPO_14005)||is_project(OPPO_14023))
	{
		rc=opchg_set_gpio_dir_output(di->opchg_clock_gpio,0);	// out 0
		rc=pinctrl_select_state(di->pinctrl,di->gpio_clock_sleep);	// PULL_down
	}
	return rc;
}
int opchg_set_clock_sleep(struct opchg_bms_charger *di)
{
	int rc=0;
	
	if(is_project(OPPO_14005)||is_project(OPPO_14023))
	{
		rc=opchg_set_gpio_dir_output(di->opchg_clock_gpio,1);	// out 1
		rc=pinctrl_select_state(di->pinctrl,di->gpio_clock_active);// PULL_up
	}
	return rc;
}

int opchg_set_data_active(struct opchg_bms_charger *di)
{
	int rc=0;

	if(is_project(OPPO_14005)||is_project(OPPO_14023))
	{
		rc=opchg_set_gpio_dir_intput(di->opchg_data_gpio);	// in
		rc=pinctrl_select_state(di->pinctrl,di->gpio_data_active);	// no_PULL
	}
	//rc=opchg_set_gpio_val(di->opchg_data_gpio,1);	// in
	return rc;
}

int opchg_set_data_sleep(struct opchg_bms_charger *di)
{
	int rc=0;
	
	if(is_project(OPPO_14005)||is_project(OPPO_14023))
	{
		rc=opchg_set_gpio_dir_output(di->opchg_data_gpio,0);	// out 1
		rc=pinctrl_select_state(di->pinctrl,di->gpio_clock_active);// no_PULL
	}
	return rc;
}

#define wait_us(n) udelay(n)
#define wait_ms(n) mdelay(n)
int opchg_set_reset_active(struct opchg_bms_charger  *di)
{
	int rc=0;

	if(is_project(OPPO_14005)||is_project(OPPO_14023))
	{
		rc=opchg_set_gpio_dir_output(di->opchg_reset_gpio,1);	// out 1
		rc=pinctrl_select_state(di->pinctrl,di->gpio_reset_active);	// PULL_up
		rc=opchg_set_gpio_val(di->opchg_reset_gpio,1);
		wait_ms(10);
		rc=opchg_set_gpio_val(di->opchg_reset_gpio,0);
		wait_ms(10);
	}
	return rc;
}

int opchg_set_switch_fast_charger(void)
{
	int rc=0;

	if(is_project(OPPO_14005)||is_project(OPPO_14023))
	{
		rc = opchg_set_gpio_dir_output(opchg_gpio.opchg_swtich1_gpio,1);	// out 1
		rc = opchg_set_gpio_dir_output(opchg_gpio.opchg_swtich2_gpio,1);	// out 1
		rc = pinctrl_select_state(bq27541_di->pinctrl,bq27541_di->gpio_switch1_act_switch2_act);	// PULL_up
		rc = opchg_set_gpio_val(opchg_gpio.opchg_swtich1_gpio,1);
		rc = opchg_set_gpio_val(opchg_gpio.opchg_swtich2_gpio,1);
	}
	return rc;

}

int opchg_set_switch_normal_charger(void)
{
	int rc=0;

	if(is_project(OPPO_14005)||is_project(OPPO_14023))
	{
		rc = opchg_set_gpio_dir_output(opchg_gpio.opchg_swtich1_gpio,0);	// in 0
		rc = opchg_set_gpio_dir_output(opchg_gpio.opchg_swtich2_gpio,1);	// out 1
		rc = pinctrl_select_state(bq27541_di->pinctrl,bq27541_di->gpio_switch1_sleep_switch2_sleep);	// PULL_down
		rc = opchg_set_gpio_val(opchg_gpio.opchg_swtich1_gpio,0);
		rc = opchg_set_gpio_val(opchg_gpio.opchg_swtich2_gpio,1);
	}
	return rc;
}
int opchg_set_switch_earphone(void)
{
	int rc=0;

	if(is_project(OPPO_14005)||is_project(OPPO_14023))
	{
		rc = opchg_set_gpio_dir_output(opchg_gpio.opchg_swtich1_gpio,1);
		rc = opchg_set_gpio_dir_output(opchg_gpio.opchg_swtich2_gpio,0);
		rc = pinctrl_select_state(bq27541_di->pinctrl,bq27541_di->gpio_switch1_sleep_switch2_sleep);	// PULL_down
		rc = opchg_set_gpio_val(opchg_gpio.opchg_swtich1_gpio,1);
		rc = opchg_set_gpio_val(opchg_gpio.opchg_swtich2_gpio,0);
	}
	return rc;
}

#endif


int opchg_set_switch_mode(u8 mode)
{
	int rc=0;

	if(is_project(OPPO_14037)||is_project(OPPO_14045) || is_project(OPPO_14051))
		return 0;
	
	// check GPIO23 and GPIO38 is  undeclared, prevent the  Invalid use for earphone
	//if(opchg_pinctrl_chip == NULL)
	if((bq27541_di == NULL)||(chip_opchg == NULL))
	{
		pr_err("%s GPIO23 and GPIO38 is no probe\n",__func__);
		return -1;
	}

	// GPIO23 and GPIO38 is  declared
    switch(mode) {
        case VOOC_CHARGER_MODE:	//11
			if(is_project(OPPO_14005)||is_project(OPPO_14023))
			{
				if(chip_opchg->opchg_earphone_enable ==false)
				{
					rc=opchg_set_switch_fast_charger();
				}
			}
			break;
            
        case HEADPHONE_MODE:		//10
        	if(is_project(OPPO_14005)||is_project(OPPO_14023))
			{
				rc=opchg_set_switch_earphone();
            }
			break;
            
        case NORMAL_CHARGER_MODE:	//01
        default:
			if(is_project(OPPO_14005)||is_project(OPPO_14023))
			{
				if(chip_opchg->opchg_earphone_enable ==false)
				{
					rc=opchg_set_switch_normal_charger();
				}
            }
			break;
    }
	
	pr_err("%s charge_mode,rc:%d,GPIO%d:%d,GPIO%d:%d\n",__func__,rc,opchg_gpio.opchg_swtich1_gpio,opchg_get_gpio_val(opchg_gpio.opchg_swtich1_gpio),opchg_gpio.opchg_swtich2_gpio,opchg_get_gpio_val(opchg_gpio.opchg_swtich2_gpio));
	return rc;
}

#define MAX_RETRY_COUNT	5
static int bq27541_battery_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct opchg_bms_charger *di;
	struct bq27541_access_methods *bus;
	int retval = 0;
	
	char *name;
	int num;
	
	int rc = 0;
	
	
	pr_debug("opcharger bq27541 start==================================\n");
	/**/
    /* i2c pull up Regulator configuration */
	#if 0
	chip->vcc_i2c = regulator_get(&client->dev, "vcc_i2c_opfastcharger");
	if (IS_ERR(chip->vcc_i2c)) {
	        dev_err(&client->dev, "%s: Failed to get i2c regulator\n", __func__);
	        retval = PTR_ERR(chip->vcc_i2c);
	        return -1;//retval;
	}
	if (regulator_count_voltages(chip->vcc_i2c) > 0) {
	        retval = regulator_set_voltage(chip->vcc_i2c, OPCHARGER_I2C_VTG_MIN_UV, OPCHARGER_I2C_VTG_MAX_UV);
	        if (retval) {
	            dev_err(&client->dev, "reg set i2c vtg failed retval =%d\n", retval);
	            goto err_set_vtg_i2c;
	        }
	}
	    
	retval = regulator_enable(chip->vcc_i2c);
	if (retval) {
	        dev_err(&client->dev,"Regulator vcc_i2c enable failed " "rc=%d\n", retval);
	        return retval;
	}
	#endif

	/**/
	if(!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s pic16F_probe,i2c_func error\n",__func__);
		return -ENODEV;
	}

	/**/
	/*
	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}
	di->client = client;	//pic16F_client = client;
	di->dev = &client->dev;
	*/
	
	/* Get new ID for the new battery device */
	#if 0
	retval = idr_pre_get(&battery_id, GFP_KERNEL);
	#else
	retval =__idr_pre_get(&battery_id, GFP_KERNEL);
	#endif
	
	if (retval == 0)
		return -ENOMEM;
	
	mutex_lock(&battery_mutex);
	#if 0
	retval = idr_get_new(&battery_id, client, &num);
	#else
	retval = oppo_idr_get_new(&battery_id, client, &num);
	#endif
	
	mutex_unlock(&battery_mutex);
	if (retval < 0)
		return retval;

	name = kasprintf(GFP_KERNEL, "%s-%d", id->name, num);
	if (!name) {
		dev_err(&client->dev, "failed to allocate device name\n");
		retval = -ENOMEM;
		goto batt_failed_1;
	}

	
	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&client->dev, "failed to allocate device info data\n");
		retval = -ENOMEM;
		goto batt_failed_2;
	}
	di->id = num;

	bus = kzalloc(sizeof(*bus), GFP_KERNEL);
	if (!bus) {
		dev_err(&client->dev, "failed to allocate access method "
					"data\n");
		retval = -ENOMEM;
		goto batt_failed_3;
	}

	i2c_set_clientdata(client, di);
	di->dev = &client->dev;
	bus->read = &bq27541_read_i2c;
	di->bus = bus;
	di->client = client;
	di->temp_pre = 0;
	di->rtc_refresh_time = 0;
	di->alow_reading = true;
	di->fast_chg_ing = false;
	di->fast_low_temp_full = false;
	di->retry_count = MAX_RETRY_COUNT;
	atomic_set(&di->suspended, 0);


	/**/
	rc=opchg_bq27541_parse_dt(di);
	//opchg_pinctrl_chip =di;
	bq27541_di = di;
	rc =opchg_set_switch_mode(NORMAL_CHARGER_MODE);
	
#ifdef CONFIG_BQ27541_TEST_ENABLE
	platform_set_drvdata(&this_device, di);
	retval = platform_device_register(&this_device);
	if (!retval) {
		retval = sysfs_create_group(&this_device.dev.kobj,
			 &fs_attr_group);
		if (retval)
			goto batt_failed_4;
	} else
		goto batt_failed_4;
#endif

	if (retval) {
		dev_err(&client->dev, "failed to setup bq27541\n");
		goto batt_failed_4;
	}

	if (retval) {
		dev_err(&client->dev, "failed to powerup bq27541\n");
		goto batt_failed_4;
	}

	spin_lock_init(&lock);

	INIT_WORK(&di->counter, bq27541_coulomb_counter_work);
	INIT_DELAYED_WORK(&di->hw_config, bq27541_hw_config);
	schedule_delayed_work(&di->hw_config, 0);
	
	/* OPPO 2013-12-22 wangjc add for fastchg*/
#ifdef OPPO_USE_FAST_CHARGER
if(is_project(OPPO_14005)||is_project(OPPO_14023))
{
	init_timer(&di->watchdog);
	di->watchdog.data = (unsigned long)di;
	di->watchdog.function = di_watchdog;
	wake_lock_init(&di->fastchg_wake_lock,		
		WAKE_LOCK_SUSPEND, "fastcg_wake_lock");
	INIT_WORK(&di->fastcg_work,fastcg_work_func);
	
#if 	1
	retval= opchg_set_data_active(di);
	di->irq = gpio_to_irq(di->opchg_data_gpio);
	retval = request_irq(di->irq, irq_rx_handler, IRQF_TRIGGER_RISING, "mcu_data", di);	//0X01:rising edge,0x02:falling edge
	if(retval < 0) {
		pr_err("%s request ap rx irq failed.\n", __func__);
	}
#else
	gpio_request(1, "mcu_clk");
	gpio_tlmm_config(AP_RX_EN,1);
	gpio_direction_input(1);
	di->irq = gpio_to_irq(1);
	retval = request_irq(di->irq, irq_rx_handler, IRQF_TRIGGER_RISING, "mcu_data", di);	//0X01:rising edge,0x02:falling edge
	if(retval < 0) {
		pr_err("%s request ap rx irq failed.\n", __func__);
	}
#endif	
}
#endif
	/* OPPO 2013-12-22 wangjc add end*/
	pr_debug("opcharger bq27541 end==================================\n");

	return 0;

batt_failed_4:
	kfree(bus);
batt_failed_3:
	kfree(di);
batt_failed_2:
	kfree(name);
batt_failed_1:
	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, num);
	mutex_unlock(&battery_mutex);

	return retval;
}

static int bq27541_battery_remove(struct i2c_client *client)
{
	struct opchg_bms_charger *di = i2c_get_clientdata(client);

	qpnp_battery_gauge_unregister(&bq27541_batt_gauge);
	bq27541_cntl_cmd(di, BQ27541_SUBCMD_DISABLE_DLOG);
	udelay(66);
	bq27541_cntl_cmd(di, BQ27541_SUBCMD_DISABLE_IT);
	cancel_delayed_work_sync(&di->hw_config);

	kfree(di->bus);

	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, di->id);
	mutex_unlock(&battery_mutex);

	kfree(di);
	return 0;
}

/* OPPO 2013-11-19 wangjingchun Add use to get rtc times for other driver */
int msmrtc_alarm_read_time(struct rtc_time *tm)
{
	struct rtc_device *alarm_rtc_dev;
	int ret=0;

#ifndef VENDOR_EDIT
/* jingchun.wang@Onlinerd.Driver, 2014/02/28  Delete for sovle alarm can't sleep */
	wake_lock(&alarm_rtc_wake_lock);
#endif /*CVENDOR_EDIT*/

	alarm_rtc_dev = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (alarm_rtc_dev == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -EINVAL;
	}

	ret = rtc_read_time(alarm_rtc_dev, tm);
	if (ret < 0) {
		pr_err("%s: Failed to read RTC time\n", __func__);
		goto err;
	}

#ifndef VENDOR_EDIT
/* jingchun.wang@Onlinerd.Driver, 2014/02/28  Delete for sovle alarm can't sleep */
	wake_unlock(&alarm_rtc_wake_lock);
#endif /*VENDOR_EDIT*/
	return 0;
err:
	pr_err("%s: rtc alarm will lost!", __func__);
#ifndef VENDOR_EDIT
/* jingchun.wang@Onlinerd.Driver, 2014/02/28  Delete for sovle alarm can't sleep */
	wake_unlock(&alarm_rtc_wake_lock);
#endif /*VENDOR_EDIT*/
	return -1;

}

static int bq27541_battery_suspend(struct i2c_client *client, pm_message_t message)
{
	int ret=0;
	struct rtc_time	rtc_suspend_rtc_time;
	struct opchg_bms_charger *di = i2c_get_clientdata(client);
	
	atomic_set(&di->suspended, 1);
	ret = msmrtc_alarm_read_time(&rtc_suspend_rtc_time);
	if (ret < 0) {
		pr_err("%s: Failed to read RTC time\n", __func__);
		return 0;
	}
	rtc_tm_to_time(&rtc_suspend_rtc_time, &di->rtc_suspend_time);
	
	return 0;
}

/*1 minute*/
#define RESUME_TIME  1*60 
#define REFRESH_TIME  10*60 
static int bq27541_battery_resume(struct i2c_client *client)
{
	int ret=0;
	struct rtc_time	rtc_resume_rtc_time;
	struct opchg_bms_charger *di = i2c_get_clientdata(client);
			
	atomic_set(&di->suspended, 0);
	ret = msmrtc_alarm_read_time(&rtc_resume_rtc_time);
	if (ret < 0) {
		pr_err("%s: Failed to read RTC time\n", __func__);
		return 0;
	}
	rtc_tm_to_time(&rtc_resume_rtc_time, &di->rtc_resume_time);
	
	if(((di->rtc_resume_time - di->rtc_suspend_time)>= RESUME_TIME)
		||((di->rtc_resume_time - di->rtc_refresh_time)>= REFRESH_TIME))
	{
		pr_err("oppo_check_rtc_time rtc_resume_time=%ld, rtc_suspend_time=%ld, rtc_refresh_time=%ld\n",di->rtc_resume_time,di->rtc_suspend_time,di->rtc_refresh_time);
		/*update pre capacity when sleep time more than 1minutes or refresh time more than 10min*/
		bq27541_battery_soc(bq27541_di, true); 
		di->rtc_refresh_time = di->rtc_resume_time;
	}

	return 0;
}

#if 0
static int __init bq27541_battery_init(void)
{
	int ret;

	ret = i2c_add_driver(&bq27541_battery_driver);
	if (ret)
		printk(KERN_ERR "Unable to register BQ27541 driver\n");

	return ret;
}
module_init(bq27541_battery_init);

static void __exit bq27541_battery_exit(void)
{
	i2c_del_driver(&bq27541_battery_driver);
}
module_exit(bq27541_battery_exit);
#endif

/* OPPO 2014-11-18 sjc Add begin for 14021 */
#define CONTROL_CMD				0x00
#define CONTROL_STATUS				0x00
#define SEAL_POLLING_RETRY_LIMIT	100
#define BQ27541_UNSEAL_KEY			11151986
#define RESET_SUBCMD				0x0041

static void control_cmd_write(struct opchg_bms_charger *di, u16 cmd)
{
	int value;
	
	//dev_dbg(di->dev, "%s: %04x\n", __FUNCTION__, cmd);
	bq27541_cntl_cmd(di, 0x0041);
	msleep(10);
	bq27541_read(CONTROL_STATUS, &value, 0, di);
	printk(KERN_ERR "bq27541 CONTROL_STATUS: 0x%x\n", value);
}
static int sealed(struct opchg_bms_charger *di)
{
	//return control_cmd_read(di, CONTROL_STATUS) & (1 << 13);
	int value = 0;
	
	bq27541_cntl_cmd(di,CONTROL_STATUS);
	msleep(10);
	bq27541_read(CONTROL_STATUS, &value, 0, di);
	pr_err("%s REG_CNTL: 0x%x\n", __func__, value);

	return value & BIT(14);
}

static int unseal(struct opchg_bms_charger *di, u32 key)
{
	int i = 0;

	if (!sealed(di))
		goto out;

	//bq27541_write(CONTROL_CMD, key & 0xFFFF, false, di);
	bq27541_cntl_cmd(di, 0x1115);
	msleep(10);
	//bq27541_write(CONTROL_CMD, (key & 0xFFFF0000) >> 16, false, di);
	bq27541_cntl_cmd(di, 0x1986);
	msleep(10);
	bq27541_cntl_cmd(di, 0xffff);
	msleep(10);
	bq27541_cntl_cmd(di, 0xffff);
	msleep(10);

	while (i < SEAL_POLLING_RETRY_LIMIT) {
		i++;
		if (!sealed(di))
			break;
		msleep(10);
	}

out:
	pr_err("bq27541 %s: i=%d\n", __func__, i);

	if ( i == SEAL_POLLING_RETRY_LIMIT) {
		pr_err("bq27541 %s failed\n", __func__);
		return 0;
	} else {
		return 1;
	}
}

static void bq27541_reset(struct i2c_client *client)
{
	struct opchg_bms_charger *di = i2c_get_clientdata(client);

	if (bq27541_get_battery_mvolts() <= 3250 * 1000 
			&& bq27541_get_battery_mvolts() > 2500 * 1000
			&& bq27541_get_battery_soc() == 0 
			&& bq27541_get_battery_temperature() > 150) {
		if (!unseal(di, BQ27541_UNSEAL_KEY)) {
			pr_err( "bq27541 unseal fail !\n");
			return;
		}
		pr_err( "bq27541 unseal OK !\n");
		
		control_cmd_write(di, RESET_SUBCMD);
	}
	return;
}

static const struct of_device_id bq27541_match[] = {
	{ .compatible = "ti,bq27541-battery" },
	{ },
};

static const struct i2c_device_id bq27541_id[] = {
	{ "bq27541-battery", 1 },
	{},
};
MODULE_DEVICE_TABLE(i2c, BQ27541_id);


static struct i2c_driver bq27541_battery_driver = {
	.driver		= {
		.name = "bq27541-battery",
		.owner	= THIS_MODULE,
		.of_match_table = bq27541_match,
	},
	.probe		= bq27541_battery_probe,
	.remove		= bq27541_battery_remove,
	.shutdown	= bq27541_reset,
	.suspend	= bq27541_battery_suspend ,
	.resume		= bq27541_battery_resume,
	.id_table	= bq27541_id,
};
module_i2c_driver(bq27541_battery_driver);


MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Qualcomm Innovation Center, Inc.");
MODULE_DESCRIPTION("BQ27541 battery monitor driver");


