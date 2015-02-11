/*******************************************************************************
* Copyright (c)  2014- 2014  Guangdong OPPO Mobile Telecommunications Corp., Ltd
* VENDOR_EDIT
* Description: Source file for CBufferList.
*           To allocate and free memory block safely.
* Version   : 0.0
* Date      : 2014-07-30
* Author    : Lijiada @Bsp.charge
* ---------------------------------- Revision History: -------------------------
* <version>           <date>          < author >              <desc>
* Revision 0.0        2014-07-30      Lijiada @Bsp.charge
* Modified to be suitable to the new coding rules in all functions.
*******************************************************************************/

#ifndef _OPPO_DEF_H_
#define _OPPO_DEF_H_

enum {
    OPCHG_CHG_TEMP_PRESENT = 0,
    OPCHG_CHG_TEMP_COLD,
    OPCHG_CHG_TEMP_COOL,
    OPCHG_CHG_TEMP_PRE_COOL,
    OPCHG_CHG_TEMP_NORMAL,
    OPCHG_CHG_TEMP_WARM,
    OPCHG_CHG_TEMP_HOT,
};

typedef enum   
{
	/*! Battery is absent               */
    CV_BATTERY_TEMP_REGION__ABSENT,
    /*! Battery is cold               */
    CV_BATTERY_TEMP_REGION__COLD,
    /*! Battery is little cold               */
    CV_BATTERY_TEMP_REGION__LITTLE_COLD,
    /*! Battery is cool              */
    CV_BATTERY_TEMP_REGION__COOL,
    /*! Battery is cool               */
    CV_BATTERY_TEMP_REGION__LITTLE_COOL,
    /*! Battery is normal             */
    CV_BATTERY_TEMP_REGION__NORMAL,
    /*! Battery is warm               */
    CV_BATTERY_TEMP_REGION__WARM,
    /*! Battery is hot                */
    CV_BATTERY_TEMP_REGION__HOT,
    /*! Invalid battery temp region   */
    CV_BATTERY_TEMP_REGION__INVALID,
} chg_cv_battery_temp_region_type;

enum {
    INPUT_CURRENT_MIN,
    INPUT_CURRENT_BY_POWER,
    INPUT_CURRENT_BY_FASTER_2PHASE,
    INPUT_CURRENT_BY_VOOC,
    INPUT_CURRENT_LCD,
    INPUT_CURRENT_CAMERA,
    INPUT_CURRENT_CMCC,
    INPUT_CURRENT_MAX
};

enum {
    FAST_CURRENT_MIN,
    FAST_CURRENT_TEMP,
    FAST_CURRENT_2CHARGER,
    FAST_CURRENT_LCD,
    FAST_CURRENT_CAMERA,
    FAST_CURRENT_CUSTOM1,
    FAST_CURRENT_CUSTOM2,
    FAST_CURRENT_CMCC,
    FAST_CURRENT_MAX
};

enum {
    TERM_VOL_MIN,
    TERM_VOL_TEMP,
    TERM_VOL_BAT,
    TERM_VOL_TAPER_PHASE,
    TERM_VOL_CUSTOM1,
    TERM_VOL_CUSTOM2,
    TERM_VOL_MAX
};

enum {
    TERM_CURRENT_MIN,
    TERM_CURRENT_NORMAL,
    TERM_CURRENT_TAPER_PHASE,
    TERM_CURRENT_MAX
};

enum {
    USER_DISABLE        = BIT(0),
    THERMAL_DISABLE     = BIT(1),
    CURRENT_DISABLE     = BIT(2),
    CHAGER_ERR_DISABLE  = BIT(3),
    CHAGER_OUT_DISABLE  = BIT(4),
    CHAGER_OTG_DISABLE  = BIT(5),
    CHAGER_VOOC_DISABLE = BIT(6),
    CHAGER_TIMEOUT_DISABLE = BIT(7),
};

enum {
    CHARGER_RESET_BY_TEMP_COOL      = BIT(0),
    CHARGER_RESET_BY_TEMP_PRE_COOL  = BIT(1),
    CHARGER_RESET_BY_TEMP_NORMAL    = BIT(2),
    CHARGER_RESET_BY_TEMP_WARM      = BIT(3),
    CHARGER_RESET_BY_VOOC           = BIT(4),
};

enum {
    FACTORY_ENABLE      = BIT(0),
    CHAGER_ERR_ENABLE   = BIT(1),
};

enum {
	OVERTIME_AC = 0,
	OVERTIME_USB,
	OVERTIME_DISABLED,
};

enum {
    PRE_PHASE,
    CC_PHASE,
    CV_PHASE,
    TERM_PHASE,
    RECHARGE_PHASE,
    ERRO_PHASE,
};

enum {
    FASTER_NONE,
    FASTER_1PHASE,
    FASTER_2PHASE,
};

enum {
	USER	= BIT(0),
	THERMAL = BIT(1),
	CURRENT = BIT(2),
	SOC 	= BIT(3),
};

#define OPCHG_THREAD_INTERVAL                   5000//5S
#define OPCHG_THREAD_INTERVAL_INIT              1000//1S

/* constants */
#define USB2_MIN_CURRENT_MA                     100
#define USB2_MAX_CURRENT_MA                     500
#define USB3_MAX_CURRENT_MA                     900

#define OPCHG_FAST_CHG_MAX_MA                   2000
#define OPCHG_IMPUT_CURRENT_LIMIT_MAX_MA        2000


struct opchg_regulator {
    struct regulator_desc           rdesc;
    struct regulator_dev            *rdev;
};

struct opchg_charger {
    struct i2c_client               *client;
    struct device                   *dev;
    struct mutex                    read_write_lock;
    bool                            charger_inhibit_disabled;
	bool							recharge_disabled;
    int                             recharge_mv;
    int                             float_compensation_mv;
    bool                            iterm_disabled;
    int                             iterm_ma;
    int                             vfloat_mv;
    int                             chg_valid_gpio;
    int                             chg_valid_act_low;
    int                             chg_present;
    int                             fake_battery_soc;
    bool                            chg_autonomous_mode;
    bool                            disable_apsd;
    bool                            battery_missing;
    const char                      *bms_psy_name;
	bool							bms_controlled_charging;
	bool							using_pmic_therm;
	int								charging_disabled_status;
    
    /* status tracking */
    bool                            batt_pre_full;
    bool                            batt_full;
    bool                            batt_hot;
    bool                            batt_cold;
    bool                            batt_warm;
    bool                            batt_cool;
    bool                            charge_voltage_over;
    bool                            batt_voltage_over;
#ifdef VENDOR_EDIT
    bool                            multiple_test;
#endif
    bool                            suspending;
    #if 0
    bool                            action_pending;
    #endif
    bool                            otg_enable_pending;
    bool                            otg_disable_pending;
    
    int                             charging_disabled;
    int                             fastchg_current_max_ma;
    int                             fastchg_current_ma;
    int                             limit_current_max_ma;
    int                             faster_normal_limit_current_max_ma;
    bool                            charging_time_out;
    int                             charging_total_time;
    int                             charging_opchg_temp_statu;
    int                             temp_vfloat_mv;
    int                             workaround_flags;
    
	struct power_supply				dc_psy;
    struct power_supply             *usb_psy;
    struct power_supply             *bms_psy;
    struct power_supply             batt_psy;
    
    struct delayed_work             update_opchg_thread_work;
    struct delayed_work             opchg_delayed_wakeup_work;
    struct wakeup_source            source;
    
    struct opchg_regulator         otg_vreg;
    
    struct dentry                   *debug_root;
    u32                             peek_poke_address;
    
    struct qpnp_vadc_chip           *vadc_dev;
    struct qpnp_adc_tm_chip         *adc_tm_dev;
    struct qpnp_adc_tm_btm_param    adc_param;
    int                             hot_bat_decidegc;
    int                             warm_bat_decidegc;
    int                             pre_cool_bat_decidegc;
    int                             cool_bat_decidegc;
    int                             cold_bat_decidegc;
    int                             bat_present_decidegc;
    int                             temp_cool_vfloat_mv;
    int                             temp_cool_fastchg_current_ma;
    int                             temp_pre_cool_vfloat_mv;
    int                             temp_pre_cool_fastchg_current_ma;
    int                             temp_warm_vfloat_mv;
    int                             temp_warm_fastchg_current_ma;
	int								non_standard_vfloat_mv;
	int								non_standard_fastchg_current_ma;
    struct regulator*               vcc_i2c;
    int                             irq_gpio;
	int								usbin_switch_gpio;
	int								batt_id_gpio;
	bool							batt_authen;
    //int                             usbphy_on_gpio;
    int                             fastcharger;
	int								fast_charge_project;
    int                             driver_id;
    bool                            g_is_changed;
    int                             g_is_vooc_changed;
    int                             vooc_start_step;
	bool								opchg_earphone_enable;
    int                             g_is_reset_changed;
    int                             max_input_current[INPUT_CURRENT_MAX+1];;
    int                             max_fast_current[FAST_CURRENT_MAX+1];
    int                             max_term_current[TERM_CURRENT_MAX+1];
    int                             min_term_voltage[TERM_VOL_MAX+1];
    int                             disabled_status;
    int                             reseted_status;
    int                            	suspend_status;
    int                             overtime_status;
    int                             charging_phase;
    int                             fastcharger_type;
    int                             fastcharger_status;
    int                             pre_fastcharger_status;
    int                             taper_vfloat_mv;
    int                             fast_taper_fastchg_current_ma;
    int                             charger_ov_status;
    int                             g_is_wakeup;
    int                             g_chg_in;
    u8                              bat_temp_status;
    int                             temperature;
    int                             bat_instant_vol;
    int                             charging_current;
    int                             charger_vol;
	int								battery_vol;
	u8								soc_bms;
    u8                              bat_volt_check_point;
	int								ocv_uv;
    bool                            is_charging;
    bool                            bat_exist;
    u8                              bat_status;
    u8                              bat_charging_state;
    u8                              battery_request_poweroff;
    bool                            is_charger_det;

	int                             charger_type;
	int								battery_low_vol;
	int								boot_mode;						

	bool                            is_lcd_on;
	bool                            is_camera_on;

	//Liao Fuchun add for bq24196 wdt enable/disable
	bool							wdt_enable;
	//Liao Fuchun add for batt temp
	int								little_cool_bat_decidegc;
	int								normal_bat_decidegc;
	int								temp_cold_vfloat_mv;
	int								temp_cold_fastchg_current_ma;
	int								temp_little_cool_vfloat_mv;
	int								temp_little_cool_fastchg_current_ma;
	int								temp_normal_vfloat_mv;
	int								temp_normal_fastchg_current_ma;
	int								mBatteryTempRegion;
	int								mBatteryTempBoundT0;
	int								mBatteryTempBoundT1;
	int								mBatteryTempBoundT2;
	int								mBatteryTempBoundT3;
	int								mBatteryTempBoundT4;
	int								mBatteryTempBoundT5;
	int								mBatteryTempBoundT6;
	
};

struct opchg_gpio_control {
	int								opchg_swtich1_gpio;
	int								opchg_swtich2_gpio;
	int								opchg_reset_gpio;
	int								opchg_clock_gpio;
	int								opchg_data_gpio;
	int                             opchg_fastcharger;
};

struct opchg_fast_charger {
    struct i2c_client               	*client;
    struct device                   	*dev;
	
    struct mutex                    	fast_read_write_lock;
    struct delayed_work             	update_opfastchg_thread_work;
    struct delayed_work             	opfastchg_delayed_wakeup_work;
	
    struct regulator*               	vcc_i2c;
    int                             	opchg_fast_driver_id;
    int                             	g_fast_charging_wakeup;
};

struct opchg_bms_charger;
struct bq27541_access_methods {
	int (*read)(u8 reg, int *rt_value, int b_single,
		struct opchg_bms_charger *di);
};

//struct bq27541_device_info {
struct opchg_bms_charger {
	struct i2c_client					*client;
	struct device						*dev;

	struct bq27541_access_methods			*bus;
	
	int				id;
	

	struct work_struct		counter;
	/* 300ms delay is needed after bq27541 is powered up
	 * and before any successful I2C transaction
	 */
	struct  delayed_work		hw_config;
	int soc_pre;
	int temp_pre;
	int batt_vol_pre;
	int current_pre;
	int saltate_counter;
	bool is_authenticated;	//wangjc add for authentication
	bool fast_chg_started;
	bool fast_switch_to_normal;
	bool fast_normal_to_warm;	//lfc add for fastchg over temp
	int battery_type;			//lfc add for battery type
	struct power_supply		*batt_psy;
	int irq;
	struct work_struct fastcg_work;
	bool alow_reading;
	struct timer_list watchdog;
	struct wake_lock fastchg_wake_lock;
	bool fast_chg_allow;
	bool fast_low_temp_full;
	int retry_count;
	unsigned long rtc_resume_time;
	unsigned long rtc_suspend_time;
	unsigned long rtc_refresh_time;
	atomic_t suspended;
	bool fast_chg_ing;

	
	int								opchg_swtich1_gpio;
	int								opchg_swtich2_gpio;
	int								opchg_reset_gpio;
	int								opchg_clock_gpio;
	int								opchg_data_gpio;

	struct pinctrl 						*pinctrl;
	struct pinctrl_state 					*gpio_switch1_act_switch2_act;
	struct pinctrl_state 					*gpio_switch1_sleep_switch2_sleep;
	struct pinctrl_state 					*gpio_switch1_act_switch2_sleep;
	struct pinctrl_state 					*gpio_switch1_sleep_switch2_act;
	
	struct pinctrl_state 					*gpio_clock_active;
	struct pinctrl_state 					*gpio_clock_sleep;
	struct pinctrl_state 					*gpio_data_active;
	struct pinctrl_state 					*gpio_data_sleep;
	struct pinctrl_state 					*gpio_reset_active;
	struct pinctrl_state 					*gpio_reset_sleep;
	
};


#endif /*_OPPO_DEF_H_*/
