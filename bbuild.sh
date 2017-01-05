#!/bin/bash

# Boeffla Kernel Universal Build Script
#
# Version 1.2, 26.08.2015
#
# (C) Lord Boeffla (aka andip71)

#######################################
# Parameters to be configured manually
#######################################

BOEFFLA_VERSION="1.3bat17c-Samsung-n5120"
#bat1 modified fuel guage basic battery figures & max charge voltage
# POWER OFF LOW MARGIN 3.1V-3.2V, recharge drop value 0.1V, 4.2V charge
#bat2 - added LOW BATT COMP voltage modifiers
# also changed capacity change limits from +-10% to +20% -5%
# because it is harder to get a full charge.
# and hold SOC at minimum 6% >3.3V from 1.3% at >3.5V
#bat3 - ignore safey margin on USB port, 1.2A charge
# to prevent boot loops on bad USB cables.
#bat4 - safety margin makes no difference on battery charge only mode
# Increase LOW MARGIN (and therefore comp margin) to 3.2V as 3.1V was kicking in at 3.0V! No useful power around 3.0-3.2V.
# Overall 200mV reduction.
#bat5 - increase to 4.225V charge (4.18V reported at 4.2V) 
#NB 4.225 -> 4.205V full, perfect.
# Add BATT_CURRENT_NOW and BATT_CURRENT_AVG 
# hopefully appears in 
# /sys/devices/platform/samsung-battery/power_supply/battery
# updated /arc/arm/mach-exynos/mach-kona.c battery (sanity check?) values
#bat6 - add undervolt to 100-400mhz by extending function to 19 values
#bat7 - add batt_capacity_full,avg,now, NB 4760mA default capacity
# BUG - reports capacity of -360 - -318, clearly incorrect.
#bat7G - set CONFIG_MALI_UMP_R3P1=y to n in boeffla_defconfig to allow undervolting etc.
# rescinded because it makes no difference!!!!
#bat8 - read val for capacity. looks suspiciously like current_now.... fixed. now returns 100(%?) :-(
#bat 9 - fixed capacity bug
# work on supporting SOC for voltages under 3500mV prevent_bad_SOC in max17047...c
# code compiles, added. 
#Uses resistive compensated look up table for SOC under 3500mV that isn't = 0%
# fixed negative -> positive resistance bug
#bat10 - tweak prevent_bad_SOC
# remove all other low voltage mechanisms by setting to 2800mV
# ignored low_boot stuff as it seems unusued.
# despite this, it drops to zero at 3300mV....
# bat 10a - no zero values in prevent_bad_SOC ex 2600mV....
# effect is it goes to 1% even at 3160mV... So
# problem is slow fluctuating current values and slow voltage response times?
# i.e. current is reported as zero?
#bat10b add average voltage
#bat10c - average volage only AND fix return fg_soc;
#warning I saw 7% at <2800mV!!!!! Then it rebooted.
#first ex_sdcard failed at 2800mV
#seems very good that nothing forces the power off. But.
#now I get silly low %, should be 0 not 7
#bat11 - MAX_SOC in prevent_bad_SOC
# this should prevent 2800mV
#bat11b - tweak - 3250 = 1% (3250-3160 was <5 minutes at 500mA)
# 3300 = 6% hold the power on.
# 3400 = 8-10% (MIN - LIMIT)
# 3500 = 10-20%
# 3600 = 20-30%
#NB when charging 5% reached is 3700mV @.2ohm - unplug = 3500 est 3450 @ 0.2 ohm is actually 5% drops to 3300 within 10 minutes 3200 in a couple of minutes
#bat11c
#3300 = 2% // LOW
#3400 = 3% sits on 3400 for 10 minutes.
#3500 = 6-10% 
#3700 = 20-30%
#bat12 - remove all pointless values
#3600 = 6-100% fixes bug where SOC max is always 30%!
#3500 = 6-10% ... and as before 3400=3 - 3250=1%, 3160=0%
#issue #12345 - charging with USB causes jump in % despite <0 current check
# as USB charging = discharging....
# fix by limiting 'minimum' to 6% - error is limited to this much extra capacity when dead.
# and added cable_type != POWER_SUPPLY_TYPE_USB
#BUG - for some reason dropped to 5% then 3% and 100mV too high???
# e.g. 3400-3500 is <6%
#bat12b
# try increasing 3600 to 10% min and 3400 max to 6%
# 3350 - 6,6%
# 3500 - 8,10%
# 3600 - 10,100%
# at ~3350 with full load (3050mV) it goes to 0%
# because resistance is not smoothed <3160mV = 1 mohm...
#bat12c - fix resistance under 3160mV, jumps to 8% on charge still
# check inst. current is <0
#bad12d fix bug wrong table length (1 short) may have no effect
# also note: batt_reset_soc - e.g. reported 288mAh but 5% showing
# and batt_read_soc shows 9%
# ran batt_reset_soc and it jumped from 5 to 9%?! probably just a fluke.
# add BATT_CURRENT and BATT_CAPACITY (battery-factory.c)
# BATT_CURRENT is current_now (might be too fast moving but hey)
# wrong SOC not fixed, problem unknown. Reboot restores it.
#jumps to 1% at about 3350 actual. - Max SOC not smoothed BUG fixed
# set 6% drop off at 3300mV from 3350.
# WORKS - drops from 5% to 1%
#bat13
#change to BATT_CURRENT = AVERAGE as most apps are dead slow.
#gov1
# mod zzmove or pegasususq (which has great response time)
# so that it doesn't overclock unless absolutely necessary
# VERY HARD
# NB 3550-3600mV ~ 10%
# Hit 10% and sat on it for ages, then 5% and off within 1 minute
# having no upper limit caused this? 
# voltage was very low, back and menu lights flickering. 
# GAMING test: medium brightness, pegasusqplus 1920Mhz
# 3:40 14%, 3.5-3.6V
# 4:47 7%, 3.4-3.5V
# 5:10 6-5-0%, 3.3V FLAT
# So, at ~3550mV, 3.666 (220), 5.1666 (310) = 90 / 310 = 29%

#bat14
# Reduce 3300mV 6% to 3350mV (prevent 5% to off within 1 minute)
# increase 3600 to 30% from 10%. (prevent sitting on 10% for 1 hour, should be >30% as per gaming test)
# work on pegasusqplus - OC time limit.
# work on board-kona-battery - charging limits? not used, ignored.
#bat14b - beta - GOV pegasususq plus not complete, added 5 GPU in Bof control
#/ramdisk_boeffla/fs/res/bc/ - fixed- removed intelliplug and added 2100mA usb
# NB 5 GPU frequencies in the 4.4KK version
# DOESN"T WORK - 
#bat 15
# copied across /drivers/gpu/mali stuff r3p2
# seems to just add it anyway and adds gpu_control - nothing else required?
# mod code to prevent jump to 30% - at least 300mA discharge or fuel guage not altered.
#bat15b
# CPU overclock 2ghz unstable. Try reserved bits on clock div, try 15 on 2ghz
#bat15c
# reverted mask change, a divisor of 15 doesn't improve 2ghz stability.
# try to fix mali frequency control... MALI_U3P1 is not set should be set?
# this should enable only /drivers/gpu/mali/u3p2
# edited makefile in drivers/misc to remove gpu volt and clock control which is limited to 4 steps and doesn't work (doestn' exist in 442)
# so only drivers/gpu/mali is enabled. Drivers/media/video/samsung/mali disabled.
# but need to enable R3P1 in boeffla_defconfig or it won't compile
# add to kernel/power/main.c which disables EXYNOS4_GPU_LOCK
# OK can't get r3p2 to work
# try fixing r3p1 - fixed 5 step gpu voltage and 5 step clock but clock is overwritten instantly
# all in r3p1/platform/pegasusm400/mali_platform_dvfs.c
#bat15d
# add boost logic to mali_platform_dvfs so that it jumps up to step 4 instantly to reduce lag
# it then has to drop back via each step
#bat15e
# mod pegasusqplus to boost - basic test beta - and fast down on OC
#bat16
# add counting logic to pegasususqplus
# this seems suspect - seems to work sometimes? mostly limits to 1500
# also jumps a lot and doesn't sit on 1100. Likes 100, 900, 1500 and 1920
# very snappy though.
#bat16b
# fix laggy gpu by reducing thresholds - once it boosts it won't keep dropping and jumping up.
# reduce from 90 to 65 up and half down threshold except fastest frequency and idle
# fix pegasususqplus - reduce OC thresholds from 95 to 90 and frequency step to 5% up ex boost
# remove OC time bug, where it never uses time below OC_COOL_TIME_MIN
# seems to always sit on 1920 now?! never goes down, 
# gpu much smoother
#bat16c
# fix pegasusqplus - change thresholds, check for <= ratio not zero.
# and sit on 1100 on the way back down.
# bootup is 1500 (expected)
#bat16d
# sanity check oc_cool_time
# starts at 1500...
# 1920 still using 20% CPU!!!!!!!!!!
# seems to set oc cool time to 0 initially but then it somehow goes bonkers
#bat16e
# rewrite down threshold - no real change?
# up threshold fix bug, where it multiplied inc by 5 instead of 5/100
# this would always jump to max cpu....
# moved static int to inside dbs_check_cpu
# CRASH at 1920mhz, 1800mhz but not 1704? - could be accidental -25mv no, crashes anyway.
# does limit to 1500Mhz properly.
#bat16f
# fix divide by zero / (policy->max - policy->cur)!!!! in pegasusqplus
# new bug, can't exceed 1500mhz
# by some fluke it went there for 1 second in 13 minutes.
#bat16g
# divide by zero was fixed wrong! change <= to >=
# this should fix can't exceed 1500mhz as it was always resetting oc_cool_time if it OC'd
# but it crashes? or not. Crashed on suspend suspect graphics. Try avoiding 54.
# Crashes on 1920Mhz.
# Almost never OCs - 
#bat16h
# decrease ratio from 30 as it was only using 0.8%- 2% OC 
# disable jump to high or OC frequency
# does OC for about 2 seconds.
#OC is 10% now. for a ratio of 10.
# CRASH
# Crash on power on
#bat16i
# move oc_cool_time to hotplug_historyplus in case it is being lost somewhere...
# CRASH on change in cpu speed up to OC from low speed suspected
# try limit to 1704 - seems ok
# try 1800 - seems ok
# try 1920 at 1375 from 1350 stable CRASH - garbled graphics - problem is GPU?
# 1920 CRASH instantly with standard GPU settings
# I think the divide by zero must be a problem somehow still???????
#bat16j
# remove divide by zero issue by simply subtracting ratio above OC level.
# 1920? CRASH. 1800 works perfectly. 1920 crashes.
# Both use OC cool time etc... ????
#bat16k
# remove 'bug fix' IS UNCHANGED. Shift drop to ideal freq point so it will drop down
# mods to down to ensure drop.
# try OC -> IDEAL HIGH -> IDEAL -> lower so it doesn't jump to 1100.
# seems stable.... but it's not.
#bat16l
# consider changes before crash - mods to up threshold increment and oc cool time.
# try removing oc_cool_time code....
# now it is just up down threshold mod.
# crashes instantly, no delay this time.
#bat16m
# remove boost freq code
# remove almost all code
# decrement is 10% of max
# a very boring governor. Won't exceed 1704.
# only thresholds remain but won't exceed 1704
# try rebooting
# If i use another governor then switch to it, it crashes at 1920.
#bat16n
# found bug - max_load = load was not attached to the if statement properly
# this could prevent exceeding 1704 sometimes?
# use normal freq step khz instead
# remove if freq_next > 0.
#bat16o
# no idea what else to do
# still limited to 1704
# try increasing frequency step to 250000khz
#changed method of calculating max_load to divide max_load_freq
# Now goes to 1800.... So the frequency step does it???
#bat16p
# reduce OC threshold to 80/85 from 85/90
# revert frequency step to 120000khz
# can't do above 1704.... wtf
# crash
#bat16q
# revive freq_step
# DOESN'T CRASH. Setting inc = ..... made all the difference
# Does go to 1920mhz
# crashed taking a photo
# try clearing cache
# crashes taking a photo again frequency bounces up and down
# seems stable except crashes when using the camera...
# something about the down threshold logic crashes the cpu.....
# sometimes survives camera from 100mhz

#bat16r
# revive cool operation (up threshold)
# revive down threshold ideal and ideal high drop down on cool
# add min, max checks before changing CPU speed.
# revert threshold OC to 85-90
# revert threshold 1100 to 10 down (100% at 100mhz)
# boost frequency left disabled.
# crash on camera still. WTF is causing this? pegasusq at 1920 doesn't crash.
# maybe it can't drop in frequency under full load? But then the minimal governor doesn't change this...? and it only went down at 100mhz at a time.

#bat17a
# give up on pegasususq
# modify pegasusq, remove hotplug code...
# add oc_time
# gov does not appear
#bat17b 
# try changing the governor name to be unique - change from pegasusq to cpufreq_gov_pegasusq_biketronic
# and register this unique name.
# did not work.
#bat17c
# try editing kconfig

EXTENDED_CMDLINE=""

#sabremod compiled
TOOLCHAIN="/media/eric/SSDFiles/n5120_kernel_source/arm-linux-gnueabi-4.8/bin/arm-eabi-"
#/opt/toolchains/arm-eabi-4.8/bin/arm-eabi-"

COMPILE_DTB="n"
MODULES_IN_SYSTEM="n"
KERNEL_SAMSUNG="y"
OUTPUT_FOLDER=""

DEFCONFIG="boeffla_defconfig"
DEFCONFIG_VARIANT=""

MKBOOTIMG_CMDLINE=""
MKBOOTIMG_BASE="0x10000000"
MKBOOTIMG_PAGESIZE="2048"
MKBOOTIMG_RAMDISK_OFFSET="0x01000000"
MKBOOTIMG_TAGS_OFFSET="0x00000100"

BOOT_PARTITION="dev/block/mmcblk0p9"
SYSTEM_PARTITION="/dev/block/mmcblk0p20"

ASSERT_1="konalte"
ASSERT_2="n5120"
ASSERT_3="GT-N5120"
ASSERT_4="konaltexx"
ASSERT_5=""
ASSERT_6=""
ASSERT_7=""
ASSERT_8=""
ASSERT_9=""
ASSERT_10=""
ASSERT_11=""
ASSERT_12=""

FINISH_MAIL_TO=""

SMB_SHARE_KERNEL=""
SMB_FOLDER_KERNEL=""
SMB_AUTH_KERNEL=""

SMB_SHARE_BACKUP=""
SMB_FOLDER_BACKUP=""
SMB_AUTH_BACKUP=""

# BIKETRONIC NOTE
# This was set to 5 CPUS????
# -j5 allow 5 jobs at once
# 348 sec 4 CPU
# 344 sec 5 CPU

NUM_CPUS="5"	# number of cpu cores used for build


#######################################
# automatic parameters, do not touch !
#######################################

COLOR_RED="\033[0;31m"
COLOR_GREEN="\033[1;32m"
COLOR_NEUTRAL="\033[0m"

SOURCE_PATH=$PWD
cd ..
ROOT_PATH=$PWD
ROOT_DIR_NAME=`basename "$PWD"`
cd $SOURCE_PATH

BUILD_PATH="$ROOT_PATH/build"
REPACK_PATH="$ROOT_PATH/repack"

TOOLCHAIN_COMPILE=`grep "^CROSS_COMPILE" $SOURCE_PATH/Makefile`
TOOLCHAIN_COMPILE=/`echo $TOOLCHAIN_COMPILE | sed -n -e 's/^.* \///p'`

BOEFFLA_DATE=$(date +%Y%m%d)
GIT_BRANCH=`git symbolic-ref --short HEAD`


# overwrite settings with custom file, if it exists
if [ -f $ROOT_PATH/x-settings.sh ]; then
  . $ROOT_PATH/x-settings.sh
fi

BOEFFLA_FILENAME="boeffla-kernel-$BOEFFLA_VERSION"

if [ "y" == "$MODULES_IN_SYSTEM" ]; then
	MODULE_PATH="system/lib/modules"
else
	MODULE_PATH="ramdisk/lib/modules"
fi


#####################
# internal functions
#####################

step0_copy_code()
{
	echo -e $COLOR_GREEN"\n0 - copy code\n"$COLOR_NEUTRAL

	# remove old build folder and create empty one
	rm -r -f $BUILD_PATH
	mkdir $BUILD_PATH

	# copy code from source folder to build folder
	# (usage of * prevents .git folder to be copied)
	cp -r $SOURCE_PATH/* $BUILD_PATH

	# Replace version information in mkcompile_h with the one from x-settings.sh
	sed "s/\`echo \$LINUX_COMPILE_BY | \$UTS_TRUNCATE\`/Boeffla-Kernel-$BOEFFLA_VERSION-$BOEFFLA_DATE/g" -i $BUILD_PATH/scripts/mkcompile_h
	sed "s/\`echo \$LINUX_COMPILE_HOST | \$UTS_TRUNCATE\`/andip71/g" -i $BUILD_PATH/scripts/mkcompile_h
}

step1_make_clean()
{
	echo -e $COLOR_GREEN"\n1 - make clean\n"$COLOR_NEUTRAL
	
	# jump to build path and make clean
	cd $BUILD_PATH
	make clean
}

step2_make_config()
{
	echo -e $COLOR_GREEN"\n2 - make config\n"$COLOR_NEUTRAL
	echo
	
	# build make string depending on if we need to compile to an output folder
	# and if we need to have a defconfig variant
	MAKESTRING="arch=arm $DEFCONFIG"
	
	if [ ! -z "$OUTPUT_FOLDER" ]; then
		rm -rf $BUILD_PATH/output
		mkdir $BUILD_PATH/output
		MAKESTRING="O=$OUTPUT_FOLDER $MAKESTRING"
	fi

	if [ ! -z "$DEFCONFIG_VARIANT" ]; then
		MAKESTRING="$MAKESTRING VARIANT_DEFCONFIG=$DEFCONFIG_VARIANT"
	fi
	
	# jump to build path and make config
	cd $BUILD_PATH
	echo "Makestring: $MAKESTRING"
	make $MAKESTRING
}

step3_compile()
{
	echo -e $COLOR_GREEN"\n3 - compile\n"$COLOR_NEUTRAL

	TIMESTAMP1=$(date +%s)
	
	# jump to build path
	cd $BUILD_PATH

	# compile source
	if [ -z "$OUTPUT_FOLDER" ]; then
		make -j$NUM_CPUS 2>&1 |tee ../compile.log
	else
		make -j$NUM_CPUS O=$OUTPUT_FOLDER 2>&1 |tee ../compile.log
	fi

	# compile dtb if required
	if [ "y" == "$COMPILE_DTB" ]; then
		echo -e ">>> compiling DTB\n"
		echo
		
		# Compile dtb (device tree blob) file
		chmod 777 tools_boeffla/dtbToolCM
		tools_boeffla/dtbToolCM -2 -o $BUILD_PATH/$OUTPUT_FOLDER/arch/arm/boot/dt.img -s 2048 -p $BUILD_PATH/$OUTPUT_FOLDER/scripts/dtc/ $BUILD_PATH/$OUTPUT_FOLDER/arch/arm/boot/
	fi

	TIMESTAMP2=$(date +%s)
	
	# Log compile time (screen output)
	echo "compile time:" $(($TIMESTAMP2 - $TIMESTAMP1)) "seconds"
	echo "zImage size (bytes):"
	stat -c%s $BUILD_PATH/$OUTPUT_FOLDER/arch/arm/boot/zImage

	# Log compile time and parameters (log file output)
	echo -e "\n***************************************************" >> ../compile.log
	echo -e "\ncompile time:" $(($TIMESTAMP2 - $TIMESTAMP1)) "seconds" >> ../compile.log
	echo "zImage size (bytes):" >> ../compile.log
	stat -c%s $BUILD_PATH/$OUTPUT_FOLDER/arch/arm/boot/zImage >> ../compile.log

	echo -e "\n***************************************************" >> ../compile.log
	echo -e "\nroot path:" $ROOT_PATH >> ../compile.log
	echo "toolchain compile:" >> ../compile.log
	grep "^CROSS_COMPILE" $BUILD_PATH/Makefile >> ../compile.log
	echo "toolchain stripping:" $TOOLCHAIN >> ../compile.log
	echo "extended cmdline:" $EXTENDED_CMDLINE >> ../compile.log
}

step4_unpack_ramdisk()
{
	echo -e $COLOR_GREEN"\n4 - unpack ramdisk\n"$COLOR_NEUTRAL
	
	# Cleanup folder if still existing
	echo -e ">>> cleanup repack folder\n"
	{
		rm -r -f $REPACK_PATH
		mkdir -p $REPACK_PATH
	} 2>/dev/null

	# Copy and Unpack original ramdisk
	echo -e ">>> unpack original ramdisk\n"

	cd $REPACK_PATH

	cp $BUILD_PATH/ramdisk_original/* .
	mkdir ramdisk

	cd $REPACK_PATH/ramdisk
	gunzip -c ../boot.img-ramdisk.gz | cpio -i
}

step5_patch_ramdisk()
{
	echo -e $COLOR_GREEN"\n5 - patch ramdisk\n"$COLOR_NEUTRAL
	
	# Copy compiled files (zImage, dtb and modules)
	echo -e ">>> copy zImage, dtb and modules\n"
	
	cp $BUILD_PATH/$OUTPUT_FOLDER/arch/arm/boot/zImage $REPACK_PATH/zImage
	{
		# copy dt.img
		cp $BUILD_PATH/$OUTPUT_FOLDER/arch/arm/boot/dt.img $REPACK_PATH/dt.img

		# copy modules and set permissions
		mkdir -p $REPACK_PATH/$MODULE_PATH
		
		cd $BUILD_PATH/$OUTPUT_FOLDER
		find -name '*.ko' -exec cp -av {} $REPACK_PATH/$MODULE_PATH/ \;
		chmod 644 $REPACK_PATH/$MODULE_PATH/*

		# strip modules
		echo -e ">>> strip modules\n"
		${TOOLCHAIN}strip --strip-unneeded $REPACK_PATH/$MODULE_PATH/*
	} 2>/dev/null


	# Apply boeffla kernel specific patches and copy additional files to ramdisk
	echo -e ">>> apply Boeffla kernel patches and copy files\n"

	cd $REPACK_PATH
	for PATCHFILE in $BUILD_PATH/ramdisk_boeffla/patch/*.patch
	do
		patch ramdisk/$(basename $PATCHFILE .patch) < $PATCHFILE
	done
		
	{
		# delete orig files, if patching created some
		rm ramdisk/*.orig

		cp -R $BUILD_PATH/ramdisk_boeffla/fs/* ramdisk
		chmod -R 755 ramdisk/*.rc
		chmod -R 755 ramdisk/sbin
		chmod -R 755 ramdisk/res/bc
		chmod -R 755 ramdisk/res/misc
	} 2>/dev/null
}

step6_repack_ramdisk()
{
	echo -e $COLOR_GREEN"\n6 - repack ramdisk\n"$COLOR_NEUTRAL
	
	echo -e ">>> repack new ramdisk\n"
	cd $REPACK_PATH/ramdisk
	find . | cpio -o -H newc | gzip > ../newramdisk.cpio.gz

	# Create new bootimage
	echo -e ">>> create boot image\n"
	
	cd $REPACK_PATH
	chmod 777 $BUILD_PATH/tools_boeffla/mkbootimg
	
	if [ "y" == "$COMPILE_DTB" ]; then
		$BUILD_PATH/tools_boeffla/mkbootimg --kernel zImage --ramdisk newramdisk.cpio.gz --cmdline "$MKBOOTIMG_CMDLINE $EXTENDED_CMDLINE" --base $MKBOOTIMG_BASE --pagesize $MKBOOTIMG_PAGESIZE --ramdisk_offset $MKBOOTIMG_RAMDISK_OFFSET --tags_offset $MKBOOTIMG_TAGS_OFFSET --dt dt.img -o boot.img
	else
		$BUILD_PATH/tools_boeffla/mkbootimg --kernel zImage --ramdisk newramdisk.cpio.gz --cmdline "$MKBOOTIMG_CMDLINE $EXTENDED_CMDLINE" --base $MKBOOTIMG_BASE --pagesize $MKBOOTIMG_PAGESIZE --ramdisk_offset $MKBOOTIMG_RAMDISK_OFFSET --tags_offset $MKBOOTIMG_TAGS_OFFSET -o boot.img
	fi
	
	# Creating recovery flashable zip
	echo -e ">>> create flashable zip\n"

	cd $REPACK_PATH
	mkdir -p META-INF/com/google/android
	cp $BUILD_PATH/tools_boeffla/update-binary META-INF/com/google/android

	# compose updater script
	if [ ! -z $ASSERT_1 ]; then
		echo "assert(getprop(\"ro.product.device\") == \"$ASSERT_1\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_1\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_2 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_2\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_2\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_3 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_3\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_3\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_4 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_4\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_4\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_5 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_5\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_5\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_6 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_6\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_6\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_7 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_7\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_7\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_8 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_8\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_8\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_9 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_9\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_9\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_10 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_10\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_10\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_11 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_11\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_11\" ||" >> META-INF/com/google/android/updater-script
	fi
	if [ ! -z $ASSERT_12 ]; then
		echo "getprop(\"ro.product.device\") == \"$ASSERT_12\" ||" >> META-INF/com/google/android/updater-script
		echo "getprop(\"ro.build.product\") == \"$ASSERT_12\" ||" >> META-INF/com/google/android/updater-script
	fi
	
	if [ ! -z $ASSERT_1 ]; then
		echo "abort(\"This package is for device: $ASSERT_1 $ASSERT_2 $ASSERT_3 $ASSERT_4 $ASSERT_5 $ASSERT_6; this device is \" + getprop(\"ro.product.device\") + \".\"););" >> META-INF/com/google/android/updater-script
	fi
	
	echo "ui_print(\"Flashing Boeffla-Kernel $BOEFFLA_VERSION\");" >> META-INF/com/google/android/updater-script
	echo "package_extract_file(\"boot.img\", \"$BOOT_PARTITION\");" >> META-INF/com/google/android/updater-script
	
	if [ ! "y" == "$KERNEL_SAMSUNG" ]; then
		echo "mount(\"ext4\", \"EMMC\", \"$SYSTEM_PARTITION\", \"/system\");" >> META-INF/com/google/android/updater-script
		echo "delete_recursive(\"/$MODULE_PATH\");" >> META-INF/com/google/android/updater-script
		echo "package_extract_dir(\"$MODULE_PATH\", \"/$MODULE_PATH\");" >> META-INF/com/google/android/updater-script
		echo "unmount(\"/system\");" >> META-INF/com/google/android/updater-script
	fi
	
	echo "ui_print(\" \");" >> META-INF/com/google/android/updater-script
	echo "ui_print(\"(c) Lord Boeffla (aka andip71), $(date +%Y.%m.%d-%H:%M:%S)\");" >> META-INF/com/google/android/updater-script
	echo "ui_print(\" \");" >> META-INF/com/google/android/updater-script
	echo "ui_print(\"Finished, please reboot.\");" >> META-INF/com/google/android/updater-script

	# add required files to new zip
	zip $BOEFFLA_FILENAME.recovery.zip boot.img
	zip $BOEFFLA_FILENAME.recovery.zip META-INF/com/google/android/updater-script
	zip $BOEFFLA_FILENAME.recovery.zip META-INF/com/google/android/update-binary

	if [ ! "y" == "$KERNEL_SAMSUNG" ]; then
		zip $BOEFFLA_FILENAME.recovery.zip $MODULE_PATH/*
	fi

	# sign recovery zip if there are keys available
	if [ -f "$BUILD_PATH/tools_boeffla/testkey.x509.pem" ]; then
		echo -e ">>> signing recovery zip\n"
		java -jar $BUILD_PATH/tools_boeffla/signapk.jar -w $BUILD_PATH/tools_boeffla/testkey.x509.pem $BUILD_PATH/tools_boeffla/testkey.pk8 $BOEFFLA_FILENAME.recovery.zip $BOEFFLA_FILENAME.recovery.zip_signed
		cp $BOEFFLA_FILENAME.recovery.zip_signed $BOEFFLA_FILENAME.recovery.zip
		rm $BOEFFLA_FILENAME.recovery.zip_signed
	fi

	md5sum $BOEFFLA_FILENAME.recovery.zip > $BOEFFLA_FILENAME.recovery.zip.md5

	# For Samsung kernels, create tar.md5 for odin
	if [ "y" == "$KERNEL_SAMSUNG" ]; then
		echo -e ">>> create Samsung files for Odin\n"
		cd $REPACK_PATH
		tar -cvf $BOEFFLA_FILENAME.tar boot.img
		md5sum $BOEFFLA_FILENAME.tar >> $BOEFFLA_FILENAME.tar
		mv $BOEFFLA_FILENAME.tar $BOEFFLA_FILENAME.tar.md5
	fi
	
	# Creating additional files for load&flash
	echo -e ">>> create load&flash files\n"

	if [ "y" == "$KERNEL_SAMSUNG" ]; then
		md5sum boot.img > checksum
	else
		cp $BOEFFLA_FILENAME.recovery.zip cm-kernel.zip
		md5sum cm-kernel.zip > checksum
	fi

	# Cleanup
	echo -e ">>> cleanup\n"
	rm -rf META-INF
}

step7_analyse_log()
{
	echo -e $COLOR_GREEN"\n7 - analyse log\n"$COLOR_NEUTRAL

	# Check compile result and patch file success
	echo -e "\n***************************************************"
	echo -e "Check for compile errors:"

	cd $ROOT_PATH
	echo -e $COLOR_RED
	grep " error" compile.log
	grep "forbidden warning" compile.log
	echo -e $COLOR_NEUTRAL

	echo -e "Check for patch file issues:"
	cd $REPACK_PATH/ramdisk
	echo -e $COLOR_RED
	find . -type f -name *.rej
	echo -e $COLOR_NEUTRAL

	echo -e "***************************************************"
}

step8_transfer_kernel()
{
	echo -e $COLOR_GREEN"\n8 - transfer kernel\n"$COLOR_NEUTRAL

	# exit function if no SMB share configured
	if [ -z "$SMB_SHARE_KERNEL" ]; then
		echo -e "No kernel smb share configured, not transfering files.\n"	
		return
	fi

	# copy the required files to a SMB network storage
	smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "mkdir $SMB_FOLDER_KERNEL\\$BOEFFLA_VERSION"
	smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "put $REPACK_PATH/$BOEFFLA_FILENAME.recovery.zip $SMB_FOLDER_KERNEL\\$BOEFFLA_VERSION\\$BOEFFLA_FILENAME.recovery.zip"
	smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "put $REPACK_PATH/$BOEFFLA_FILENAME.recovery.zip.md5 $SMB_FOLDER_KERNEL\\$BOEFFLA_VERSION\\$BOEFFLA_FILENAME.recovery.zip.md5"
	smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "put $REPACK_PATH/checksum $SMB_FOLDER_KERNEL\\$BOEFFLA_VERSION\\checksum"

	if [ "y" == "$KERNEL_SAMSUNG" ]; then
		smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "put $REPACK_PATH/$BOEFFLA_FILENAME.tar.md5 $SMB_FOLDER_KERNEL\\$BOEFFLA_VERSION\\$BOEFFLA_FILENAME.tar.md5"
		smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "put $REPACK_PATH/boot.img $SMB_FOLDER_KERNEL\\$BOEFFLA_VERSION\\boot.img"
	else
		smbclient $SMB_SHARE_KERNEL -U $SMB_AUTH_KERNEL -c "put $REPACK_PATH/cm-kernel.zip $SMB_FOLDER_KERNEL\\$BOEFFLA_VERSION\\cm-kernel.zip"
	fi
}

step9_send_finished_mail()
{
	echo -e $COLOR_GREEN"\n9 - send finish mail\n"$COLOR_NEUTRAL

	# send a mail to inform about finished compilation
	if [ -z "$FINISH_MAIL_TO" ]; then
		echo -e "No mail address configured, not sending mail.\n"	
	else
		cat $ROOT_PATH/compile.log | /usr/bin/mailx -s "Compilation for Boeffla-Kernel $BOEFFLA_VERSION finished!!!" $FINISH_MAIL_TO
	fi
}	

stepR_rewrite_config()
{
	echo -e $COLOR_GREEN"\nr - rewrite config\n"$COLOR_NEUTRAL
	
	# copy defconfig, run make oldconfig and copy it back
	cd $SOURCE_PATH
	cp arch/arm/configs/$DEFCONFIG .config
	make oldconfig
	cp .config arch/arm/configs/$DEFCONFIG
	make mrproper
	
	# commit change
	git add arch/arm/configs/$DEFCONFIG
	git commit
}

stepC_cleanup()
{
	echo -e $COLOR_GREEN"\nc - cleanup\n"$COLOR_NEUTRAL
	
	# remove old build and repack folders, remove any logs
	{
		rm -r -f $BUILD_PATH
		rm -r -f $REPACK_PATH
		rm $ROOT_PATH/*.log
	} 2>/dev/null
}

stepB_backup()
{
	echo -e $COLOR_GREEN"\nb - backup\n"$COLOR_NEUTRAL

	# Create a tar backup in parent folder, gzip it and copy to verlies
	BACKUP_FILE="$ROOT_DIR_NAME""_$(date +"%Y-%m-%d_%H-%M").tar.gz"

	cd $ROOT_PATH
	tar cvfz $BACKUP_FILE source x-settings.sh
	cd $SOURCE_PATH

	# transfer backup only if smbshare configured
	if [ -z "$SMB_SHARE_BACKUP" ]; then
		echo -e "No backup smb share configured, not transfering backup.\n"	
	else
		# copy backup to a SMB network storage and delete backup afterwards
		smbclient $SMB_SHARE_BACKUP -U $SMB_AUTH_BACKUP -c "put $ROOT_PATH/$BACKUP_FILE $SMB_FOLDER_BACKUP\\$BACKUP_FILE"
		rm $ROOT_PATH/$BACKUP_FILE
	fi
}


################
# main function
################

unset CCACHE_DISABLE

case "$1" in
	rel)
		export CCACHE_DISABLE=1
		step0_copy_code
		step1_make_clean
		step2_make_config
		step3_compile
		step4_unpack_ramdisk
		step5_patch_ramdisk
		step6_repack_ramdisk
		step7_analyse_log
		step8_transfer_kernel
		step9_send_finished_mail
		exit
		;;
	a)
		step0_copy_code
		step1_make_clean
		step2_make_config
		step3_compile
		step4_unpack_ramdisk
		step5_patch_ramdisk
		step6_repack_ramdisk
		step7_analyse_log
		step8_transfer_kernel
		step9_send_finished_mail
		exit
		;;
	u)
		step3_compile
		step4_unpack_ramdisk
		step5_patch_ramdisk
		step6_repack_ramdisk
		step7_analyse_log
		step8_transfer_kernel
		step9_send_finished_mail
		exit
		;;
	ur)
		step6_repack_ramdisk
		step7_analyse_log
		step8_transfer_kernel
		step9_send_finished_mail
		exit
		;;
	0)
		step0_copy_code
		exit
		;;
	1)
		step1_make_clean
		exit
		;;
	2)
		step2_make_config
		exit
		;;
	3)
		step3_compile
		exit
		;;
	4)
		step4_unpack_ramdisk
		exit
		;;
	5)
		step5_patch_ramdisk
		exit
		;;
	6)
		step6_repack_ramdisk
		exit
		;;
	7)
		step7_analyse_log
		exit
		;;
	8)
		step8_transfer_kernel
		exit
		;;
	9)
		step9_send_finished_mail
		exit
		;;
	b)
		stepB_backup
		exit
		;;
	c)
		stepC_cleanup
		exit
		;;
	r)
		stepR_rewrite_config
		exit
		;;
esac	

echo
echo
echo "Function menu"
echo "================================================"
echo
echo "rel = all, execute steps 0-9 - without CCACHE"
echo "a   = all, execute steps 0-9"
echo "u   = upd, execute steps 3-9"
echo "ur  = upd, execute steps 6-9"
echo
echo "0  = copy code"
echo "1  = make clean"
echo "2  = make config"
echo "3  = compile"
echo "4  = unpack ramdisk"
echo "5  = patch ramdisk"
echo "6  = repack ramdisk"
echo "7  = analyse log"
echo "8  = transfer kernel"
echo "9  = send finish mail"
echo
echo "r = rewrite config"
echo "c = cleanup"
echo "b = backup"
echo 
echo "================================================"
echo 
echo "Parameters:"
echo
echo "  Boeffla version:  $BOEFFLA_VERSION"
echo "  Extended cmdline: $EXTENDED_CMDLINE"
echo "  Boeffla date:     $BOEFFLA_DATE"
echo "  Git branch:       $GIT_BRANCH"
echo
echo "  Toolchain:     $TOOLCHAIN"
echo "  Cross_compile: $TOOLCHAIN_COMPILE"
echo "  Root path:     $ROOT_PATH"
echo "  Root dir:      $ROOT_DIR_NAME"
echo "  Source path:   $SOURCE_PATH"
echo "  Build path:    $BUILD_PATH"
echo "  Repack path:   $REPACK_PATH"
echo
echo "================================================"

exit
