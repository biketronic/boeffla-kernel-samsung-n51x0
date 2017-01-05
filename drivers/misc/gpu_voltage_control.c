/*
 * gpu_voltage_control.c -- gpu voltage control interface for the sgs2/3
 *
 *  Copyright (C) 2011 Michael Wodkins
 *  twitter - @xdanetarchy
 *  XDA-developers - netarchy
 *
 *  Modified for SiyahKernel
 *
 *  Modified by Andrei F. for Galaxy S3 / Perseus kernel (June 2012)
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of the GNU General Public License as published by the
 *  Free Software Foundation;
 *
*
* BIKETRONIC MODS: Increase to 5 steps
*
 *
 */

#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/kallsyms.h>

#define MIN_VOLTAGE_GPU  500000
#define MAX_VOLTAGE_GPU 1300000

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

//BIKETRONIC MOD - changed to 5 steps
unsigned int gv[5];

//BIKETRONIC MOD - changed to 5 steps
static ssize_t gpu_voltage_show(struct device *dev, struct device_attribute *attr, char *buf) {
	return sprintf(buf, "Step1: %d\nStep2: %d\nStep3: %d\nStep4: %d\nStep5: %d\n",
		       mali_dvfs[0].vol, mali_dvfs[1].vol,mali_dvfs[2].vol, mali_dvfs[3].vol, mali_dvfs[4].vol);
}

static ssize_t gpu_voltage_store(struct device *dev, struct device_attribute *attr, const char *buf,
									size_t count) {
	unsigned int ret = -EINVAL;
	int i = 0;

	ret = sscanf(buf, "%d %d %d %d %d", &gv[0], &gv[1], &gv[2], &gv[3], &gv[4]);
	if(ret!=5) return -EINVAL;

	/* safety floor and ceiling - netarchy */
	for( i = 0; i < 5; i++ ) {
		if (gv[i] < MIN_VOLTAGE_GPU) {
		    gv[i] = MIN_VOLTAGE_GPU;
		}
		else if (gv[i] > MAX_VOLTAGE_GPU) {
		    gv[i] = MAX_VOLTAGE_GPU;
		}
		if(ret==5)
		    mali_dvfs[i].vol=gv[i];
	}
	return count;
}

static DEVICE_ATTR(gpu_control, S_IRUGO | S_IWUGO, gpu_voltage_show, gpu_voltage_store);

static struct attribute *gpu_voltage_control_attributes[] = {
	&dev_attr_gpu_control.attr,
	NULL
};

static struct attribute_group gpu_voltage_control_group = {
	.attrs = gpu_voltage_control_attributes,
};

static struct miscdevice gpu_voltage_control_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "gpu_voltage_control",
};

static int __init gpu_voltage_control_init(void)
{
    int ret;
    
    pr_info("%s misc_register(%s)\n", __FUNCTION__, gpu_voltage_control_device.name);
        
    ret = misc_register(&gpu_voltage_control_device);
    if (ret)
        {
            pr_err("%s misc_register(%s) fail\n", __FUNCTION__, gpu_voltage_control_device.name);
            return 1;
        }
    if (sysfs_create_group(&gpu_voltage_control_device.this_device->kobj, &gpu_voltage_control_group) < 0)
	{
	    pr_err("%s sysfs_create_group fail\n", __FUNCTION__);
	    pr_err("Failed to create sysfs group for device (%s)!\n", gpu_voltage_control_device.name);
	}
                    
return 0;
}
                        
static void __exit gpu_voltage_control_exit(void)
{
        sysfs_remove_group(&gpu_voltage_control_device.this_device->kobj, &gpu_voltage_control_group);
        misc_deregister(&gpu_voltage_control_device);
}
                    
module_init( gpu_voltage_control_init );
module_exit( gpu_voltage_control_exit );
                        
MODULE_AUTHOR("netarchy.AndreiLux");
MODULE_DESCRIPTION("Mali voltage control interface module");
MODULE_LICENSE("GPL");
