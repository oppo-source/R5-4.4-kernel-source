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

#define OPPO_BATTERY_PAR
#include <oppo_inc.h>


int opchg_inout_charge_parameters(struct opchg_charger *chip)
{
    int rc = 0;
    
    chip->batt_voltage_over = false;
    chip->charge_voltage_over = false;
    chip->charging_time_out = false;
    chip->batt_pre_full = 0;
    chip->batt_full = 0;

	chip->is_lcd_on=true;
	chip->is_camera_on=false;
	
    chip->disabled_status = 0;
    chip->reseted_status = 0;
    chip->g_is_reset_changed = false;
	chip->suspend_status = 0;
	
    chip->overtime_status = OVERTIME_DISABLED;
    chip->charging_total_time = 0;
    chip->charging_phase = PRE_PHASE;
    chip->fastcharger_type = false;
    chip->fastcharger_status = FASTER_NONE;
    chip->pre_fastcharger_status = chip->fastcharger_status;
    chip->charger_ov_status = false;
    
    chip->charging_opchg_temp_statu = OPCHG_CHG_TEMP_NORMAL;
	chip->adc_param.low_temp = chip->pre_cool_bat_decidegc;
    chip->adc_param.high_temp = chip->warm_bat_decidegc;
	chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
	chip->battery_missing = false;
	chip->batt_hot = false;
	chip->batt_cold = false;
	chip->bat_temp_status = POWER_SUPPLY_HEALTH_GOOD;
	chip->mBatteryTempRegion = CV_BATTERY_TEMP_REGION__NORMAL;
	chip->mBatteryTempBoundT0 = chip->bat_present_decidegc;
	chip->mBatteryTempBoundT1 = chip->cold_bat_decidegc;
	chip->mBatteryTempBoundT2 = chip->cool_bat_decidegc;
	chip->mBatteryTempBoundT3 = chip->little_cool_bat_decidegc;
	chip->mBatteryTempBoundT4 = chip->normal_bat_decidegc;
	chip->mBatteryTempBoundT5 = chip->warm_bat_decidegc;
	chip->mBatteryTempBoundT6 = chip->hot_bat_decidegc;
	opchg_config_term_voltage(chip, TERM_VOL_TEMP, chip->vfloat_mv);
    opchg_config_fast_current(chip, FAST_CURRENT_TEMP, chip->fastchg_current_max_ma);
    opchg_config_charging_disable(chip, THERMAL_DISABLE, 0);
    
    opchg_config_charging_disable(chip, CHAGER_TIMEOUT_DISABLE, 0);

    chip->vooc_start_step = 1;
	
	
    return rc;
}

int opchg_init_charge_parameters(struct opchg_charger *chip)
{
    int i,rc = 0;
    
    chip_opchg = chip;
    
    chip->fastcharger= 0;
	chip->is_charger_det= 0;

    chip->charger_vol = 0;
    chip->is_charging = false;
    chip->charging_current = 0;
    
    chip->bat_exist = true;
	chip->bat_status = POWER_SUPPLY_STATUS_UNKNOWN;
	chip->bat_instant_vol = 3800; 
	chip->temperature = 250;
	
	chip->bat_charging_state = POWER_SUPPLY_CHARGE_TYPE_NONE;
	chip->bat_temp_status = POWER_SUPPLY_HEALTH_GOOD;
	chip->battery_request_poweroff = 0;
	
	chip->bat_volt_check_point = 50;
    
    chip->g_is_wakeup = -1;
    chip->g_chg_in = -1;
	chip->opchg_earphone_enable =false;

    for (i = INPUT_CURRENT_MIN;i <= INPUT_CURRENT_MAX;i++) {
        chip->max_input_current[i] = chip->limit_current_max_ma;
    }
    for (i = FAST_CURRENT_MIN;i <= FAST_CURRENT_MAX;i++) {
        chip->max_fast_current[i] = chip->fastchg_current_max_ma;
    }
    for (i = TERM_VOL_MIN;i <= TERM_VOL_MAX;i++) {
        chip->min_term_voltage[i] = chip->vfloat_mv;
    }
    for (i = TERM_CURRENT_MIN;i <= TERM_CURRENT_MAX;i++) {
        chip->max_term_current[i] = chip->iterm_ma;
    }
    
    rc = opchg_inout_charge_parameters(chip);
    
    return rc;
}

void opchg_get_tm_adc(struct opchg_charger *chip, enum qpnp_tm_state state, int temp)
{
    if (state == ADC_TM_WARM_STATE) {
        if (temp > chip->hot_bat_decidegc) {
            /* warm to hot */
            chip->charging_opchg_temp_statu = OPCHG_CHG_TEMP_HOT;
            
			chip->adc_param.low_temp = chip->hot_bat_decidegc - HYSTERISIS_DECIDEGC;
            chip->adc_param.state_request = ADC_TM_COOL_THR_ENABLE;
        } else if (temp >= chip->warm_bat_decidegc) {
            /* normal to warm */
            chip->charging_opchg_temp_statu = OPCHG_CHG_TEMP_WARM;
            
            chip->adc_param.low_temp = chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC;
            chip->adc_param.high_temp = chip->hot_bat_decidegc;
            chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
        } else if (temp >= chip->pre_cool_bat_decidegc) {
            /* pre_cool to normal */
            chip->charging_opchg_temp_statu = OPCHG_CHG_TEMP_NORMAL;
            
            chip->adc_param.low_temp = chip->pre_cool_bat_decidegc;
            chip->adc_param.high_temp = chip->warm_bat_decidegc;
            chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
        } else if (temp >= chip->cool_bat_decidegc) {
            /* cool to pre_cool */
            chip->charging_opchg_temp_statu = OPCHG_CHG_TEMP_PRE_COOL;
            
            chip->adc_param.low_temp = chip->cool_bat_decidegc;
            chip->adc_param.high_temp = chip->pre_cool_bat_decidegc + HYSTERISIS_DECIDEGC;
            chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
        } else if (temp >= chip->cold_bat_decidegc) {
            /* cold to cool */
            chip->charging_opchg_temp_statu = OPCHG_CHG_TEMP_COOL;
            
            chip->adc_param.low_temp = chip->cold_bat_decidegc;
            chip->adc_param.high_temp = chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC;
            chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
        } else if (temp >= chip->bat_present_decidegc) {
            /* Present to cold */
            chip->charging_opchg_temp_statu = OPCHG_CHG_TEMP_COLD;
            
            chip->adc_param.low_temp = chip->bat_present_decidegc;
            chip->adc_param.high_temp = chip->cold_bat_decidegc + HYSTERISIS_DECIDEGC;
            chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
        }
    }
    else {
        if (temp < chip->bat_present_decidegc) {
            /* Cold to present */
            chip->charging_opchg_temp_statu = OPCHG_CHG_TEMP_PRESENT;
			
            chip->adc_param.high_temp = chip->bat_present_decidegc;
            chip->adc_param.state_request = ADC_TM_WARM_THR_ENABLE;
        } else if (chip->bat_present_decidegc <= temp && temp < chip->cold_bat_decidegc) {
            /* cool to cold */
            chip->charging_opchg_temp_statu = OPCHG_CHG_TEMP_COLD;
			
            chip->adc_param.high_temp = chip->cold_bat_decidegc + HYSTERISIS_DECIDEGC;
            chip->adc_param.low_temp = chip->bat_present_decidegc;
            chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
        } else if (chip->cold_bat_decidegc <= temp && temp < chip->cool_bat_decidegc) {
            /* pre_cool to cool */
            chip->charging_opchg_temp_statu = OPCHG_CHG_TEMP_COOL;
			
            chip->adc_param.high_temp = chip->cool_bat_decidegc + HYSTERISIS_DECIDEGC;
            chip->adc_param.low_temp = chip->cold_bat_decidegc;
            chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
        } else if (chip->cool_bat_decidegc <= temp && temp < chip->pre_cool_bat_decidegc) {
            /* normal to pre_cool */
            chip->charging_opchg_temp_statu = OPCHG_CHG_TEMP_PRE_COOL;
			
            chip->adc_param.high_temp = chip->pre_cool_bat_decidegc + HYSTERISIS_DECIDEGC;
            chip->adc_param.low_temp = chip->cool_bat_decidegc;
            chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
        } else if (chip->pre_cool_bat_decidegc <= temp && temp < chip->warm_bat_decidegc) {
            /* warm to normal */
            chip->charging_opchg_temp_statu = OPCHG_CHG_TEMP_NORMAL;
			
            chip->adc_param.high_temp = chip->warm_bat_decidegc;
            chip->adc_param.low_temp = chip->pre_cool_bat_decidegc;
            chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
		} else if (temp <= chip->hot_bat_decidegc) {
            /* hot to Warm */
            chip->charging_opchg_temp_statu = OPCHG_CHG_TEMP_WARM;
            
            chip->adc_param.high_temp = chip->hot_bat_decidegc;
            chip->adc_param.low_temp = chip->warm_bat_decidegc - HYSTERISIS_DECIDEGC;
            chip->adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;
        }
    }
    
    if (OPCHG_CHG_TEMP_WARM == chip->charging_opchg_temp_statu){
        opchg_config_term_voltage(chip, TERM_VOL_TEMP, chip->temp_warm_vfloat_mv);
        opchg_config_fast_current(chip, FAST_CURRENT_TEMP, chip->temp_warm_fastchg_current_ma);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_WARM, 1);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_NORMAL, 0);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_PRE_COOL, 0);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_COOL, 0);
    }
    else if (OPCHG_CHG_TEMP_NORMAL == chip->charging_opchg_temp_statu){
        opchg_config_term_voltage(chip, TERM_VOL_TEMP, chip->vfloat_mv);
        opchg_config_fast_current(chip, FAST_CURRENT_TEMP, chip->fastchg_current_max_ma);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_WARM, 0);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_NORMAL, 1);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_PRE_COOL, 0);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_COOL, 0);
    }
    else if (OPCHG_CHG_TEMP_PRE_COOL == chip->charging_opchg_temp_statu){
        opchg_config_term_voltage(chip, TERM_VOL_TEMP, chip->temp_pre_cool_vfloat_mv);
        opchg_config_fast_current(chip, FAST_CURRENT_TEMP, chip->temp_pre_cool_fastchg_current_ma);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_WARM, 0);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_NORMAL, 0);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_PRE_COOL, 1);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_COOL, 0);
    }
    else if ((OPCHG_CHG_TEMP_COOL == chip->charging_opchg_temp_statu))
    {
        opchg_config_term_voltage(chip, TERM_VOL_TEMP, chip->temp_cool_vfloat_mv);
        opchg_config_fast_current(chip, FAST_CURRENT_TEMP, chip->temp_cool_fastchg_current_ma);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_WARM, 0);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_NORMAL, 0);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_PRE_COOL, 0);
        opchg_config_reset_charger(chip, CHARGER_RESET_BY_TEMP_COOL, 1);
    }
    
    if (OPCHG_CHG_TEMP_PRESENT == chip->charging_opchg_temp_statu) {
        chip->battery_missing = true;
    }
	else {
        chip->battery_missing = false;
    }
    if (OPCHG_CHG_TEMP_HOT == chip->charging_opchg_temp_statu) {
        chip->batt_hot = true;
    }
    else {
        chip->batt_hot = false;
    }
    if (OPCHG_CHG_TEMP_COLD == chip->charging_opchg_temp_statu) {
        chip->batt_cold = true;
    }
    else {
        chip->batt_cold = false;
    }
    
    if (chip->batt_hot || chip->batt_cold || chip->battery_missing){
        opchg_config_charging_disable(chip, THERMAL_DISABLE, 1);
    }
    else{
        opchg_config_charging_disable(chip, THERMAL_DISABLE, 0);
    }
    
    pr_debug("hot %d, cold %d, missing %d, low = %d deciDegC, high = %d deciDegC\n",
                            chip->batt_hot, chip->batt_cold, chip->battery_missing,
                            chip->adc_param.low_temp, chip->adc_param.high_temp);
	
	#ifdef OPPO_USE_ADC_TM_IRQ
    if (qpnp_adc_tm_channel_measure(chip->adc_tm_dev, &chip->adc_param)) {
        pr_err("request ADC error\n");
    }
	#endif
}

#ifdef OPPO_USE_ADC_TM_IRQ
void opchg_get_adc_notification(enum qpnp_tm_state state, void *ctx)
{
    struct opchg_charger *chip = ctx;
    int temp;
    
    if (state >= ADC_TM_STATE_NUM) {
        pr_err("invallid state parameter %d\n", state);
        return;
    }
    
    temp = opchg_get_prop_batt_temp(chip);
    
	pr_debug("temp = %d state = %s\n", temp, state == ADC_TM_WARM_STATE ? "hot" : "cold");
	
	opchg_get_tm_adc(chip, state, temp);
}
#else
void opchg_get_adc_notification(struct opchg_charger *chip)
{
    int temp;
    int state = ADC_TM_STATE_NUM;
    
    temp = opchg_get_prop_batt_temp(chip);

    if (ADC_TM_COOL_THR_ENABLE == chip->adc_param.state_request) {
        if (temp < chip->adc_param.low_temp) {
            state = ADC_TM_COOL_STATE;
        }
        else {
            return;
        }
    }
    else if (ADC_TM_WARM_THR_ENABLE == chip->adc_param.state_request) {
        if (temp > chip->adc_param.high_temp) {
            state = ADC_TM_WARM_STATE;
        }
        else {
            return;
        }
    }
    else if (ADC_TM_HIGH_LOW_THR_ENABLE == chip->adc_param.state_request) {
        if (temp < chip->adc_param.low_temp) {
            state = ADC_TM_COOL_STATE;
        }
        else if (temp > chip->adc_param.high_temp) {
            state = ADC_TM_WARM_STATE;
        }
        else {
            return;
        }
    }
    else {
        pr_err("invallid state parameter %d\n", state);
        return;
    }
    
	pr_debug("temp = %d state = %s\n", temp, state == ADC_TM_WARM_STATE ? "hot" : "cold");
	
	opchg_get_tm_adc(chip, state, temp);
}
#endif

#if 1
//lfc 2014-11-17 add for new standard of charge
void opchg_battery_temp_region_set(struct opchg_charger *chip,int batt_temp_region)
{
	chip->mBatteryTempRegion = batt_temp_region;
	
	if(chip->mBatteryTempRegion == CV_BATTERY_TEMP_REGION__ABSENT)
		chip->battery_missing = true;
	else
		chip->battery_missing = false;
	
	if(chip->mBatteryTempRegion == CV_BATTERY_TEMP_REGION__HOT)
		chip->batt_hot = true;
	else
		chip->batt_hot = false;
	
	if(chip->mBatteryTempRegion == CV_BATTERY_TEMP_REGION__COLD)
		chip->batt_cold = true;
	else
		chip->batt_cold = false;
}

int	opchg_battery_temp_region_get(struct opchg_charger *chip)
{
	return chip->mBatteryTempRegion;
}

void opchg_update_temp_boundaries(struct opchg_charger *chip,int t0_hys,int t1_hys,
			int t2_hys,int t3_hys,int t4_hys,int t5_hys,int t6_hys)
{
	chip->mBatteryTempBoundT0 = chip->bat_present_decidegc - t0_hys;
	chip->mBatteryTempBoundT1 = chip->cold_bat_decidegc - t1_hys;
	chip->mBatteryTempBoundT2 = chip->cool_bat_decidegc - t2_hys;
	chip->mBatteryTempBoundT3 = chip->little_cool_bat_decidegc - t3_hys;
	chip->mBatteryTempBoundT4 = chip->normal_bat_decidegc - t4_hys;
	chip->mBatteryTempBoundT5 = chip->warm_bat_decidegc - t5_hys;
	chip->mBatteryTempBoundT6 = chip->hot_bat_decidegc - t6_hys;
}

void opchg_get_adc_notification_poll_new_standard(struct opchg_charger *chip)
{
	int temp = opchg_get_prop_batt_temp(chip);

	if(!chip->chg_present)
		return ;
	
	pr_info("temp:%d,region:%d\n",temp,opchg_battery_temp_region_get(chip));
	//pr_info("t0:%d,t1:%d,t2:%d,t3:%d,t4:%d,t5:%d,t6:%d\n",chip->mBatteryTempBoundT0,chip->mBatteryTempBoundT1,chip->mBatteryTempBoundT2,
	//	chip->mBatteryTempBoundT3,chip->mBatteryTempBoundT4,chip->mBatteryTempBoundT5,chip->mBatteryTempBoundT6);
	if(temp < chip->mBatteryTempBoundT0){		// < -20
		if(opchg_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__ABSENT)
			return ;
		opchg_config_charging_disable(chip, THERMAL_DISABLE, 1);
		opchg_battery_temp_region_set(chip,CV_BATTERY_TEMP_REGION__ABSENT);
		opchg_update_temp_boundaries(chip,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE);
	} else if(temp < chip->mBatteryTempBoundT1){	//-20 ~ -10
		if(opchg_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__COLD)
			return ;
		opchg_config_charging_disable(chip, THERMAL_DISABLE, 1);
		opchg_battery_temp_region_set(chip,CV_BATTERY_TEMP_REGION__COLD);
		opchg_update_temp_boundaries(chip,HYSTERISIS_DECIDEGC,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE);
	} else if(temp < chip->mBatteryTempBoundT2){	//-10 ~ 0
		if(opchg_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__LITTLE_COLD)
			return ;
		opchg_config_term_voltage(chip, TERM_VOL_TEMP, chip->temp_cold_vfloat_mv);
        opchg_config_fast_current(chip, FAST_CURRENT_TEMP, chip->temp_cold_fastchg_current_ma);
		opchg_config_charging_disable(chip, THERMAL_DISABLE, 0);
		opchg_battery_temp_region_set(chip,CV_BATTERY_TEMP_REGION__LITTLE_COLD);
		opchg_update_temp_boundaries(chip,HYS_NONE,HYSTERISIS_DECIDEGC,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE);
	} else if(temp < chip->mBatteryTempBoundT3){	//0 ~ 15
		if(opchg_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__COOL)
			return ;
		opchg_config_term_voltage(chip, TERM_VOL_TEMP, chip->temp_cool_vfloat_mv);
        opchg_config_fast_current(chip, FAST_CURRENT_TEMP, chip->temp_cool_fastchg_current_ma);
		opchg_config_charging_disable(chip, THERMAL_DISABLE, 0);
		opchg_battery_temp_region_set(chip,CV_BATTERY_TEMP_REGION__COOL);
		opchg_update_temp_boundaries(chip,HYS_NONE,HYS_NONE,HYSTERISIS_DECIDEGC,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE);
	} else if(temp < chip->mBatteryTempBoundT4){	//15 ~ 20
		if(opchg_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__LITTLE_COOL)
			return ;
		opchg_config_term_voltage(chip, TERM_VOL_TEMP, chip->temp_little_cool_vfloat_mv);
        opchg_config_fast_current(chip, FAST_CURRENT_TEMP, chip->temp_little_cool_fastchg_current_ma);
		opchg_config_charging_disable(chip, THERMAL_DISABLE, 0);
		opchg_battery_temp_region_set(chip,CV_BATTERY_TEMP_REGION__LITTLE_COOL);
		opchg_update_temp_boundaries(chip,HYS_NONE,HYS_NONE,HYS_NONE,HYSTERISIS_DECIDEGC,HYS_NONE,HYS_NONE,HYS_NONE);
	} else if(temp < chip->mBatteryTempBoundT5){	//20 ~ 45
		if(opchg_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__NORMAL)
			return ;
		opchg_config_term_voltage(chip, TERM_VOL_TEMP, chip->vfloat_mv);
        opchg_config_fast_current(chip, FAST_CURRENT_TEMP, chip->fastchg_current_max_ma);
		opchg_config_charging_disable(chip, THERMAL_DISABLE, 0);
		opchg_battery_temp_region_set(chip,CV_BATTERY_TEMP_REGION__NORMAL);
		opchg_update_temp_boundaries(chip,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE,HYSTERISIS_DECIDEGC,HYS_NONE,HYS_NONE);
	} else if(temp < chip->mBatteryTempBoundT6){	//45 ~ 55
		if(opchg_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__WARM)
			return ;
		opchg_config_term_voltage(chip, TERM_VOL_TEMP, chip->temp_warm_vfloat_mv);
        opchg_config_fast_current(chip, FAST_CURRENT_TEMP, chip->temp_warm_fastchg_current_ma);
		opchg_config_charging_disable(chip, THERMAL_DISABLE, 0);
		opchg_battery_temp_region_set(chip,CV_BATTERY_TEMP_REGION__WARM);
		opchg_update_temp_boundaries(chip,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE,HYSTERISIS_DECIDEGC,HYS_NONE);
	} else {		// > 55,hot
		if(opchg_battery_temp_region_get(chip) == CV_BATTERY_TEMP_REGION__HOT)
			return ;
		opchg_config_charging_disable(chip, THERMAL_DISABLE, 1);
		opchg_battery_temp_region_set(chip,CV_BATTERY_TEMP_REGION__HOT);
		opchg_update_temp_boundaries(chip,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE,HYS_NONE,HYSTERISIS_DECIDEGC);
	}
}
#endif

#define CHARGER_OV_VALUE            5800
#define CHARGER_OV_RESUME_VALUE     5500
void opchg_get_charger_ov_status(struct opchg_charger *chip)
{
    if (chip->charger_ov_status == false) {
        if (chip->charger_vol > CHARGER_OV_VALUE) {
            chip->charger_ov_status = true;
            opchg_config_suspend_enable(chip, CHAGER_ERR_ENABLE, 1);
            power_supply_set_health_state(chip->usb_psy, POWER_SUPPLY_HEALTH_OVERVOLTAGE);
            power_supply_changed(chip->usb_psy);
        }
    }
    else {
        if (chip->charger_vol < CHARGER_OV_RESUME_VALUE) {
            chip->charger_ov_status = false;
            opchg_config_suspend_enable(chip, CHAGER_ERR_ENABLE, 0);
            power_supply_set_health_state(chip->usb_psy, POWER_SUPPLY_HEALTH_GOOD);
            power_supply_changed(chip->usb_psy);
        }
    }
}

#define CHG_CP_COUNT    12
static int opchg_check_charging_full(struct opchg_charger *chip)
{
    static int chg_full_count = 0,chg_full_fg = 0;

	if (is_project(OPPO_14005) || is_project(OPPO_14023)  || is_project(OPPO_14045)|| is_project(OPPO_14037) || is_project(OPPO_14051))
    {
    	opchg_check_charging_pre_full(chip);
	}
	
    if (chip->batt_pre_full) {
        chg_full_count++;
        if (chg_full_count > CHG_CP_COUNT) {
            chg_full_count = CHG_CP_COUNT;
            chip->batt_full = 1;
            if (!chg_full_fg){
                chg_full_fg = 1;
            }
        }
    }
    else {
        chg_full_fg = 0;
        chg_full_count = 0;
    }
	//pr_debug("chip->batt_pre_full=%d,chip->chg_full_count=%d \r\n",chip->batt_pre_full,chg_full_count);

    return 0;
}

void opchg_config_reset_charger(struct opchg_charger *chip, int reason, int reset)
{
    int reseted;
    
    reseted = chip->reseted_status;
    
    if (reset == true) {
        reseted |= reason;
    }
    else {
        reseted &= ~reason;
    }
    
    if (reseted == chip->reseted_status) {
        goto skip;
    }
    
    chip->batt_pre_full = 0;
    chip->batt_full = 0;
    
    chip->g_is_changed = true;
    chip->g_is_reset_changed = true;
    
skip:
    chip->reseted_status = reseted;
}

void opchg_config_input_chg_current(struct opchg_charger *chip, int reason, int mA)
{
    int i,temp;
    
    chip->max_input_current[reason] = mA;
    
    temp = chip->max_input_current[INPUT_CURRENT_MAX];
    for (i=INPUT_CURRENT_MIN+1;i<INPUT_CURRENT_MAX;i++) {
        if (chip->max_input_current[i] < temp) {
            temp = chip->max_input_current[i];
        }
    }
    
    if (chip->max_input_current[INPUT_CURRENT_MIN] != temp) {
        chip->max_input_current[INPUT_CURRENT_MIN] = temp;
        chip->g_is_changed = true;
    }
}

void opchg_config_over_time(struct opchg_charger *chip, int mA)
{
    #ifdef OPPO_CMCC_TEST
    chip->overtime_status = OVERTIME_DISABLED;
	#else
	if (mA > USB2_MAX_CURRENT_MA) {
        //chip->overtime_status = OVERTIME_AC;
        chip->overtime_status = OVERTIME_USB;		//modify for new standard
    }
	else {
        chip->overtime_status = OVERTIME_USB;
    }
	#endif

	chip->g_is_changed = true;
}

void opchg_config_fast_current(struct opchg_charger *chip, int reason, int mA)
{
    int i,temp;
    
    chip->max_fast_current[reason] = mA;
    
    temp = chip->max_fast_current[FAST_CURRENT_MAX];
    for (i=FAST_CURRENT_MIN+1;i<FAST_CURRENT_MAX;i++) {
        if (chip->max_fast_current[i] < temp) {
            temp = chip->max_fast_current[i];
        }
    }
    
    if (chip->max_fast_current[FAST_CURRENT_MIN] != temp) {
        chip->max_fast_current[FAST_CURRENT_MIN] = temp;
        chip->g_is_changed = true;
    }
}

void opchg_config_term_voltage(struct opchg_charger *chip, int reason, int mV)
{
    int i,temp;
    
    chip->min_term_voltage[reason] = mV;
    
    temp = chip->min_term_voltage[TERM_VOL_MAX];
    for (i=TERM_VOL_MIN+1;i<TERM_VOL_MAX;i++) {
        if (chip->min_term_voltage[i] < temp) {
            temp = chip->min_term_voltage[i];
        }
    }
    
    if (chip->min_term_voltage[TERM_VOL_MIN] != temp) {
        chip->min_term_voltage[TERM_VOL_MIN] = temp;
        
        chip->g_is_changed = true;
    }
}

void opchg_config_term_current(struct opchg_charger *chip, int reason, int mA)
{
    int i,temp;
    
    chip->max_term_current[reason] = mA;
    
    temp = chip->max_term_current[TERM_CURRENT_MIN];
    for (i=TERM_CURRENT_MIN+1;i<TERM_CURRENT_MAX;i++) {
        if (chip->max_term_current[i] > temp) {
            temp = chip->max_term_current[i];
        }
    }
    
    if (chip->max_term_current[TERM_CURRENT_MAX] != temp) {
        chip->max_term_current[TERM_CURRENT_MAX] = temp;
        
        chip->g_is_changed = true;
    }
}

void opchg_config_charging_disable(struct opchg_charger *chip, int reason, int disable)
{
    int disabled;
    
    disabled = chip->disabled_status;
    
    if (disable == true) {
        disabled |= reason;
    }
    else {
        disabled &= ~reason;
    }
    
    if (!!disabled == !!chip->disabled_status) {
        goto skip;
    }
    
    chip->g_is_changed = true;
    
skip:
    chip->disabled_status = disabled;
}

void opchg_config_suspend_enable(struct opchg_charger *chip, int reason, bool suspend)
{
    int suspended;
    
    suspended = chip->suspend_status;
    
    if (suspend == true) {
        suspended |= reason;
    }
    else {
        suspended &= ~reason;
    }
    
	if (!!suspended == !!chip->suspend_status) {
        goto skip;
    }
    
    chip->g_is_changed = true;
    
skip:
    chip->suspend_status = suspended;
}

void opchg_config_charging_phase(struct opchg_charger *chip,int phase)
{
    chip->charging_phase = phase;
    
    chip->g_is_changed = true;
}

#ifdef OPPO_USE_2CHARGER
void opchg_get_prop_fastcharger_status(struct opchg_charger *chip)
{
    if (chip->fastcharger_type == true) {
        if (chip->bat_volt_check_point <= 85) {
            chip->fastcharger_status = FASTER_1PHASE;
        }
        else if (chip->bat_volt_check_point <= 88) {
            chip->fastcharger_status = FASTER_2PHASE;
        }
        else {
            chip->fastcharger_status = FASTER_NONE;
        }
    }
    else {
        chip->fastcharger_status = FASTER_NONE;
    }

    pr_debug("chip->fastcharger_type=%d \r\n",chip->fastcharger_type);
    pr_debug("chip->pre_fastcharger_status=%d \r\n",chip->pre_fastcharger_status);
    pr_debug("chip->fastcharger_status=%d \r\n",chip->fastcharger_status);
    
    
    if (chip->pre_fastcharger_status != chip->fastcharger_status) {
        chip->pre_fastcharger_status = chip->fastcharger_status;
        if (chip->fastcharger_status == FASTER_2PHASE) {
            opchg_config_term_voltage(chip, TERM_VOL_TAPER_PHASE, chip->vfloat_mv);
            opchg_config_fast_current(chip, FAST_CURRENT_2CHARGER, chip->fastchg_current_max_ma);
            opchg_config_input_chg_current(chip, INPUT_CURRENT_BY_FASTER_2PHASE, chip->limit_current_max_ma);
        }
        else if (chip->fastcharger_status == FASTER_1PHASE) {
            opchg_config_term_voltage(chip, TERM_VOL_TAPER_PHASE, chip->vfloat_mv);
            opchg_config_fast_current(chip, FAST_CURRENT_2CHARGER, chip->fastchg_current_max_ma);
            opchg_config_input_chg_current(chip, INPUT_CURRENT_BY_FASTER_2PHASE, chip->faster_normal_limit_current_max_ma);
        }
        else {
            opchg_config_term_voltage(chip, TERM_VOL_TAPER_PHASE, chip->taper_vfloat_mv);
            opchg_config_fast_current(chip, FAST_CURRENT_2CHARGER, chip->fast_taper_fastchg_current_ma);
            opchg_config_input_chg_current(chip, INPUT_CURRENT_BY_FASTER_2PHASE, chip->faster_normal_limit_current_max_ma);
        }
        
        chip->g_is_changed = true;
    }
}
#endif

#ifdef OPPO_USE_FAST_CHARGER
//#define AP_SWITCH_USB	GPIO_CFG(96, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA)
//#define AP_SWITCH_FAST	GPIO_CFG(96, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA)

#if 0
bool is_alow_fast_chg(struct opchg_charger *chip)
{
	bool auth = false;
	int temp = 0;
	int cap = 0;
	int chg_type = 0;
	bool low_temp_full = 0;
	
	auth = opchg_get_prop_authenticate(chip);
	temp = opchg_get_prop_batt_temp(chip);//get_prop_batt_temp(chip);
	cap = opchg_get_prop_batt_capacity(chip);//get_prop_capacity(chip);
	chg_type = qpnp_charger_type_get(chip);
	low_temp_full = opchg_get_fast_low_temp_full(chip);//qpnp_get_fast_low_temp_full(chip);
	//pr_err("%s auth:%d,temp:%d,cap:%d,chg_type:%d,low_temp_full:%d\n",__func__,auth,temp,cap,chg_type,low_temp_full);

	if(auth == false)
		return false;
	if(chg_type != POWER_SUPPLY_TYPE_USB_DCP)
		return false;
#ifndef CONFIG_OPPO_DEVICE_FIND7OP
/* jingchun.wang@Onlinerd.Driver, 2014/02/25  Modify for use different temp range of 14001 */
	if(temp < 105)
		return false;
	if((temp < 155) && (low_temp_full == 1)){
		return false;
	}
#else /*CONFIG_OPPO_DEVICE_FIND7OP*/
	if(temp < 205)
		return false;
#endif /*CONFIG_OPPO_DEVICE_FIND7OP*/
	if(temp > 420)
		return false;
	if(cap < 1)
		return false;
	if(cap > 96)
		return false;
	
	if(opchg_get_prop_fast_switch_to_normal(chip) == true){
		//pr_err("%s fast_switch_to_noraml is true\n",__func__);
		return false;
	}
	return true;
}
#else
bool is_alow_fast_chg(struct opchg_charger *chip)
{
	bool auth = false;
	bool low_temp_full = 0;
	
	auth = opchg_get_prop_authenticate(chip);
	low_temp_full = opchg_get_fast_low_temp_full(chip);//qpnp_get_fast_low_temp_full(chip);
    chip->temperature = opchg_get_prop_batt_temp(chip);
	chip->bat_volt_check_point = opchg_get_prop_batt_capacity(chip);
	chip->charger_type = qpnp_charger_type_get(chip);

	//  charging current adaptive  judgment
	#ifdef OPCHARGER_DEBUG_ENABLE 
	pr_err("vooc_debug-->is_alow_fast_chg =%d\n",0);
	#endif
	if(chip->is_charger_det)
	{
		return false;
	}
	
	#ifdef OPCHARGER_DEBUG_ENABLE 
	pr_err("vooc_debug-->is_alow_fast_chg =%d\n",1);
	#endif
	if(auth == false)
		return false;
	
	#ifdef OPCHARGER_DEBUG_ENABLE 
	pr_err("vooc_debug-->is_alow_fast_chg =%d\n",2);
	#endif
	if(chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP)
		return false;
	
	#ifdef OPCHARGER_DEBUG_ENABLE 
	pr_err("vooc_debug-->is_alow_fast_chg =%d\n",3);
	#endif
	if(chip->temperature < 150)
		return false;
	#ifdef OPCHARGER_DEBUG_ENABLE 
	pr_err("vooc_debug-->is_alow_fast_chg =%d\n",4);
	#endif	
	if((chip->temperature < 155) && (low_temp_full == 1)){
		return false;
	}
	#ifdef OPCHARGER_DEBUG_ENABLE 
	pr_err("vooc_debug-->is_alow_fast_chg =%d\n",5);
	#endif
	if(chip->temperature > 470)
		return false;
	
	#ifdef OPCHARGER_DEBUG_ENABLE 
	pr_err("vooc_debug-->is_alow_fast_chg =%d\n",6);
	#endif
	if(chip->bat_volt_check_point < 1)
		return false;
	
	#ifdef OPCHARGER_DEBUG_ENABLE 
	pr_err("vooc_debug-->is_alow_fast_chg =%d\n",7);
	#endif
	if(chip->bat_volt_check_point > 96)
		return false;
	
	#ifdef OPCHARGER_DEBUG_ENABLE 
	pr_err("vooc_debug-->is_alow_fast_chg =%d\n",8);
	#endif
	if(opchg_get_prop_fast_switch_to_normal(chip) == true){
		return false;
	}
	
	#ifdef OPCHARGER_DEBUG_ENABLE 
	pr_err("vooc_debug-->is_alow_fast_chg =%d\n",9);
	#endif
	return true;
}
#endif
#if 0
static void switch_fast_chg(struct opchg_charger *chip)
{
	int ret = 0;
	
	//chek if fast charging
	if(opchg_get_gpio_val(opchg_gpio.opchg_swtich1_gpio)&&opchg_get_gpio_val(opchg_gpio.opchg_swtich2_gpio))
	{
		//pr_debug("fast_chg gpio is high,return\n");
		#ifdef OPCHARGER_DEBUG_ENABLE 
		pr_err("vooc_debug-->fast_chg gpio is high,return =%d\n",true);
		#endif
		return;
	}
	
	//check fast 	
	#ifdef OPCHARGER_DEBUG_ENABLE 
	pr_err("vooc_debug-->opchg_get_prop_fast_chg_allow =%d\n",opchg_get_prop_fast_chg_allow(chip));
	#endif
	if(opchg_get_prop_fast_chg_allow(chip) == false){
		if(is_alow_fast_chg(chip) == true) {
			// add reset mcu 
			//ret =opchg_set_reset_active(opchg_pinctrl_chip);
			ret =opchg_set_reset_active(bq27541_di);
			// set switch
			ret =opchg_set_switch_mode(VOOC_CHARGER_MODE);
			
			if(ret){
				pr_err("%s switch fast error %d\n", __func__, ret);
			}
			opchg_set_fast_chg_allow(chip,true);
		}
	}
	//pr_info("%s end,allow_fast_chg:%d\n",__func__,opchg_get_prop_fast_chg_allow(chip));
}
#endif
#endif

#ifdef OPPO_USE_TIMEOVER_BY_AP
#define TOTAL_TIME_06H              4320// =6*60*60/5
#define TOTAL_TIME_10H              7200// =10*60*60/5
#define AC_CHARGING_TOTAL_TIME      TOTAL_TIME_10H
#define USB_CHARGING_TOTAL_TIME     TOTAL_TIME_10H
void opchg_check_charging_time(struct opchg_charger *chip)
{
    if (is_project(OPPO_14005) || is_project(OPPO_14023)  || is_project(OPPO_14045)|| is_project(OPPO_14037) || is_project(OPPO_14051))
    {
        if (chip->batt_pre_full && chip->batt_full) {
            chip->charging_total_time = 0;
            //pr_debug("opchg_check_charging_time01 charging_total_time = %d\n",chip->charging_total_time);
        }
        else if (chip->chg_present == false) {
            chip->charging_total_time = 0;
            //pr_debug("opchg_check_charging_time02 charging_total_time = %d\n",chip->charging_total_time);
        }
        else {
            chip->charging_total_time ++;
            if (chip->charging_total_time > USB_CHARGING_TOTAL_TIME) {
                chip->charging_total_time = USB_CHARGING_TOTAL_TIME;
            }
            //pr_debug("opchg_check_charging_time03 charging_total_time = %d\n",chip->charging_total_time);
        }
        
        if (chip->overtime_status == OVERTIME_DISABLED) {
            //do nothing
            //pr_debug("opchg_check_charging_time11 charging_total_time = %d\n",chip->charging_total_time);
        }
        else if (chip->overtime_status == OVERTIME_USB) {
            if (chip->charging_total_time >= USB_CHARGING_TOTAL_TIME) {
                chip->charging_time_out = true;
            }
            //pr_debug("opchg_check_charging_time22 charging_total_time = %d\n",chip->charging_total_time);
        }
        else if (chip->overtime_status == OVERTIME_AC) {
            if (chip->charging_total_time >= AC_CHARGING_TOTAL_TIME) {
                chip->charging_time_out = true;
            }
            //pr_debug("opchg_check_charging_time33 charging_total_time = %d\n",chip->charging_total_time);
        }
        
        if (chip->charging_time_out == true) {
            opchg_config_charging_disable(chip, CHAGER_TIMEOUT_DISABLE, 1);
        }
    }
}
#endif

#ifdef VENDOR_EDIT
void opchg_check_lcd_on(void)
{
	if(chip_opchg == NULL)
	{
		return;
	}
	chip_opchg->is_lcd_on=true;
}
EXPORT_SYMBOL(opchg_check_lcd_on);
void opchg_check_lcd_off(void)
{
	if(chip_opchg == NULL)
	{
		return;
	}
	chip_opchg->is_lcd_on=false;
}
EXPORT_SYMBOL(opchg_check_lcd_off);
void opchg_check_camera_on(void)
{
	if(chip_opchg == NULL)
	{
		return;
	}
	chip_opchg->is_camera_on=true;
}
EXPORT_SYMBOL(opchg_check_camera_on);
void opchg_check_camera_off(void)
{
	if(chip_opchg == NULL)
	{
		return;
	}
	chip_opchg->is_camera_on=false;
}
EXPORT_SYMBOL(opchg_check_camera_off);
void opchg_check_earphone_on(void)
{
	if(chip_opchg == NULL)
	{
		return;
	}
	chip_opchg->opchg_earphone_enable=true;
}
EXPORT_SYMBOL(opchg_check_earphone_on);
void opchg_check_earphone_off(void)
{
	if(chip_opchg == NULL)
	{
		return;
	}
	chip_opchg->opchg_earphone_enable=false;
}
EXPORT_SYMBOL(opchg_check_earphone_off);
#endif


void opchg_check_lcd_onoff(struct opchg_charger *chip)
{
	//if((chip->is_lcd_on==true)&&(chip->temperature >= 400))
	if(chip->is_lcd_on==true)
	{
		opchg_config_input_chg_current(chip, INPUT_CURRENT_LCD, LCD_ON_CHARGING_INPUT_CURRENT);
		opchg_config_fast_current(chip, FAST_CURRENT_LCD, LCD_ON_CHARGING_FAST_CURRENT);
	}
	else
	{
		opchg_config_input_chg_current(chip, INPUT_CURRENT_LCD, LCD_OFF_CHARGING_INPUT_CURRENT);
		opchg_config_fast_current(chip, FAST_CURRENT_LCD, LCD_OFF_CHARGING_FAST_CURRENT);
	}
}

void opchg_check_camera_onoff(struct opchg_charger *chip)
{
	//if((chip->is_lcd_on==true)&&(chip->temperature >= 400))
	if(chip->is_camera_on==true)
	{
		opchg_config_input_chg_current(chip, INPUT_CURRENT_CAMERA, CAMERA_ON_CHARGING_INPUT_CURRENT);
		opchg_config_fast_current(chip, FAST_CURRENT_CAMERA, CAMERA_ON_CHARGING_FAST_CURRENT);
	}
	else
	{
		opchg_config_input_chg_current(chip, INPUT_CURRENT_CAMERA, CAMERA_OFF_CHARGING_INPUT_CURRENT);
		opchg_config_fast_current(chip, FAST_CURRENT_CAMERA, CAMERA_OFF_CHARGING_FAST_CURRENT);
	}
}

void opchg_check_status(struct opchg_charger *chip)
{
	static int count_debug=0;

	if(is_project(OPPO_14043) || is_project(14037)){
		if(chip->chg_present && (!gpio_get_value(chip->usbin_switch_gpio)))
			opchg_switch_to_usbin(chip,true);
	}
	
	if(is_project(OPPO_14045))
	{
		opchg_check_lcd_onoff(chip);
	//	opchg_check_camera_onoff(chip);
	}

	#ifdef OPPO_CMCC_TEST
	if(is_project(OPPO_14005)||is_project(OPPO_14045))
	{	
		if(chip->charger_type != POWER_SUPPLY_TYPE_USB_DCP)
		{
			if(chip->bat_instant_vol >= CMCC_CHARGER_FULL_4250MV)
			{
				opchg_config_input_chg_current(chip, INPUT_CURRENT_CMCC, CMCC_FULL_CHARGING_INPUT_CURRENT);
			}
			else if(chip->bat_instant_vol < CMCC_CHARGER_FULL_4208MV)
			{
				opchg_config_input_chg_current(chip, INPUT_CURRENT_CMCC, CMCC_CHARGING_INPUT_CURRENT);
			}	
		}
	}
	#endif
	
    //pr_debug("opchg_check_status = %d\n", 1);
    #ifndef OPPO_USE_ADC_TM_IRQ
	if(is_project(OPPO_14037) || is_project(OPPO_14051)|| is_project(OPPO_14045))
		opchg_get_adc_notification_poll_new_standard(chip);
	else
    	opchg_get_adc_notification(chip);
    #endif
    opchg_get_charger_ov_status(chip);
	
    opchg_get_prop_batt_status(chip);
    opchg_get_charging_status(chip);
    opchg_get_prop_charge_type(chip);

    #ifdef OPPO_USE_2CHARGER
    opchg_get_prop_fastcharger_type(chip);
    opchg_get_prop_fastcharger_status(chip);
    #endif
    chip->bat_temp_status = opchg_get_prop_batt_health(chip);
    chip->temperature = opchg_get_prop_batt_temp(chip);
    chip->bat_instant_vol = opchg_get_prop_battery_voltage_now(chip);
    chip->charging_current = opchg_get_prop_current_now(chip);//1000;
    chip->charger_vol = opchg_get_prop_charger_voltage_now(chip);
    chip->bat_volt_check_point = opchg_get_prop_batt_capacity(chip);

	chip->charger_type = qpnp_charger_type_get(chip);
	chip->battery_low_vol = opchg_get_prop_low_battery_voltage(chip);

	if(is_project(OPPO_14043) || is_project(OPPO_14037) || is_project(OPPO_14051)){
		pr_debug("chg_vol:%d,chg_sta:%d,current:%d,batt_vol:%d,temp:%d,soc:%d,soc_cal:%d,ocv:%d,auth:%d\n",chip->charger_vol,chip->bat_charging_state,chip->charging_current,
					chip->bat_instant_vol/1000,chip->temperature,chip->soc_bms,chip->bat_volt_check_point,chip->ocv_uv/1000,chip->batt_authen);
	} else {
	    count_debug++;
	    if(count_debug >12)
	    {
	        count_debug =0;
	        pr_debug("oppo_charging_status  battery_temp=%d,battery_instant_vol=%d,\
	battery_soc=%d, charging_current=%d,charger_vol=%d, usb_online= %d, fastcharger_sign= %d,\
	chip->batt_pre_full=%d,charger_status=%d, charger_type=%d,battery_low_vol=%d,charging_time_out=%d,chip->charging_total_time=%d\r\n",
				chip->temperature,chip->bat_instant_vol,chip->bat_volt_check_point,
	            chip->charging_current,chip->charger_vol,chip->chg_present,chip->fastcharger,chip->batt_pre_full,
	            chip->bat_charging_state,chip->charger_type,chip->battery_low_vol,chip->charging_time_out,chip->charging_total_time);
	    }
	}
}

void opchg_set_status(struct opchg_charger *chip)
{
    opchg_set_wdt_reset(chip);
	
    #ifdef OPPO_USE_2CHARGER
    opchg_set_wdt_reset(opchg_slave_chip);
    #endif
    #ifdef OPPO_USE_FAST_CHARGER
    /* set input charging current limit */
    if(is_project(OPPO_14005) || is_project(OPPO_14023) || is_project(OPPO_14037)|| is_project(OPPO_14045) || is_project(OPPO_14051))
	{
	    if (chip->g_is_reset_changed) {
	        opchg_set_reset_charger(chip, true);
	        //msleep(10);
	        opchg_hw_init(chip);
	        opchg_set_input_chg_current(chip, chip->max_input_current[INPUT_CURRENT_MIN],false);
	        chip->g_is_reset_changed = false;
        }
    }
    #endif
        
    if (chip->g_is_changed) {
		/* set charger input current*/
		opchg_set_input_chg_current(chip, chip->max_input_current[INPUT_CURRENT_MIN],false);
		
        /* set charging overtime */
        opchg_set_complete_charge_timeout(chip, chip->overtime_status);
        
        /* set the fast charge current limit */
        opchg_set_fast_chg_current(chip, chip->max_fast_current[FAST_CURRENT_MIN]);
        
        /* set the float voltage */
        opchg_set_float_voltage(chip, chip->min_term_voltage[TERM_VOL_MIN]);
        
        /* set charging disable/enbale */
		opchg_set_charging_disable(chip, !!chip->disabled_status);
        
        /* set suspend enbale/disable */
        opchg_set_suspend_enable(chip, !!chip->suspend_status);
        
        #ifdef OPPO_USE_2CHARGER
        /* set slave charger */
        opchg_slave_set_charging_phase(opchg_slave_chip, chip->fastcharger_status);
        #endif
        
        chip->g_is_changed = false;
    }
}

void opchg_update_thread(struct work_struct *work)
{
    struct delayed_work *dwork = to_delayed_work(work);
    struct opchg_charger *chip = container_of(dwork,
                            struct opchg_charger, update_opchg_thread_work);
	int rc;

    #ifdef OPPO_USE_TIMEOVER_BY_AP
	if(is_project(OPPO_14005) || is_project(OPPO_14023)  || is_project(OPPO_14045)|| is_project(OPPO_14037) || is_project(OPPO_14051))
	{
    	opchg_check_charging_time(chip);
	}
    #endif

//Fuchun.Liao@Mobile.BSP.CHG.BSP enable bq24196 wdt after resume
	if(chip->wdt_enable == false){
		opchg_set_wdt_timer(chip,true);
		chip->wdt_enable = true;
	}

#ifdef OPPO_USE_FAST_CHARGER
    #if 0
	if((chip->boot_mode != MSM_BOOT_MODE__FACTORY) && (chip->boot_mode != MSM_BOOT_MODE__RF) && (chip->boot_mode != MSM_BOOT_MODE__WLAN))
	{
		if(is_project(OPPO_14005)||is_project(OPPO_14023))
		{
			int charge_type = qpnp_charger_type_get(chip);

			// The  switch toggle moved to the main thread
			#if 0
			if(bq27541_di->opchg_fast_charger_enable==1)
			{
				opchg_set_switch_mode(NORMAL_CHARGER_MODE);
			}
			#endif
			
			if((charge_type == POWER_SUPPLY_TYPE_USB_DCP) && (chip->chg_present == true))
			{
				if(opchg_get_prop_fast_chg_started(chip) == true) {
					#ifdef OPCHARGER_DEBUG_ENABLE 
					pr_err("vooc_debug-->fast_chg_started =%d\n",true);
					#endif
					switch_fast_chg(chip);		
					opchg_config_charging_disable(chip, CHAGER_VOOC_DISABLE, 1);
					opchg_config_reset_charger(chip, CHARGER_RESET_BY_VOOC, 1);
					if (chip->g_is_changed) {
					    opchg_set_charging_disable(chip, !!chip->disabled_status);
					}
					opchg_set_wdt_reset(chip);
						
					/*update time 6s*/
					schedule_delayed_work(&chip->update_opchg_thread_work,
							      round_jiffies_relative(msecs_to_jiffies
										     (OPCHG_THREAD_INTERVAL)));
					return;
				}
				else
				{
					#ifdef OPCHARGER_DEBUG_ENABLE 
					pr_err("vooc_debug-->fast_chg_started =%d\n",false);
					#endif
					switch_fast_chg(chip);
					opchg_config_reset_charger(chip, CHARGER_RESET_BY_VOOC, 0);
					opchg_config_charging_disable(chip, CHAGER_VOOC_DISABLE, 0);
				}
			}
		}
	}
	#else
	if((chip->boot_mode != MSM_BOOT_MODE__FACTORY) && (chip->boot_mode != MSM_BOOT_MODE__RF) && (chip->boot_mode != MSM_BOOT_MODE__WLAN))
	{
		if(is_project(OPPO_14005)||is_project(OPPO_14023))
		{
		    int charge_type = qpnp_charger_type_get(chip);
		    int ret = 0;
		    
		    if((charge_type == POWER_SUPPLY_TYPE_USB_DCP) && (chip->chg_present == true)) {
            	switch(chip->vooc_start_step) {
            	    case 0://delay 5s
            	        #ifdef OPCHARGER_DEBUG_ENABLE 
                    	pr_err("vooc_debug-->vooc_start_step =%d\n",0);
                    	#endif
						
						#ifdef OPPO_USE_FAST_CHARGER
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
						#endif
            	        chip->vooc_start_step = 1;
            	        //break;
            	        
            	    case 1:
            	        #ifdef OPCHARGER_DEBUG_ENABLE 
                    	pr_err("vooc_debug-->vooc_start_step =%d\n",1);
                    	#endif
            	        if (is_alow_fast_chg(chip) == true) {
                			// add reset mcu 
                			ret =opchg_set_reset_active(bq27541_di);
                			// set switch
                			ret =opchg_set_switch_mode(VOOC_CHARGER_MODE);
                			
                			chip->vooc_start_step = 2;
                		}
                		
            	        //opchg_config_reset_charger(chip, CHARGER_RESET_BY_VOOC, 0);
            	        if(chip->is_charger_det == 0) {
            	            opchg_set_input_chg_current(chip, chip->max_input_current[INPUT_CURRENT_MIN],false);
            	        }
					    opchg_config_charging_disable(chip, CHAGER_VOOC_DISABLE, 0);
            	        break;
            	    
            	    case 2:
            	        #ifdef OPCHARGER_DEBUG_ENABLE 
                    	pr_err("vooc_debug-->vooc_start_step =%d\n",2);
                    	#endif
            	        if (opchg_get_prop_fast_chg_started(chip) == true) {
            	            chip->vooc_start_step = 3;
            	        }
            	        else {
            	            //opchg_config_reset_charger(chip, CHARGER_RESET_BY_VOOC, 0);
            	            if(chip->is_charger_det == 0) {
            	                opchg_set_input_chg_current(chip, chip->max_input_current[INPUT_CURRENT_MIN],false);
            	            }
					        opchg_config_charging_disable(chip, CHAGER_VOOC_DISABLE, 0);
            	            break;
            	        }
            	    
            	    case 3:
            	        #ifdef OPCHARGER_DEBUG_ENABLE 
                    	pr_err("vooc_debug-->vooc_start_step =%d\n",3);
                    	#endif
            	        opchg_config_charging_disable(chip, CHAGER_VOOC_DISABLE, 1);
				        //opchg_config_reset_charger(chip, CHARGER_RESET_BY_VOOC, 1);
				        if (chip->g_is_changed) {
    					    opchg_set_charging_disable(chip, !!chip->disabled_status);
    					}
    					opchg_set_wdt_reset(chip);
    						
    					/*update time 5s*/
    					schedule_delayed_work(&chip->update_opchg_thread_work,
    							      round_jiffies_relative(msecs_to_jiffies
    										     (OPCHG_THREAD_INTERVAL)));
            	        return;
            	    
            	    default:
            	        #ifdef OPCHARGER_DEBUG_ENABLE 
                    	pr_err("vooc_debug-->vooc_start_step =%d\n",99);
                    	#endif
            	        chip->vooc_start_step = 1;
            	        break;
            	}
            }
        }
    }
	#endif
#endif

    opchg_check_status(chip);
    opchg_check_charging_full(chip);
    
    opchg_set_status(chip);    
    power_supply_changed(&chip->batt_psy);
    
    /*update time 5s*/
    schedule_delayed_work(&chip->update_opchg_thread_work,
                            round_jiffies_relative(msecs_to_jiffies
                            (OPCHG_THREAD_INTERVAL)));
}

void opchg_delayed_wakeup_thread(struct work_struct *work)
{
    struct delayed_work *dwork = to_delayed_work(work);
    struct opchg_charger *chip = container_of(dwork, struct opchg_charger, opchg_delayed_wakeup_work);
    
    if((chip->g_is_wakeup == 1)&&(chip->g_chg_in == 0)){
        __pm_relax(&chip->source);
        chip->g_is_wakeup = 0;
    }
}

void opchg_works_init(struct opchg_charger *chip)
{
    INIT_DELAYED_WORK(&chip->update_opchg_thread_work, opchg_update_thread);        
    schedule_delayed_work(&chip->update_opchg_thread_work,
                            round_jiffies_relative(msecs_to_jiffies(OPCHG_THREAD_INTERVAL_INIT)));
    
    INIT_DELAYED_WORK(&chip->opchg_delayed_wakeup_work, opchg_delayed_wakeup_thread);
    chip->g_is_wakeup = 0;
}

