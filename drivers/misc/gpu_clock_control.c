/*
 * gpu_clock_control.c -- a clock control interface for the sgs2/3
 *
 *  Copyright (C) 2011 Michael Wodkins
 *  twitter - @xdanetarchy
 *  XDA-developers - netarchy
 *  modified by gokhanmoral
 *
 *  Modified by Andrei F. for Galaxy S3 / Perseus kernel (June 2012)
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of the GNU General Public License as published by the
 *  Free Software Foundation;
*
* BIKETRONIC MODS: Increase to 5 steps, MAX 800MHZ, MIN54
*
 *
 */

#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/kallsyms.h>

#define GPU_MAX_CLOCK 800
#define GPU_MIN_CLOCK 54

typedef struct mali_dvfs_tableTag{
    unsigned int clock;
    unsigned int freq;
    unsigned int vol;
}mali_dvfs_table;
typedef struct mali_dvfs_thresholdTag{
	unsigned int downthreshold;
	unsigned int upthreshold;
}mali_dvfs_threshold_table;
//BIKETRONIC MOD - changed to 5 steps
extern mali_dvfs_table mali_dvfs[5];
extern mali_dvfs_threshold_table mali_dvfs_threshold[5];

typedef struct mali_dvfs_staycount{
	unsigned int staycount;
}mali_dvfs_staycount_table;

//BIKETRONIC MOD - changed to 5 steps
extern mali_dvfs_staycount_table mali_dvfs_staycount[5];

//BIKETRONIC MOD - changed to 5 steps
static ssize_t gpu_clock_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return sprintf(buf, "Step0: %d\nStep1: %d\nStep2: %d\nStep3: %d\nStep4: %d\n"
						"Threshold0-1/up-down: %d%% %d%%\n"
						"Threshold1-2/up-down: %d%% %d%%\n"
						"Threshold2-3/up-down: %d%% %d%%\n"
						"Threshold3-4/up-down: %d%% %d%%\n",
		mali_dvfs[0].clock,
		mali_dvfs[1].clock,
		mali_dvfs[2].clock,
		mali_dvfs[3].clock,
		mali_dvfs[4].clock,
		mali_dvfs_threshold[0].upthreshold*100/255,
		mali_dvfs_threshold[1].downthreshold*100/255,
		mali_dvfs_threshold[1].upthreshold*100/255,
		mali_dvfs_threshold[2].downthreshold*100/255,
		mali_dvfs_threshold[2].upthreshold*100/255,
		mali_dvfs_threshold[3].downthreshold*100/255,
		mali_dvfs_threshold[3].upthreshold*100/255,
		mali_dvfs_threshold[4].downthreshold*100/255
		);
}

unsigned int g[8];

static ssize_t gpu_clock_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count) {
	unsigned int ret = -EINVAL;
	int i = 0;

	if ( (ret=sscanf(buf, "%d%% %d%% %d%% %d%% %d%% %d%% %d%% %d%%",
			 &g[0], &g[1], &g[2], &g[3], &g[4], &g[5], &g[6], &g[7]))
	      == 8 ) i=1;

	if(i) {
		if(g[1]<0 || g[0]>100 || g[3]<0 || g[2]>100 || g[5]<0 || g[4]>100 || g[7]<0 || g[6]>100)
			return -EINVAL;

		mali_dvfs_threshold[0].upthreshold = ((int)((255*g[0])/100));
		mali_dvfs_threshold[1].downthreshold = ((int)((255*g[1])/100));
		mali_dvfs_threshold[1].upthreshold = ((int)((255*g[2])/100));
		mali_dvfs_threshold[2].downthreshold = ((int)((255*g[3])/100));
		mali_dvfs_threshold[2].upthreshold = ((int)((255*g[4])/100));
		mali_dvfs_threshold[3].downthreshold = ((int)((255*g[5])/100));
		mali_dvfs_threshold[3].upthreshold = ((int)((255*g[5])/100));
		mali_dvfs_threshold[4].downthreshold = ((int)((255*g[6])/100));
	} else {
		if ( (ret=sscanf(buf, "%d %d %d %d %d", &g[0], &g[1], &g[2], &g[3], &g[4])) != 5)
			return -EINVAL;

		/* safety floor and ceiling - netarchy */
		for( i = 0; i < 5; i++ ) {
			if (g[i] < GPU_MIN_CLOCK) {
				g[i] = GPU_MIN_CLOCK;
			}
			else if (g[i] > GPU_MAX_CLOCK) {
				g[i] = GPU_MAX_CLOCK;
			}

			if(ret==5)
				mali_dvfs[i].clock=g[i];
		}
	}

	return count;
}

//BIKETRONIC MOD - changed to 5 steps
static ssize_t gpu_staycount_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return sprintf(buf, "%d %d %d %d %d\n",
	mali_dvfs_staycount[0].staycount,
	mali_dvfs_staycount[1].staycount,
	mali_dvfs_staycount[2].staycount,
	mali_dvfs_staycount[3].staycount,
	mali_dvfs_staycount[4].staycount
	);
}

//BIKETRONIC MOD - changed to 5 steps
static ssize_t gpu_staycount_store(struct device *dev, struct device_attribute *attr, const char *buf,
									size_t count) {
	unsigned int ret = -EINVAL;
	int i1, i2, i3, i4, i5;

	if ( (ret=sscanf(buf, "%d %d %d %d %d", &i1, &i2, &i3, &i4, &i5))!=5 ) //BUG - was 3
		return -EINVAL;
	else {
		mali_dvfs_staycount[0].staycount = i1;
		mali_dvfs_staycount[1].staycount = i2;
		mali_dvfs_staycount[2].staycount = i3;
		mali_dvfs_staycount[3].staycount = i4;
		mali_dvfs_staycount[4].staycount = i5;
	}
	return count;
}

static DEVICE_ATTR(gpu_control, S_IRUGO | S_IWUGO, gpu_clock_show, gpu_clock_store);
static DEVICE_ATTR(gpu_staycount, S_IRUGO | S_IWUGO, gpu_staycount_show, gpu_staycount_store);

static struct attribute *gpu_clock_control_attributes[] = {
	&dev_attr_gpu_control.attr,
	&dev_attr_gpu_staycount.attr,
	NULL
};

static struct attribute_group gpu_clock_control_group = {
	.attrs = gpu_clock_control_attributes,
};

static struct miscdevice gpu_clock_control_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "gpu_clock_control",
};

static int __init gpu_clock_control_init(void)
{
    int ret;

    pr_info("%s misc_register(%s)\n", __FUNCTION__, gpu_clock_control_device.name);

    ret = misc_register(&gpu_clock_control_device);
    if (ret) 
	{
	    pr_err("%s misc_register(%s) fail\n", __FUNCTION__, gpu_clock_control_device.name);
	    return 1;
	}
    if (sysfs_create_group(&gpu_clock_control_device.this_device->kobj, &gpu_clock_control_group) < 0) 
	{
	    pr_err("%s sysfs_create_group fail\n", __FUNCTION__);
	    pr_err("Failed to create sysfs group for device (%s)!\n", gpu_clock_control_device.name);
	}

    return 0;
}

static void __exit gpu_clock_control_exit(void)
{
	sysfs_remove_group(&gpu_clock_control_device.this_device->kobj, &gpu_clock_control_group);
	misc_deregister(&gpu_clock_control_device);
}

module_init( gpu_clock_control_init );
module_exit( gpu_clock_control_exit );

MODULE_AUTHOR("netarchy.gokhanmoral.AndreiLux");
MODULE_DESCRIPTION("Mali clock control interface module");
MODULE_LICENSE("GPL");
