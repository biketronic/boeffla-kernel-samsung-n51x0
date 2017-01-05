/*
 * Author: andip71, 01.12.2013
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

extern int ac_level;
extern int usb_level;
extern int wireless_level;

extern int ignore_unstable_power;
extern int ignore_safety_margin;

/*BIKETRONIC_BATT */
/*Set high charge levels, 2100mA probable limit, 1600/1800 previous */
/* Minimums not used anywhere... */
/* previous MIN DEF MAX */
/* Set high defaults & "safety" off to prevent loss of charge while plugged*/
/* in, and resultant boot loops on low volt, high resistance battery*/
/* 100 1000 1800 */
#define AC_CHARGE_LEVEL_DEFAULT 1200
#define AC_CHARGE_LEVEL_MIN 100
#define AC_CHARGE_LEVEL_MAX 2100

/* 475 0 1600 */
#define USB_CHARGE_LEVEL_DEFAULT 1200
#define USB_CHARGE_LEVEL_MIN 0
#define USB_CHARGE_LEVEL_MAX 2100

#define WIRELESS_CHARGE_LEVEL_DEFAULT 475
#define WIRELESS_CHARGE_LEVEL_MIN 100
#define WIRELESS_CHARGE_LEVEL_MAX 1000

#define IGNORE_UNSTABLE_POWER_DEFAULT 1
#define IGNORE_SAFETY_MARGIN_DEFAULT 1

extern char charge_info_text[30];
extern int charge_info_level;
