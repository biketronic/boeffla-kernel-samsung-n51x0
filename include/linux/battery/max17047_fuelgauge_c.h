/*
 * max17047_fuelgauge.h
 *
 * Copyright (C) 2011 Samsung Electronics
 * SangYoung Son <hello.son@samsung.com>
 *
 * based on max17042_battery.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MAX17047_BATTERY_H_
#define __MAX17047_BATTERY_H_

/*#if defined (CONFIG_FUELGAUGE_MAX17047_COULOMB_COUNTING)*/
/* jig state */
extern bool is_jig_attached;

#define CURRENT_RANGE_MAX_NUM	5
#define TEMP_RANGE_MAX_NUM	3

enum {
	SDI = 0,
};

enum {
	RANGE = 0,
	SLOPE,
	OFFSET,
	TABLE_MAX
};


struct battery_data_t {
	u16 Capacity;
	u16 low_battery_comp_voltage;
	s32 low_battery_table[CURRENT_RANGE_MAX_NUM][TABLE_MAX];
	s32 temp_adjust_table[TEMP_RANGE_MAX_NUM][TABLE_MAX];
	u8	*type_str;
};

enum {
	FG_LEVEL = 0,
	FG_TEMPERATURE,
	FG_VOLTAGE,
	FG_CURRENT,
	FG_CURRENT_AVG,
	FG_CHECK_STATUS,
	FG_RAW_SOC,
	FG_VF_SOC,
	FG_AV_SOC,
	FG_FULLCAP,
	FG_MIXCAP,
	FG_AVCAP,
	FG_REPCAP,
};

enum {
	POSITIVE = 0,
	NEGATIVE,
};

#define LOW_BATT_COMP_RANGE_NUM 5
#define LOW_BATT_COMP_LEVEL_NUM 2
#define MAX_LOW_BATT_CHECK_CNT	10

/* FullCap learning setting */
#define VFFULLCAP_CHECK_INTERVAL	300 /* sec */
/* soc should be 0.1% unit */
#define VFSOC_FOR_FULLCAP_LEARNING	95
#define LOW_CURRENT_FOR_FULLCAP_LEARNING	20
#define HIGH_CURRENT_FOR_FULLCAP_LEARNING	120
#define LOW_AVGCURRENT_FOR_FULLCAP_LEARNING 20
#define HIGH_AVGCURRENT_FOR_FULLCAP_LEARNING	100

/* power off margin */
/* soc should be 0.1% unit (BIKETRONIC_BATT NOTE: it's not, its 1% units) */
/* BIKETRONIC_BATT - LOW_MARGIN forces SOC to 0.9% sometimes */
/* HIGH margin holds SOC at POWER_OFF_SOC_HIGH_MARGIN% */
/* LOW_BATT_COMP forces SOC to 0.9% sometimes */
/*original values 3.4-3.5V reduced to 3.1-3.2, 3.3V hold minimum 6% SOC */
/* 3300 HIGH goes to low power with 30 minutes left of reasonable charge */
/* 3100 LOW goes to low power with no margin at 3.0V BAD! */
/* 3100 is about 15 minutes extra power vs 3200 at minimum brightness */
/* at 3300 6%, VOC 3400 to 3100 SOC was 2% at power off */
/* so below 6% is not accurate but at least functions won't turn off */
/* unless the battery really is almost flat */
/* No power between 3.0-3.2V */
/* below values are minimal 6,3300,3200 was 2,3500,3400 */
/* NOT USED - SEE prevent_bad_SOC in .c file */
/* LOW MARGIN not disabled*/
#define POWER_OFF_SOC_HIGH_MARGIN	6 // was 2, disabled
#define POWER_OFF_VOLTAGE_HIGH_MARGIN	2800 // was 3500, disabled.
#define POWER_OFF_VOLTAGE_LOW_MARGIN	2800

//BIKETRONIC_BATT additional DEF
// Affects all battery data tables which in a convoluted way
// force SOC to 0.9% on _corrected_ voltages below 3500 low_batt_comp
// recommend reduction to +0.1V from LOW MARGIN
#define LOW_BATT_COMP_REDUCTION		(3400 - POWER_OFF_VOLTAGE_LOW_MARGIN)

/* FG recovery handler */
/* soc should be 1% unit */
// BIKETRONIC_BATT NOTE: NOT USED
#define STABLE_LOW_BATTERY_DIFF 3
#define STABLE_LOW_BATTERY_DIFF_LOWBATT 1
#define LOW_BATTERY_SOC_REDUCE_UNIT 1

/*#endif*/

enum {
		SDI_BATTERY_TYPE = 0,
		BYD_BATTERY_TYPE,
		LIS_BATTERY_TYPE,
		UNKNOWN_TYPE
};

#define ADC_SDI_BATTERY_MIN 0 /* adc value : 70 */
#define ADC_SDI_BATTERY_MAX 19 /* adc value : 70 */

#define ADC_BYD_BATTERY_MIN 51 /* adc value : 32 */
#define ADC_BYD_BATTERY_MAX 90 /* adc value : 32 */

#define ADC_LIS_BATTERY_MIN 20 /* adc value : 42 */
#define ADC_LIS_BATTERY_MAX 50 /* adc value : 42 */


struct max17047_platform_data {
	int irq_gpio;

	bool enable_current_sense;
	bool enable_gauging_temperature;

	const char *psy_name;
};

#endif

