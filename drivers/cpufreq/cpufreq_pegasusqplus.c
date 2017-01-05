/*
 *  drivers/cpufreq/cpufreq_pegasusqplus.c
 *
 *  Copyright (C)  2011 Samsung Electronics co. ltd
 *    ByungChang Cha <bc.cha@samsung.com>
 *
 *  Based on ondemand governor
 *  Copyright (C)  2001 Russell King
 *            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                      Jun Nakajima <jun.nakajima@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
* BIKETRONIC mod: This is a great governor (or should be) 
* Main issue is overclocking - 
* if you have a heavy load, any governor will go to full speed
* and this could happen for an indefinite period!
* Soltion:
* set a time limit for use of processor above say 1100Mhz
* Say at full OC 1920Mhz, at most 20% of the time
* At 1500Mhz, 50% of the time
* So after 1 secont at 1920Mhz it drops to 1500
* After 5 seonds at 1500 it drops to 1100
* Reset when frequency drops below 1100 for 20 seconds.
* idea:
* for example there is a variable with = 1 second left, add 1/10 second every 20/10 second 
* up to a maximum total.
* Otherwise try to be the same as the pegasususq governor
* because it works.
* Is this basically samsung's DVFS?
*
*/



#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/jiffies.h>
#include <linux/kernel_stat.h>
#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/reboot.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#define EARLYSUSPEND_HOTPLUGLOCK 1

 /*
 * runqueue average
 */

#define RQ_AVG_TIMER_RATE	10

struct runqueue_data {
	unsigned int nr_run_avg;
	unsigned int update_rate;
	int64_t last_time;
	int64_t total_time;
	struct delayed_work work;
	struct workqueue_struct *nr_run_wq;
	spinlock_t lock;
};

static struct runqueue_data *rq_data;
static void rq_work_fn(struct work_struct *work);

static void start_rq_work(void)
{
	rq_data->nr_run_avg = 0;
	rq_data->last_time = 0;
	rq_data->total_time = 0;
	if (rq_data->nr_run_wq == NULL)
		rq_data->nr_run_wq =
			create_singlethread_workqueue("nr_run_avg");

	queue_delayed_work(rq_data->nr_run_wq, &rq_data->work,
			   msecs_to_jiffies(rq_data->update_rate));
	return;
}

static void stop_rq_work(void)
{
	if (rq_data->nr_run_wq)
		cancel_delayed_work(&rq_data->work);
	return;
}

static int __init init_rq_avg(void)
{
	rq_data = kzalloc(sizeof(struct runqueue_data), GFP_KERNEL);
	if (rq_data == NULL) {
		pr_err("%s cannot allocate memory\n", __func__);
		return -ENOMEM;
	}
	spin_lock_init(&rq_data->lock);
	rq_data->update_rate = RQ_AVG_TIMER_RATE;
	INIT_DELAYED_WORK_DEFERRABLE(&rq_data->work, rq_work_fn);

	return 0;
}

static void rq_work_fn(struct work_struct *work)
{
	int64_t time_diff = 0;
	int64_t nr_run = 0;
	unsigned long flags = 0;
	int64_t cur_time = ktime_to_ns(ktime_get());

	spin_lock_irqsave(&rq_data->lock, flags);

	if (rq_data->last_time == 0)
		rq_data->last_time = cur_time;
	if (rq_data->nr_run_avg == 0)
		rq_data->total_time = 0;

	nr_run = nr_running() * 100;
	time_diff = cur_time - rq_data->last_time;
	do_div(time_diff, 1000 * 1000);

	if (time_diff != 0 && rq_data->total_time != 0) {
		nr_run = (nr_run * time_diff) +
			(rq_data->nr_run_avg * rq_data->total_time);
		do_div(nr_run, rq_data->total_time + time_diff);
	}
	rq_data->nr_run_avg = nr_run;
	rq_data->total_time += time_diff;
	rq_data->last_time = cur_time;

	if (rq_data->update_rate != 0)
		queue_delayed_work(rq_data->nr_run_wq, &rq_data->work,
				   msecs_to_jiffies(rq_data->update_rate));

	spin_unlock_irqrestore(&rq_data->lock, flags);
}

static unsigned int get_nr_run_avg(void)
{
	unsigned int nr_run_avg;
	unsigned long flags = 0;

	spin_lock_irqsave(&rq_data->lock, flags);
	nr_run_avg = rq_data->nr_run_avg;
	rq_data->nr_run_avg = 0;
	spin_unlock_irqrestore(&rq_data->lock, flags);

	return nr_run_avg;
}

/*
 * dbs is used in this file as a shortform for demandbased switching
 * It helps to keep variable names smaller, simpler
 */

#define DEF_SAMPLING_DOWN_FACTOR		(1) //was 2
#define MAX_SAMPLING_DOWN_FACTOR		(100000) //same
#define DEF_FREQUENCY_DOWN_DIFFERENTIAL		(5) // same
#define DEF_FREQUENCY_UP_THRESHOLD		(82) //was 85
/* for multiple freq_step */
#define DEF_UP_THRESHOLD_DIFF			(6)
#define DEF_FREQUENCY_MIN_SAMPLE_RATE		(10000) //same
#define MIN_FREQUENCY_UP_THRESHOLD		(11) //same
#define MAX_FREQUENCY_UP_THRESHOLD		(100) //same
#define DEF_SAMPLING_RATE			(30000)//faster than usq, was 50000
#define MIN_SAMPLING_RATE			(10000) //same
#define MAX_HOTPLUG_RATE			(40u)

/* reality:
* Takes a couple of seconds to drop out or drop in 1 Core
* Runs with 3 or 4 cores.
* Except on sleep it has 1 core?
*/
#define DEF_MAX_CPU_LOCK			(0)
#define DEF_MIN_CPU_LOCK			(0)
#define DEF_UP_NR_CPUS				(1) // ? same
#define DEF_CPU_ONLINE_BIAS_COUNT		(2) // at > this # CPUs:
#define DEF_CPU_ONLINE_BIAS_UP_THRESHOLD	(65) // >65% turn one on
#define DEF_CPU_ONLINE_BIAS_DOWN_THRESHOLD	(30) // <30% turn one off

// HOT PLUG
#define DEF_CPU_UP_RATE				(16) //was 10 16
#define DEF_CPU_DOWN_RATE			(30) //was 20 30

// NOT USED
#define DEF_FREQ_STEP				(37) //same
/* for multiple freq_step */
#define DEF_FREQ_STEP_DEC			(13) //new

#define DEF_START_DELAY				(0) //same

/* this simply changes the threshold */
#define UP_THRESHOLD_AT_MIN_FREQ		(40) //same
#define FREQ_FOR_RESPONSIVENESS			(1100000) //was 500000 was 400000
/* for fast decrease */
#define UP_THRESHOLD_AT_FAST_DOWN		(95) //new
#define FREQ_FOR_FAST_DOWN			(1500000) //new

/* BIKETRONIC_GOV */
/* keep system cool under load */
/* overclocking is for quick response only not sustained load */
/* gives 1 second of OC per 30 seconds */
/* must have 24 seconds at/below IDEAL_FREQ before resetting counter */
/* so OC COOL TIME COUNTER will be zero and then  */
/* set to a non zero number when oc time runs out */
/* set as a ratio */
/* for every cool time add one to OC time */
/* minimum OC time before using it */

#define IDEAL_FREQ				(1100000) //1100MHZ
#define IDEAL_HIGH_FREQ				(1500000) //1500MHZ
//#define MAX_FREQ				(1920000) //1920MHZ
#define OC_COOL_TIME_RATIO			(10) // 1/30 = 3% max use -> 0.8% normally
#define OC_COOL_TIME_MIN			(100) // *10ms = 1 sec to charge
#define OC_COOL_TIME_MAX			(3000) // *10ms = 30 sec to charge

/* PART 2 */
/* super fast response */
#define NORMAL_FREQ_STEP			(20) // 5% is minimum at 2000mhz. // not used - crash bug
#define NORMAL_FREQ_STEP_KHZ			(120000) //khz NOT USED // crash bug

#define IDLE_DOWN_THRESHOLD			(30)
#define IDLE_UP_THRESHOLD			(40)

#define IDLE_JUMP_IDEAL_UP_THRESHOLD		(90) //increase from 80
#define IDLE_JUMP_HIGH_UP_THRESHOLD		(100) // disable, as it spends too much time at 1500

#define IDEAL_DOWN_THRESHOLD			(10) // stay at 1100 unless cpu use drops off
#define IDEAL_UP_THRESHOLD			(80)
#define IDEAL_HIGH_DOWN_THRESHOLD		(70)

#define IDEAL_JUMP_OC_UP_THRESHOLD		(95)

#define OC_UP_THRESHOLD				(90)
#define OC_DOWN_THRESHOLD			(85)



#define HOTPLUG_DOWN_INDEX			(0)
#define HOTPLUG_UP_INDEX			(1)

#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE		
#define FLEX_MAX_FREQ				(800000) //new
#endif

#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
#define LCD_FREQ_KICK_IN_DOWN_DELAY		(20) //new
#define LCD_FREQ_KICK_IN_FREQ			(500000) //new

extern int _lcdfreq_lock(int lock);
#endif

// MACH MIDAS IS SET TO Y!
#ifdef CONFIG_MACH_MIDAS
static int hotplug_rq[4][2] = { 
	{0, 175}, {175, 275}, {275, 375}, {375, 0} //MACH MIDAS +75 
};

static int hotplug_freq[4][2] = {
	{0, 500000},
	{200000, 500000},
	{200000, 700000}, //was 2,5
	{400000, 0} //was 2,0
};
#else
static int hotplug_rq[4][2] = {
	{0, 100}, {100, 200}, {200, 300}, {300, 0} // same
};

static int hotplug_freq[4][2] = { //same
	{0, 500000},
	{200000, 500000},
	{200000, 500000},
	{200000, 0}
};
#endif

#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
static unsigned int max_duration = (CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE_MAX_DURATION);
static bool flexrate_enabled = true;
static unsigned int forced_rate;
static unsigned int flexrate_num_effective;
#endif

static unsigned int min_sampling_rate;

static void do_dbs_timer(struct work_struct *work);
static int cpufreq_governor_dbs(struct cpufreq_policy *policy,
				unsigned int event);

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_PEGASUSQPLUS
static
#endif
struct cpufreq_governor cpufreq_gov_pegasusqplus = {
	.name                   = "pegasusqplus",
	.governor               = cpufreq_governor_dbs,
	.owner                  = THIS_MODULE,
};

/* Sampling types */
enum {DBS_NORMAL_SAMPLE, DBS_SUB_SAMPLE};

struct cpu_dbs_info_s {
	cputime64_t prev_cpu_idle;
	cputime64_t prev_cpu_wall;
	cputime64_t prev_cpu_nice;
	struct cpufreq_policy *cur_policy;
	struct delayed_work work;
	struct work_struct up_work;
	struct work_struct down_work;
	struct cpufreq_frequency_table *freq_table;
	unsigned int rate_mult;
	int cpu;
	/*
	 * percpu mutex that serializes governor limit change with
	 * do_dbs_timer invocation. We do not want do_dbs_timer to run
	 * when user is changing the governor or limits.
	 */
	struct mutex timer_mutex;
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
	unsigned int flex_duration;
	int flex_hotplug_sample_delay;
	int flex_hotplug_sample_delay_count;
#endif
};
static DEFINE_PER_CPU(struct cpu_dbs_info_s, od_cpu_dbs_info);

struct workqueue_struct *dvfs_workqueueplus;

static unsigned int dbs_enable;	/* number of CPUs using this policy */

/*
 * dbs_mutex protects dbs_enable in governor start/stop.
 */
static DEFINE_MUTEX(dbs_mutex);
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
static DEFINE_MUTEX(flex_mutex);
#endif

static struct dbs_tuners {
	/* BIKETRONIC_GOV */
	unsigned int oc_freq;
	unsigned int cool_freq;
	unsigned int max_gt_oc_time;
	unsigned int max_oc_time;
	unsigned int oc_cool_time;

	unsigned int sampling_rate;
	unsigned int up_threshold;
	unsigned int up_threshold_diff;
	unsigned int down_differential;
	unsigned int ignore_nice;
	unsigned int sampling_down_factor;
	/* pegasusqplus tuners */
	unsigned int freq_step;
	unsigned int freq_step_dec;
	unsigned int cpu_up_rate;
	unsigned int cpu_down_rate;
	unsigned int up_nr_cpus;
	unsigned int cpu_online_bias_count;
	unsigned int cpu_online_bias_up_threshold;
	unsigned int cpu_online_bias_down_threshold;
	unsigned int max_cpu_lock;
	unsigned int min_cpu_lock;
	atomic_t hotplug_lock;
	unsigned int dvfs_debug;
	unsigned int max_freq;
	unsigned int min_freq;
#ifdef CONFIG_HAS_EARLYSUSPEND
	int early_suspend;
#endif
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
	unsigned int flex_sampling_rate;
	unsigned int flex_duration;
	unsigned int flex_max_freq;
#endif
#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
	int lcdfreq_enable;
	unsigned int lcdfreq_kick_in_down_delay;
	unsigned int lcdfreq_kick_in_down_left;
	unsigned int lcdfreq_kick_in_freq;
#endif
	unsigned int up_threshold_at_min_freq;
	unsigned int freq_for_responsiveness;

	unsigned int up_threshold_at_fast_down;
	unsigned int freq_for_fast_down;
} dbs_tuners_ins = {
	/* BIKETRONIC_GOV */
//	.oc_freq = OC_FREQ,
//	.cool_freq = COOL_FREQ,
//	.max_gt_oc_time = MAX_GT_OC_TIME,
//	.max_oc_time = MAX_OC_TIME,
//	.oc_cool_time = OC_COOL_TIME,

	.up_threshold = DEF_FREQUENCY_UP_THRESHOLD,
	.up_threshold_diff = DEF_UP_THRESHOLD_DIFF,
	.sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR,
	.down_differential = DEF_FREQUENCY_DOWN_DIFFERENTIAL,
	.ignore_nice = 0,
	.freq_step = DEF_FREQ_STEP,
	.freq_step_dec = DEF_FREQ_STEP_DEC,
	.cpu_up_rate = DEF_CPU_UP_RATE,
	.cpu_down_rate = DEF_CPU_DOWN_RATE,
	.up_nr_cpus = DEF_UP_NR_CPUS,
	.cpu_online_bias_count = DEF_CPU_ONLINE_BIAS_COUNT,
	.cpu_online_bias_up_threshold = DEF_CPU_ONLINE_BIAS_UP_THRESHOLD,
	.cpu_online_bias_down_threshold = DEF_CPU_ONLINE_BIAS_DOWN_THRESHOLD,
	.max_cpu_lock = DEF_MAX_CPU_LOCK,
	.min_cpu_lock = DEF_MIN_CPU_LOCK,
	.hotplug_lock = ATOMIC_INIT(0),
	.dvfs_debug = 0,
#ifdef CONFIG_HAS_EARLYSUSPEND
	.early_suspend = -1,
#endif
	.up_threshold_at_min_freq = UP_THRESHOLD_AT_MIN_FREQ,
	.freq_for_responsiveness = FREQ_FOR_RESPONSIVENESS,
	.up_threshold_at_fast_down = UP_THRESHOLD_AT_FAST_DOWN,
	.freq_for_fast_down = FREQ_FOR_FAST_DOWN,
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
	.flex_sampling_rate = DEF_SAMPLING_RATE,
	.flex_max_freq = FLEX_MAX_FREQ,
#endif
#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
	.lcdfreq_enable = false,
	.lcdfreq_kick_in_down_delay = LCD_FREQ_KICK_IN_DOWN_DELAY,
	.lcdfreq_kick_in_down_left = LCD_FREQ_KICK_IN_DOWN_DELAY,
	.lcdfreq_kick_in_freq = LCD_FREQ_KICK_IN_FREQ,
#endif
};


/*
 * CPU hotplug lock interface
 */

static atomic_t g_hotplug_count = ATOMIC_INIT(0);
static atomic_t g_hotplug_lock = ATOMIC_INIT(0);

static void apply_hotplug_lock(void)
{
	int online, possible, lock, flag;
	struct work_struct *work;
	struct cpu_dbs_info_s *dbs_info;

	/* do turn_on/off cpus */
	dbs_info = &per_cpu(od_cpu_dbs_info, 0); /* from CPU0 */
	online = num_online_cpus();
	possible = num_possible_cpus();
	lock = atomic_read(&g_hotplug_lock);
	flag = lock - online;

	if (flag == 0)
		return;

	work = flag > 0 ? &dbs_info->up_work : &dbs_info->down_work;

	pr_debug("%s online %d possible %d lock %d flag %d %d\n",
		 __func__, online, possible, lock, flag, (int)abs(flag));

	queue_work_on(dbs_info->cpu, dvfs_workqueueplus, work);
}

int cpufreq_pegasusqplus_cpu_lock(int num_core)
{
	int prev_lock;

	if (num_core < 1 || num_core > num_possible_cpus())
		return -EINVAL;

	prev_lock = atomic_read(&g_hotplug_lock);

	if (prev_lock != 0 && prev_lock < num_core)
		return -EINVAL;
	else if (prev_lock == num_core)
		atomic_inc(&g_hotplug_count);

	atomic_set(&g_hotplug_lock, num_core);
	atomic_set(&g_hotplug_count, 1);
	apply_hotplug_lock();

	return 0;
}

int cpufreq_pegasusqplus_cpu_unlock(int num_core)
{
	int prev_lock = atomic_read(&g_hotplug_lock);

	if (prev_lock < num_core)
		return 0;
	else if (prev_lock == num_core)
		atomic_dec(&g_hotplug_count);

	if (atomic_read(&g_hotplug_count) == 0)
		atomic_set(&g_hotplug_lock, 0);

	return 0;
}

void cpufreq_pegasusqplus_min_cpu_lock(unsigned int num_core)
{
	int online, flag;
	struct cpu_dbs_info_s *dbs_info;

	dbs_tuners_ins.min_cpu_lock = min(num_core, num_possible_cpus());

	dbs_info = &per_cpu(od_cpu_dbs_info, 0); /* from CPU0 */
	online = num_online_cpus();
	flag = (int)num_core - online;
	if (flag <= 0)
		return;
	queue_work_on(dbs_info->cpu, dvfs_workqueueplus, &dbs_info->up_work);
}

void cpufreq_pegasusqplus_min_cpu_unlock(void)
{
	int online, lock, flag;
	struct cpu_dbs_info_s *dbs_info;

	dbs_tuners_ins.min_cpu_lock = 0;

	dbs_info = &per_cpu(od_cpu_dbs_info, 0); /* from CPU0 */
	online = num_online_cpus();
	lock = atomic_read(&g_hotplug_lock);
	if (lock == 0)
		return;
	flag = lock - online;
	if (flag >= 0)
		return;
	queue_work_on(dbs_info->cpu, dvfs_workqueueplus, &dbs_info->down_work);
}

/*
 * History of CPU usage
 */
struct cpu_usage {
	unsigned int freq;
	unsigned int load[NR_CPUS];
	unsigned int rq_avg;
	unsigned int avg_load;
};

struct cpu_usage_history {
	struct cpu_usage usage[MAX_HOTPLUG_RATE];
	unsigned int num_hist;
	unsigned int oc_cool_time; // ADD HERE?
};

/* BIKETRONIC_GOV */
/*struct cpu_oc_history {
	unsigned int time_remaining_gt_oc_speed;
	unsigned int time_remaining_at_oc_speed;
	unsigned int time_remaining_cooling_speed;
};*/

struct cpu_usage_history *hotplug_historyplus;

static inline cputime64_t get_cpu_idle_time_jiffy(unsigned int cpu,
						  cputime64_t *wall)
{
	cputime64_t idle_time;
	cputime64_t cur_wall_time;
	cputime64_t busy_time;

	cur_wall_time = jiffies64_to_cputime64(get_jiffies_64());
	busy_time = cputime64_add(kstat_cpu(cpu).cpustat.user,
				  kstat_cpu(cpu).cpustat.system);

	busy_time = cputime64_add(busy_time, kstat_cpu(cpu).cpustat.irq);
	busy_time = cputime64_add(busy_time, kstat_cpu(cpu).cpustat.softirq);
	busy_time = cputime64_add(busy_time, kstat_cpu(cpu).cpustat.steal);
	busy_time = cputime64_add(busy_time, kstat_cpu(cpu).cpustat.nice);

	idle_time = cputime64_sub(cur_wall_time, busy_time);
	if (wall)
		*wall = (cputime64_t)jiffies_to_usecs(cur_wall_time);

	return (cputime64_t)jiffies_to_usecs(idle_time);
}

static inline cputime64_t get_cpu_idle_time(unsigned int cpu, cputime64_t *wall)
{
	u64 idle_time = get_cpu_idle_time_us(cpu, wall);

	if (idle_time == -1ULL)
		return get_cpu_idle_time_jiffy(cpu, wall);

	return idle_time;
}

/************************** sysfs interface ************************/

static ssize_t show_sampling_rate_min(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", min_sampling_rate);
}

define_one_global_ro(sampling_rate_min);

/* cpufreq_pegasusqplus Governor Tunables */
#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return sprintf(buf, "%u\n", dbs_tuners_ins.object);		\
}
show_one(sampling_rate, sampling_rate);
show_one(up_threshold, up_threshold);
show_one(up_threshold_diff, up_threshold_diff);
show_one(sampling_down_factor, sampling_down_factor);
show_one(ignore_nice_load, ignore_nice);
show_one(down_differential, down_differential);
show_one(freq_step, freq_step);
show_one(freq_step_dec, freq_step_dec);
show_one(cpu_up_rate, cpu_up_rate);
show_one(cpu_down_rate, cpu_down_rate);
show_one(up_nr_cpus, up_nr_cpus);
show_one(cpu_online_bias_count, cpu_online_bias_count);
show_one(cpu_online_bias_up_threshold, cpu_online_bias_up_threshold);
show_one(cpu_online_bias_down_threshold, cpu_online_bias_down_threshold);
show_one(max_cpu_lock, max_cpu_lock);
show_one(min_cpu_lock, min_cpu_lock);
show_one(dvfs_debug, dvfs_debug);
show_one(up_threshold_at_min_freq, up_threshold_at_min_freq);
show_one(freq_for_responsiveness, freq_for_responsiveness);
show_one(up_threshold_at_fast_down, up_threshold_at_fast_down);
show_one(freq_for_fast_down, freq_for_fast_down);

#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
show_one(lcdfreq_enable, lcdfreq_enable);
show_one(lcdfreq_kick_in_down_delay, lcdfreq_kick_in_down_delay);
show_one(lcdfreq_kick_in_freq, lcdfreq_kick_in_freq);
#endif

#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
static struct global_attr flexrate_forcerate;
static struct global_attr flexrate_enable;
static struct global_attr flexrate_max_freq;
static struct global_attr flexrate_num_effective_usage;
#endif

static ssize_t show_hotplug_lock(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", atomic_read(&g_hotplug_lock));
}

#define show_hotplug_param(file_name, num_core, up_down)		\
static ssize_t show_##file_name##_##num_core##_##up_down		\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return sprintf(buf, "%u\n", file_name[num_core - 1][up_down]);	\
}

#define store_hotplug_param(file_name, num_core, up_down)		\
static ssize_t store_##file_name##_##num_core##_##up_down		\
(struct kobject *kobj, struct attribute *attr,				\
	const char *buf, size_t count)					\
{									\
	unsigned int input;						\
	int ret;							\
	ret = sscanf(buf, "%u", &input);				\
	if (ret != 1)							\
		return -EINVAL;						\
	file_name[num_core - 1][up_down] = input;			\
	return count;							\
}

show_hotplug_param(hotplug_freq, 1, 1);
show_hotplug_param(hotplug_freq, 2, 0);
show_hotplug_param(hotplug_freq, 2, 1);
show_hotplug_param(hotplug_freq, 3, 0);
show_hotplug_param(hotplug_freq, 3, 1);
show_hotplug_param(hotplug_freq, 4, 0);

show_hotplug_param(hotplug_rq, 1, 1);
show_hotplug_param(hotplug_rq, 2, 0);
show_hotplug_param(hotplug_rq, 2, 1);
show_hotplug_param(hotplug_rq, 3, 0);
show_hotplug_param(hotplug_rq, 3, 1);
show_hotplug_param(hotplug_rq, 4, 0);

store_hotplug_param(hotplug_freq, 1, 1);
store_hotplug_param(hotplug_freq, 2, 0);
store_hotplug_param(hotplug_freq, 2, 1);
store_hotplug_param(hotplug_freq, 3, 0);
store_hotplug_param(hotplug_freq, 3, 1);
store_hotplug_param(hotplug_freq, 4, 0);

store_hotplug_param(hotplug_rq, 1, 1);
store_hotplug_param(hotplug_rq, 2, 0);
store_hotplug_param(hotplug_rq, 2, 1);
store_hotplug_param(hotplug_rq, 3, 0);
store_hotplug_param(hotplug_rq, 3, 1);
store_hotplug_param(hotplug_rq, 4, 0);

define_one_global_rw(hotplug_freq_1_1);
define_one_global_rw(hotplug_freq_2_0);
define_one_global_rw(hotplug_freq_2_1);
define_one_global_rw(hotplug_freq_3_0);
define_one_global_rw(hotplug_freq_3_1);
define_one_global_rw(hotplug_freq_4_0);

define_one_global_rw(hotplug_rq_1_1);
define_one_global_rw(hotplug_rq_2_0);
define_one_global_rw(hotplug_rq_2_1);
define_one_global_rw(hotplug_rq_3_0);
define_one_global_rw(hotplug_rq_3_1);
define_one_global_rw(hotplug_rq_4_0);

static ssize_t store_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.sampling_rate = max(input, min_sampling_rate);
	return count;
}

static ssize_t store_up_threshold(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
	    input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_threshold = input;
	return count;
}

static ssize_t store_up_threshold_diff(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD - dbs_tuners_ins.up_threshold ||
	    input < 1) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_threshold_diff = input;
	return count;
}

static ssize_t store_sampling_down_factor(struct kobject *a,
					  struct attribute *b,
					  const char *buf, size_t count)
{
	unsigned int input, j;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;
	dbs_tuners_ins.sampling_down_factor = input;

	/* Reset down sampling multiplier in case it was active */
	for_each_online_cpu(j) {
		struct cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(od_cpu_dbs_info, j);
		dbs_info->rate_mult = 1;
	}
	return count;
}

static ssize_t store_ignore_nice_load(struct kobject *a, struct attribute *b,
				      const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	unsigned int j;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == dbs_tuners_ins.ignore_nice) { /* nothing to do */
		return count;
	}
	dbs_tuners_ins.ignore_nice = input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(od_cpu_dbs_info, j);
		dbs_info->prev_cpu_idle =
			get_cpu_idle_time(j, &dbs_info->prev_cpu_wall);
		if (dbs_tuners_ins.ignore_nice)
			dbs_info->prev_cpu_nice = kstat_cpu(j).cpustat.nice;
	}
	return count;
}

static ssize_t store_down_differential(struct kobject *a, struct attribute *b,
				       const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.down_differential = min(input, 100u);
	return count;
}

static ssize_t store_freq_step(struct kobject *a, struct attribute *b,
			       const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.freq_step = min(input, 100u);
	return count;
}

static ssize_t store_freq_step_dec(struct kobject *a, struct attribute *b,
			       const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.freq_step_dec = min(input, dbs_tuners_ins.freq_step / 2);
	return count;
}

static ssize_t store_cpu_up_rate(struct kobject *a, struct attribute *b,
				 const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.cpu_up_rate = min(input, MAX_HOTPLUG_RATE);
	return count;
}

static ssize_t store_cpu_down_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.cpu_down_rate = min(input, MAX_HOTPLUG_RATE);
	return count;
}

static ssize_t store_up_nr_cpus(struct kobject *a, struct attribute *b,
				const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.up_nr_cpus = min(input, num_possible_cpus());
	return count;
}

static ssize_t store_cpu_online_bias_count(struct kobject *a, struct attribute *b,
				const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.cpu_online_bias_count = min(input, num_possible_cpus());
	return count;
}

static ssize_t store_cpu_online_bias_up_threshold(struct kobject *a, 
					      struct attribute *b,
					      const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
	    input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	dbs_tuners_ins.cpu_online_bias_up_threshold = input;
	return count;
}

static ssize_t store_cpu_online_bias_down_threshold(struct kobject *a, 
					      struct attribute *b,
					      const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
	    input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	dbs_tuners_ins.cpu_online_bias_down_threshold = input;
	return count;
}

static ssize_t store_max_cpu_lock(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.max_cpu_lock = min(input, num_possible_cpus());
	return count;
}

static ssize_t store_min_cpu_lock(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	if (input == 0)
		cpufreq_pegasusqplus_min_cpu_unlock();
	else
		cpufreq_pegasusqplus_min_cpu_lock(input);
	return count;
}

static ssize_t store_hotplug_lock(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	int prev_lock;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	input = min(input, num_possible_cpus());
	prev_lock = atomic_read(&dbs_tuners_ins.hotplug_lock);

	if (prev_lock)
		cpufreq_pegasusqplus_cpu_unlock(prev_lock);

	if (input == 0) {
		atomic_set(&dbs_tuners_ins.hotplug_lock, 0);
		return count;
	}

	ret = cpufreq_pegasusqplus_cpu_lock(input);
	if (ret) {
		printk(KERN_ERR "[HOTPLUG] already locked with smaller value %d < %d\n",
			atomic_read(&g_hotplug_lock), input);
		return ret;
	}

	atomic_set(&dbs_tuners_ins.hotplug_lock, input);

	return count;
}

static ssize_t store_dvfs_debug(struct kobject *a, struct attribute *b,
				const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.dvfs_debug = input > 0;
	return count;
}

static ssize_t store_up_threshold_at_min_freq(struct kobject *a, 
					      struct attribute *b,
					      const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
	    input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_threshold_at_min_freq = input;
	return count;
}

static ssize_t store_up_threshold_at_fast_down(struct kobject *a, 
					      struct attribute *b,
					      const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_FREQUENCY_UP_THRESHOLD ||
	    input < MIN_FREQUENCY_UP_THRESHOLD) {
		return -EINVAL;
	}
	dbs_tuners_ins.up_threshold_at_fast_down = input;
	return count;
}

static ssize_t store_freq_for_responsiveness(struct kobject *a,
					     struct attribute *b,
				   	     const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.freq_for_responsiveness = input;
	return count;
}

static ssize_t store_freq_for_fast_down(struct kobject *a,
					     struct attribute *b,
				   	     const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	dbs_tuners_ins.freq_for_fast_down = input;
	return count;
}

#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
static ssize_t store_flexrate_enable(struct kobject *a, struct attribute *b,
				     const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 0)
		flexrate_enabled = true;
	else
		flexrate_enabled = false;

	return count;
}

static ssize_t show_flexrate_enable(struct kobject *a, struct attribute *b,
				    char *buf)
{
	return sprintf(buf, "%d\n", !!flexrate_enabled);
}

static ssize_t store_flexrate_forcerate(struct kobject *a, struct attribute *b,
					 const char *buf, size_t count)
{
	unsigned int rate;
	int ret;

	ret = sscanf(buf, "%u", &rate);
	if (ret != 1)
		return -EINVAL;

	forced_rate = rate;

	pr_info("CAUTION: flexrate_forcerate is for debugging/benchmarking only.\n");
	return count;
}

static ssize_t show_flexrate_forcerate(struct kobject *a, struct attribute *b,
					char *buf)
{
	return sprintf(buf, "%u\n", forced_rate);
}

static ssize_t store_flexrate_max_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.flex_max_freq = input;
	return count;
}

static ssize_t show_flexrate_max_freq(struct kobject *a, struct attribute *b,
					char *buf)
{
	return sprintf(buf, "%u\n", dbs_tuners_ins.flex_max_freq);
}

static ssize_t show_flexrate_num_effective_usage(struct kobject *a,
						 struct attribute *b,
						 char *buf)
{
	return sprintf(buf, "%u\n", flexrate_num_effective);
}
#endif

#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
static ssize_t store_lcdfreq_enable(struct kobject *a, struct attribute *b,
				     const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 0)
		dbs_tuners_ins.lcdfreq_enable = true;
	else
		dbs_tuners_ins.lcdfreq_enable = false;

	return count;
}

static ssize_t store_lcdfreq_kick_in_down_delay(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1 && input < 0)
		return -EINVAL;

	dbs_tuners_ins.lcdfreq_kick_in_down_delay = input;
	dbs_tuners_ins.lcdfreq_kick_in_down_left =
				  dbs_tuners_ins.lcdfreq_kick_in_down_delay;
	return count;
}

static ssize_t store_lcdfreq_kick_in_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.lcdfreq_kick_in_freq = input;
	return count;
}
#endif

define_one_global_rw(sampling_rate);
define_one_global_rw(up_threshold);
define_one_global_rw(up_threshold_diff);
define_one_global_rw(sampling_down_factor);
define_one_global_rw(ignore_nice_load);
define_one_global_rw(down_differential);
define_one_global_rw(freq_step);
define_one_global_rw(freq_step_dec);
define_one_global_rw(cpu_up_rate);
define_one_global_rw(cpu_down_rate);
define_one_global_rw(up_nr_cpus);
define_one_global_rw(cpu_online_bias_count);
define_one_global_rw(cpu_online_bias_up_threshold);
define_one_global_rw(cpu_online_bias_down_threshold);
define_one_global_rw(max_cpu_lock);
define_one_global_rw(min_cpu_lock);
define_one_global_rw(hotplug_lock);
define_one_global_rw(dvfs_debug);
define_one_global_rw(up_threshold_at_min_freq);
define_one_global_rw(freq_for_responsiveness);
define_one_global_rw(up_threshold_at_fast_down);
define_one_global_rw(freq_for_fast_down);
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
define_one_global_rw(flexrate_forcerate);
define_one_global_rw(flexrate_enable);
define_one_global_rw(flexrate_max_freq);
define_one_global_ro(flexrate_num_effective_usage);
#endif
#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
define_one_global_rw(lcdfreq_enable);
define_one_global_rw(lcdfreq_kick_in_down_delay);
define_one_global_rw(lcdfreq_kick_in_freq);
#endif

static struct attribute *dbs_attributes[] = {
	&sampling_rate_min.attr,
	&sampling_rate.attr,
	&up_threshold.attr,
	&up_threshold_diff.attr,
	&sampling_down_factor.attr,
	&ignore_nice_load.attr,
	&down_differential.attr,
	&freq_step.attr,
	&freq_step_dec.attr,
	&cpu_up_rate.attr,
	&cpu_down_rate.attr,
	&up_nr_cpus.attr,
	&cpu_online_bias_count.attr,
	&cpu_online_bias_up_threshold.attr,
	&cpu_online_bias_down_threshold.attr,
	/* priority: hotplug_lock > max_cpu_lock > min_cpu_lock
	   Exception: hotplug_lock on early_suspend uses min_cpu_lock */
	&max_cpu_lock.attr,
	&min_cpu_lock.attr,
	&hotplug_lock.attr,
	&dvfs_debug.attr,
	&hotplug_freq_1_1.attr,
	&hotplug_freq_2_0.attr,
	&hotplug_freq_2_1.attr,
	&hotplug_freq_3_0.attr,
	&hotplug_freq_3_1.attr,
	&hotplug_freq_4_0.attr,
	&hotplug_rq_1_1.attr,
	&hotplug_rq_2_0.attr,
	&hotplug_rq_2_1.attr,
	&hotplug_rq_3_0.attr,
	&hotplug_rq_3_1.attr,
	&hotplug_rq_4_0.attr,
	&up_threshold_at_min_freq.attr,
	&freq_for_responsiveness.attr,
	&up_threshold_at_fast_down.attr,
	&freq_for_fast_down.attr,
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
	&flexrate_enable.attr,
	&flexrate_forcerate.attr,
	&flexrate_max_freq.attr,
	&flexrate_num_effective_usage.attr,
#endif
#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
	&lcdfreq_enable.attr,
	&lcdfreq_kick_in_down_delay.attr,
	&lcdfreq_kick_in_freq.attr,
#endif
	NULL
};

static struct attribute_group dbs_attr_group = {
	.attrs = dbs_attributes,
	.name = "pegasusqplus",
};

/************************** sysfs end ************************/

static void cpu_up_work(struct work_struct *work)
{
	int cpu;
	int online = num_online_cpus();
	int nr_up = dbs_tuners_ins.up_nr_cpus;
	int min_cpu_lock = dbs_tuners_ins.min_cpu_lock;
	int hotplug_lock = atomic_read(&g_hotplug_lock);

	if (hotplug_lock && min_cpu_lock)
		nr_up = max(hotplug_lock, min_cpu_lock) - online;
	else if (hotplug_lock)
		nr_up = hotplug_lock - online;
	else if (min_cpu_lock)
		nr_up = max(nr_up, min_cpu_lock - online);

	if (online == 1) {
		printk(KERN_ERR "CPU_UP 3\n");
		cpu_up(num_possible_cpus() - 1);
		nr_up -= 1;
	}

	for_each_cpu_not(cpu, cpu_online_mask) {
		if (nr_up-- == 0)
			break;
		if (cpu == 0)
			continue;
		printk(KERN_ERR "CPU_UP %d\n", cpu);
		cpu_up(cpu);
	}
}

static void cpu_down_work(struct work_struct *work)
{
	int cpu;
	int online = num_online_cpus();
	int nr_down = 1;
	int hotplug_lock = atomic_read(&g_hotplug_lock);

	if (hotplug_lock)
		nr_down = online - hotplug_lock;

	for_each_online_cpu(cpu) {
		if (cpu == 0)
			continue;
		printk(KERN_ERR "CPU_DOWN %d\n", cpu);
		cpu_down(cpu);
		if (--nr_down == 0)
			break;
	}
}

static void dbs_freq_increase(struct cpufreq_policy *p, unsigned int freq)
{
#ifndef CONFIG_ARCH_EXYNOS4
	if (p->cur == p->max)
		return;
#endif

	__cpufreq_driver_target(p, freq, CPUFREQ_RELATION_L);
}

/*
 * print hotplug debugging info.
 * which 1 : UP, 0 : DOWN
 */
static void debug_hotplug_check(int which, int rq_avg, int freq,
			 struct cpu_usage *usage)
{
	int cpu;
	printk(KERN_ERR "CHECK %s rq %d.%02d freq %d [", which ? "up" : "down",
	       rq_avg / 100, rq_avg % 100, freq);
	for_each_online_cpu(cpu) {
		printk(KERN_ERR "(%d, %d), ", cpu, usage->load[cpu]);
	}
	printk(KERN_ERR "]\n");
}

/* BIKETRONIC_GOV NOTE: check_up and check_down used for hot plugging */
static int check_up(void)
{
	int num_hist = hotplug_historyplus->num_hist;
	struct cpu_usage *usage;
	int freq, rq_avg;
	int avg_load;
	int i;
	int up_rate = dbs_tuners_ins.cpu_up_rate;
	int up_freq, up_rq;
	int min_freq = INT_MAX;
	int min_rq_avg = INT_MAX;
	int min_avg_load = INT_MAX;
	int online;
	int hotplug_lock = atomic_read(&g_hotplug_lock);

	if (hotplug_lock > 0)
		return 0;

	online = num_online_cpus();
	up_freq = hotplug_freq[online - 1][HOTPLUG_UP_INDEX];
	up_rq = hotplug_rq[online - 1][HOTPLUG_UP_INDEX];

	if (online == num_possible_cpus())
		return 0;

	if (dbs_tuners_ins.max_cpu_lock != 0
		&& online >= dbs_tuners_ins.max_cpu_lock)
		return 0;

	if (dbs_tuners_ins.min_cpu_lock != 0
		&& online < dbs_tuners_ins.min_cpu_lock)
		return 1;

	if (num_hist == 0 || num_hist % up_rate)
		return 0;

	for (i = num_hist - 1; i >= num_hist - up_rate; --i) {
		usage = &hotplug_historyplus->usage[i];

		freq = usage->freq;
		rq_avg =  usage->rq_avg;
		avg_load = usage->avg_load;

		min_freq = min(min_freq, freq);
		min_rq_avg = min(min_rq_avg, rq_avg);
		min_avg_load = min(min_avg_load, avg_load);

		if (dbs_tuners_ins.dvfs_debug)
			debug_hotplug_check(1, rq_avg, freq, usage);
	}

	if (min_freq >= up_freq && min_rq_avg > up_rq) { // freq > up_freq (=UP) and rq > up_rq (=UP)
		if (online >= dbs_tuners_ins.cpu_online_bias_count) { // num cpus online >= minimum (bias) (if less CPUs than bias online, don't check the following)
			if (min_avg_load < dbs_tuners_ins.cpu_online_bias_up_threshold) // min load < bias up threshold (=NOT UP)
				return 0;
		}
		printk(KERN_ERR "[HOTPLUG IN] %s %d>=%d && %d>%d\n",
			__func__, min_freq, up_freq, min_rq_avg, up_rq);
		hotplug_historyplus->num_hist = 0;
		return 1;
	}
	return 0;
}

static int check_down(void)
{
	int num_hist = hotplug_historyplus->num_hist;
	struct cpu_usage *usage;
	int freq, rq_avg;
	int avg_load;
	int i;
	int down_rate = dbs_tuners_ins.cpu_down_rate;
	int down_freq, down_rq;
	int max_freq = 0;
	int max_rq_avg = 0;
	int max_avg_load = 0;
	int online;
	int hotplug_lock = atomic_read(&g_hotplug_lock);

	if (hotplug_lock > 0)
		return 0;

	online = num_online_cpus();
	down_freq = hotplug_freq[online - 1][HOTPLUG_DOWN_INDEX];
	down_rq = hotplug_rq[online - 1][HOTPLUG_DOWN_INDEX];

	if (online == 1)
		return 0;

	if (dbs_tuners_ins.max_cpu_lock != 0
		&& online > dbs_tuners_ins.max_cpu_lock)
		return 1;

	if (dbs_tuners_ins.min_cpu_lock != 0
		&& online <= dbs_tuners_ins.min_cpu_lock)
		return 0;

	if (num_hist == 0 || num_hist % down_rate)
		return 0;

	for (i = num_hist - 1; i >= num_hist - down_rate; --i) {
		usage = &hotplug_historyplus->usage[i];

		freq = usage->freq;
		rq_avg =  usage->rq_avg;
		avg_load = usage->avg_load;

		max_freq = max(max_freq, freq);
		max_rq_avg = max(max_rq_avg, rq_avg);
		max_avg_load = max(max_avg_load, avg_load);

		if (dbs_tuners_ins.dvfs_debug)
			debug_hotplug_check(0, rq_avg, freq, usage);
	}

	if ((max_freq <= down_freq && max_rq_avg <= down_rq) // freq < down & rq < down OR?
		|| (online >= (dbs_tuners_ins.cpu_online_bias_count + 1) //(more than #bias cpus online AND
		    && max_avg_load < dbs_tuners_ins.cpu_online_bias_down_threshold)) { // load < bias_down)
		printk(KERN_ERR "[HOTPLUG OUT] %s %d<=%d && %d<%d\n",
			__func__, max_freq, down_freq, max_rq_avg, down_rq);
		hotplug_historyplus->num_hist = 0;
		return 1;
	}

	return 0;
}

/*BIKETRONIC_GOV note*/
/* main cpu freq logic here */
/* all mods here */
static void dbs_check_cpu(struct cpu_dbs_info_s *this_dbs_info)
{
	unsigned int max_load_freq;

	struct cpufreq_policy *policy;
	unsigned int j;
	int num_hist = hotplug_historyplus->num_hist;
	int max_hotplug_rate = max(dbs_tuners_ins.cpu_up_rate,
				   dbs_tuners_ins.cpu_down_rate);
	int up_threshold = dbs_tuners_ins.up_threshold;

	/* add total_load, avg_load to get average load */
	unsigned int total_load = 0;
	unsigned int avg_load = 0;
	unsigned int max_load = 0;
	int load_each[4] = {-1, -1, -1, -1};

	policy = this_dbs_info->cur_policy;

#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
	int hp_s_delay = this_dbs_info->flex_hotplug_sample_delay;
	int hp_s_delayc = this_dbs_info->flex_hotplug_sample_delay_count;

	if(hp_s_delay > 0 && hp_s_delay != hp_s_delayc) {
		hotplug_historyplus->usage[num_hist].freq = 
			(hotplug_historyplus->usage[num_hist].freq * 
			(hp_s_delayc - hp_s_delay) + policy->cur) / 
			(hp_s_delayc - hp_s_delay + 1);
	} else
#endif
		
	hotplug_historyplus->usage[num_hist].freq = policy->cur;
	
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
	if(hp_s_delay <= 1){
#endif
		
	hotplug_historyplus->usage[num_hist].rq_avg = get_nr_run_avg();
	++hotplug_historyplus->num_hist;
	
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
	}
#endif

	/* Get Absolute Load - in terms of freq */
	max_load_freq = 0;
	max_load = 0;

	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_info_s *j_dbs_info;
		cputime64_t cur_wall_time, cur_idle_time;
		cputime64_t prev_wall_time, prev_idle_time;
		unsigned int idle_time, wall_time;
		unsigned int load, load_freq;
		int freq_avg;

		j_dbs_info = &per_cpu(od_cpu_dbs_info, j);
		prev_wall_time = j_dbs_info->prev_cpu_wall; 
		prev_idle_time = j_dbs_info->prev_cpu_idle;

		cur_idle_time = get_cpu_idle_time(j, &cur_wall_time);

		wall_time = (unsigned int) cputime64_sub(cur_wall_time,
							 prev_wall_time);
		j_dbs_info->prev_cpu_wall = cur_wall_time;

		idle_time = (unsigned int) cputime64_sub(cur_idle_time,
							 prev_idle_time);
		j_dbs_info->prev_cpu_idle = cur_idle_time;

		if (dbs_tuners_ins.ignore_nice) {
			cputime64_t cur_nice;
			unsigned long cur_nice_jiffies;

			cur_nice = cputime64_sub(kstat_cpu(j).cpustat.nice,
						 j_dbs_info->prev_cpu_nice);
			/*
			 * Assumption: nice time between sampling periods will
			 * be less than 2^32 jiffies for 32 bit sys
			 */
			cur_nice_jiffies = (unsigned long)
				cputime64_to_jiffies64(cur_nice);

			j_dbs_info->prev_cpu_nice = kstat_cpu(j).cpustat.nice;
			idle_time += jiffies_to_usecs(cur_nice_jiffies);
		}

		if (unlikely(!wall_time || wall_time < idle_time))
			continue;

		load = 100 * (wall_time - idle_time) / wall_time; // 0-100 not idle

		/* keep load of each CPUs and combined load across all CPUs */
		if (cpu_online(j))
			load_each[j] = load;

		total_load += load;

#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
		if(hp_s_delay > 0 && hp_s_delay != hp_s_delayc)
		  hotplug_historyplus->usage[num_hist].load[j] = 
			(hotplug_historyplus->usage[num_hist].load[j] * 
			(hp_s_delayc - hp_s_delay) + load) / 
			(hp_s_delayc - hp_s_delay + 1);
		else
#endif
		hotplug_historyplus->usage[num_hist].load[j] = load;

		freq_avg = __cpufreq_driver_getavg(policy, j);
		if (freq_avg <= 0)
			freq_avg = policy->cur; //  freq_avg = freq * 1

		load_freq = load * freq_avg; // = 100 * freq_avg
		if (load_freq > max_load_freq)
			max_load_freq = load_freq;
	}

	/* calculate the average load across all related CPUs */
	avg_load = total_load / num_online_cpus();

	// ADDED
	max_load = max_load_freq / policy->cur; //added
	
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
	if(hp_s_delay > 0 && hp_s_delay != hp_s_delayc)
		hotplug_historyplus->usage[num_hist].avg_load = 
		(hotplug_historyplus->usage[num_hist].avg_load * 
		(hp_s_delayc - hp_s_delay) + avg_load) / 
		(hp_s_delayc - hp_s_delay + 1);
	else
#endif
	hotplug_historyplus->usage[num_hist].avg_load = avg_load;
	
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
	if(hp_s_delay <= 1) {
#endif
	/* Check for CPU hotplug */
	if (check_up()) {
		queue_work_on(this_dbs_info->cpu, dvfs_workqueueplus,
			      &this_dbs_info->up_work);
	} else if (check_down()) {
		queue_work_on(this_dbs_info->cpu, dvfs_workqueueplus,
			      &this_dbs_info->down_work);
	}
	if (hotplug_historyplus->num_hist  == max_hotplug_rate)
		hotplug_historyplus->num_hist = 0;
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
	}
#endif

/*
	check for overclocking for too long wasting battery
	if frequency > OC frequency, subtract from timer
	if frequency <= cool frequency, add to timer
	if frequency > OC frequency && timer <=0 then reduce speed
*/
	//use oc history
	// simple count of times this function is called...
	// OC COOL TIME COUNTER:
	if (policy->cur <= IDEAL_FREQ){
		hotplug_historyplus->oc_cool_time++;
		if (hotplug_historyplus->oc_cool_time > OC_COOL_TIME_MAX){
			hotplug_historyplus->oc_cool_time = OC_COOL_TIME_MAX;
		}
	} else if (policy->cur > IDEAL_HIGH_FREQ){ //overclocked
		if (hotplug_historyplus->oc_cool_time < OC_COOL_TIME_RATIO){
			hotplug_historyplus->oc_cool_time=0;
		} else {
//*			if (IDEAL_HIGH_FREQ >= policy->max){//avoid divide by zero
//*				hotplug_historyplus->oc_cool_time=0;
//*			} else {
			hotplug_historyplus->oc_cool_time=hotplug_historyplus->oc_cool_time - 
				OC_COOL_TIME_RATIO;
//*				((policy->cur - IDEAL_HIGH_FREQ)* OC_COOL_TIME_RATIO
//*				/ (policy->max - IDEAL_HIGH_FREQ)); // eg (1600-1500)/(2000-1500) * 24
//*			}
		}
	}

	// BIKETRONIC set thresholds for normal operation
	int down_threshold = 0;
	if (policy->cur < IDEAL_FREQ){ // IDLE-IDEAL
		up_threshold = IDLE_UP_THRESHOLD;
		down_threshold = IDLE_DOWN_THRESHOLD;
	} else if (policy->cur == IDEAL_FREQ){ // IDEAL
		up_threshold = IDEAL_UP_THRESHOLD;
		down_threshold = IDEAL_DOWN_THRESHOLD;
	} else if (policy->cur < IDEAL_HIGH_FREQ){ // IDEAL-HIGH
		up_threshold = IDEAL_UP_THRESHOLD;
		down_threshold = IDEAL_HIGH_DOWN_THRESHOLD;
	} else if (policy->cur = IDEAL_HIGH_FREQ){ // HIGH
		up_threshold = OC_UP_THRESHOLD;
		down_threshold = IDEAL_HIGH_DOWN_THRESHOLD;
	} else if (policy->cur > IDEAL_HIGH_FREQ){ // HIGH-OC
		up_threshold = OC_UP_THRESHOLD;
		down_threshold = OC_DOWN_THRESHOLD;
	}

	// BIKETRONIC set boost operation
//	boost_freq=0;
//	threshold = load or avg_load (over all CPUs)
/****	unsigned int boost_freq=0;
	if (policy->cur < IDEAL_FREQ){
		if (max_load > IDLE_JUMP_IDEAL_UP_THRESHOLD){
			boost_freq = IDEAL_FREQ;
		} else if (max_load > IDLE_JUMP_HIGH_UP_THRESHOLD){
			boost_freq = IDEAL_HIGH_FREQ;
		}
	} else if (max_load > IDEAL_JUMP_OC_UP_THRESHOLD){ // >=IDEAL FREQ
		boost_freq = policy->max;
	}
*/

	/* Check for frequency increase */
	/* old removed */
/*	if (policy->cur < dbs_tuners_ins.freq_for_responsiveness)
		up_threshold = dbs_tuners_ins.up_threshold_at_min_freq;
	else
		up_threshold = dbs_tuners_ins.up_threshold;
*/
	/* UP uses target */
	// NB max_load_freq = freq * 100
	if (max_load > up_threshold) {  // removed multiplier _freq *policy->cur
		unsigned int target, inc; //Changed to unsigned int
		target = 0; inc = 0;

		/* for multiple freq_step */
		/* use of this causes crashes and limits frequency to 1704 (120mhz or 1800 250mhz)
		//inc = (policy->max * NORMAL_FREQ_STEP) / 100; // FIX BUG /100
		//inc = NORMAL_FREQ_STEP_KHZ;

		/*remove complex freq step logic*/
		/* removal limits frequency to 1704Mhz and causes crashes */
		inc=policy->max *(dbs_tuners_ins.freq_step
				     - dbs_tuners_ins.freq_step_dec * 2) / 100; // e.g. max=1920 * (30 - 16*2) /100???

		// for multiple freq_step
		if (max_load_freq > (up_threshold + dbs_tuners_ins.up_threshold_diff * 2)
			* policy->cur)
			inc = policy->max * dbs_tuners_ins.freq_step / 100;
		else if (max_load_freq > (up_threshold + dbs_tuners_ins.up_threshold_diff)
			* policy->cur)
			inc = policy->max * (dbs_tuners_ins.freq_step
					- dbs_tuners_ins.freq_step_dec) / 100;
		
		target = min(policy->max, policy->cur + inc);
		
		// BIKETRONIC BOOST
/*****		if (boost_freq > 0 ){
			boost_freq = min(policy->max, boost_freq); //sanity check
			target = boost_freq;
		}
*/
		// BIKETRONIC OC time limit
		if (hotplug_historyplus->oc_cool_time < OC_COOL_TIME_MIN){
			target = min(target, IDEAL_HIGH_FREQ); //max IDEAL_HIGH_FREQ
		}

		/* If switching to max speed, apply sampling_down_factor */
		if (policy->cur < policy->max && target == policy->max)
			this_dbs_info->rate_mult =
				dbs_tuners_ins.sampling_down_factor;

		target = min(policy->max, target); // try to fix crash on 1920?
		target = max(policy->min, target);

		dbs_freq_increase(policy, target);

#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
		if(dbs_tuners_ins.lcdfreq_enable) {
			if(target > dbs_tuners_ins.lcdfreq_kick_in_freq) {
				dbs_tuners_ins.lcdfreq_kick_in_down_left =
				  dbs_tuners_ins.lcdfreq_kick_in_down_delay;
				_lcdfreq_lock(0);
			} else 
				dbs_tuners_ins.lcdfreq_kick_in_down_left--;
		}
#endif
		return;
	}

	/* Check for frequency decrease */
	/* if we cannot reduce the frequency anymore, break out early */
#ifndef CONFIG_ARCH_EXYNOS4
	if (policy->cur == policy->min)
		return;
#endif

#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
	/* Don't scale down when inside of a flexrate sub-sample */
	if(hp_s_delay > 0 && hp_s_delay != hp_s_delayc)
		return;
	else {
		/* 
		 * Recalculate max_load_freq based on the averaged histoic of 
		 * the previous normalized samples instead of the current sample.
		 */
		max_load_freq = policy->min; // not *100?

		for_each_cpu(j, policy->cpus) {
			unsigned int load_freq;

			load_freq = hotplug_historyplus->usage[num_hist].load[j] * 
				    hotplug_historyplus->usage[num_hist].freq; // is *100

			if (load_freq > max_load_freq)
				max_load_freq = load_freq; // is *100
		}
	}
#endif

	/*
	 * The optimal frequency is the frequency that is the lowest that
	 * can support the current CPU usage without triggering the up
	 * policy. To be safe, we focus DOWN_DIFFERENTIAL points under
	 * the threshold.
	 */
/*
	if (policy->cur > dbs_tuners_ins.freq_for_fast_down)
		up_threshold = dbs_tuners_ins.up_threshold_at_fast_down;
	else
		up_threshold = dbs_tuners_ins.up_threshold;
*/

	/* DOWN uses freq_next */
 	//down_threshold = up_threshold - dbs_tuners_ins.down_differential;

	// BIKETRONIC OC time limit
//****	if ((hotplug_historyplus->oc_cool_time <= OC_COOL_TIME_RATIO) && (policy->cur > IDEAL_HIGH_FREQ)){
//		freq_next = IDEAL_HIGH_FREQ;
//	}

	// other down
	// NB max_load_freq = max_freq * 100
	if (max_load_freq <
	    (down_threshold) * policy->cur) {
		unsigned int freq_next = 0;
		//unsigned int down_thres;
		
		// maxloadfreq = load * freq_avg (*100)
		// so it's the MHZ at 100% load *100
		// freq_next is the MHZ at down threshold
		// if down_threshold is really low this will be really high
		// e.g. 200MHZ*100 / 20 = 1000Mhz
		// so freq_next is freq* 1 
		freq_next = max_load_freq /
			(down_threshold); // e.g. 800mhz / 0.40??

		// prevent OC and jump below ideal
		if ((hotplug_historyplus->oc_cool_time <= OC_COOL_TIME_RATIO) && (policy->cur > IDEAL_HIGH_FREQ)){
			freq_next = IDEAL_HIGH_FREQ;
				// min(IDEAL_HIGH_FREQ, freq_next);
		}
		
		if (policy->cur > IDEAL_FREQ && freq_next < IDEAL_FREQ){
			freq_next = IDEAL_FREQ;
		}


		/* No longer fully busy, reset rate_mult */
		this_dbs_info->rate_mult = 1;

		if (freq_next < policy->min)
			freq_next = policy->min;


		/*down_thres = dbs_tuners_ins.up_threshold_at_min_freq
			- dbs_tuners_ins.down_differential;

		if (freq_next < dbs_tuners_ins.freq_for_responsiveness
			&& (max_load_freq / freq_next) > down_thres)
			freq_next = dbs_tuners_ins.freq_for_responsiveness;
		*/

//	}


//	if (freq_next > 0 ){
#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
		if(dbs_tuners_ins.lcdfreq_enable) {
			if(dbs_tuners_ins.lcdfreq_kick_in_freq < freq_next) {
				dbs_tuners_ins.lcdfreq_kick_in_down_left =
				  dbs_tuners_ins.lcdfreq_kick_in_down_delay;
			} else if(dbs_tuners_ins.lcdfreq_kick_in_down_left <= 0) {
				_lcdfreq_lock(1);
			} else {
				dbs_tuners_ins.lcdfreq_kick_in_down_left--;
			}
		}
#endif

		if (policy->cur == freq_next)
			return;

		freq_next = min(policy->max, freq_next); //bug fix?
		freq_next = max(policy->min, freq_next);

		/* BIKETRONIC_GOV note: CPU speed set here: freq_next */
		__cpufreq_driver_target(policy, freq_next,
					CPUFREQ_RELATION_L);
	}
}

/*BIKETRONIG_GOV note runs dbs_check_cpu*/
/* which is the main logic area */
static void do_dbs_timer(struct work_struct *work)
{
	struct cpu_dbs_info_s *dbs_info =
		container_of(work, struct cpu_dbs_info_s, work.work);
	unsigned int cpu = dbs_info->cpu;
	int delay;

	mutex_lock(&dbs_info->timer_mutex);

	dbs_check_cpu(dbs_info); //BIKETRONIC_GOV note: MAIN LOGIC

	delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate * dbs_info->rate_mult);
	
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
	if (dbs_info->flex_duration) {
		mutex_lock(&flex_mutex);
		
		if(dbs_info->cur_policy->cur < dbs_tuners_ins.flex_max_freq && 
		   dbs_info->cur_policy->cur < dbs_info->cur_policy->max ) {
			delay = usecs_to_jiffies(dbs_tuners_ins.flex_sampling_rate);

			if (--dbs_info->flex_duration < dbs_tuners_ins.flex_duration)
				dbs_tuners_ins.flex_duration = dbs_info->flex_duration;
			    
			if (dbs_info->flex_hotplug_sample_delay > 0) 
				--dbs_info->flex_hotplug_sample_delay;
		} else {
			dbs_info->flex_duration = 0;
			dbs_tuners_ins.flex_duration = 0;
			dbs_info->flex_hotplug_sample_delay = 0;
		}
		
		mutex_unlock(&flex_mutex);
	}
#endif /* CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE */

	/* We want all CPUs to do sampling nearly on
	 * same jiffy
	 */

	if (num_online_cpus() > 1)
		delay -= jiffies % delay;

	queue_delayed_work_on(cpu, dvfs_workqueueplus, &dbs_info->work, delay);
	mutex_unlock(&dbs_info->timer_mutex);
}

#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
int cpufreq_ondemand_flexrate_request(unsigned int rate_us, unsigned int duration)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);
	unsigned int cpu = policy->cpu;
	struct cpu_dbs_info_s *dbs_info = &per_cpu(od_cpu_dbs_info, 0);
	unsigned int sample_overflow = 0;
	bool now = 0;

#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
	/* hijack flexrate request as a touch lcdfreq boost */
	if(dbs_tuners_ins.lcdfreq_enable) {
		_lcdfreq_lock(0);
		dbs_tuners_ins.lcdfreq_kick_in_down_left =
				  dbs_tuners_ins.lcdfreq_kick_in_down_delay;
	}
#endif

	if (!flexrate_enabled)
		return 0;

	if (forced_rate)
		rate_us = forced_rate;

	if (rate_us >= dbs_tuners_ins.sampling_rate)
		return 0;

	if (policy->cur >= dbs_tuners_ins.flex_max_freq)
		return 0;

	mutex_lock(&flex_mutex);
	if (rate_us >= dbs_tuners_ins.flex_sampling_rate &&
	    duration <= dbs_tuners_ins.flex_duration)
		goto out;

	duration = min(max_duration, duration);
	if (rate_us > 0 && rate_us < min_sampling_rate)
		rate_us = min_sampling_rate;

	if (rate_us == 0 || duration == 0) {
		dbs_info->flex_duration = 0;
		goto out;
	}

	dbs_tuners_ins.flex_sampling_rate = rate_us;
	dbs_tuners_ins.flex_duration = max(dbs_info->flex_duration, duration);

	dbs_info->flex_duration = dbs_tuners_ins.flex_duration;

	if(dbs_info->flex_duration){
		sample_overflow = dbs_info->flex_hotplug_sample_delay;
		dbs_info->flex_hotplug_sample_delay_count =
			dbs_tuners_ins.sampling_rate / dbs_tuners_ins.flex_sampling_rate;
		dbs_info->flex_hotplug_sample_delay = 
			dbs_info->flex_hotplug_sample_delay_count - sample_overflow;
		if(dbs_info->flex_hotplug_sample_delay < 0)
			     dbs_info->flex_hotplug_sample_delay = 0;
	} else {
		dbs_info->flex_hotplug_sample_delay_count = 0;
		dbs_info->flex_hotplug_sample_delay = 0;
	}
	
	flexrate_num_effective++;

	mutex_unlock(&flex_mutex);
	mutex_lock(&dbs_info->timer_mutex);

	cancel_delayed_work_sync(&dbs_info->work);
	schedule_delayed_work_on(cpu, &dbs_info->work, 1);

	mutex_unlock(&dbs_info->timer_mutex);
	
	return 0;
out:
	mutex_unlock(&flex_mutex);

	return 0;
}

EXPORT_SYMBOL_GPL(cpufreq_ondemand_flexrate_request);

#endif

static inline void dbs_timer_init(struct cpu_dbs_info_s *dbs_info)
{
	/* We want all CPUs to do sampling nearly on same jiffy */
	int delay = usecs_to_jiffies(DEF_START_DELAY * 1000 * 1000
				     + dbs_tuners_ins.sampling_rate);
	if (num_online_cpus() > 1)
		delay -= jiffies % delay;

	INIT_DELAYED_WORK_DEFERRABLE(&dbs_info->work, do_dbs_timer);
	INIT_WORK(&dbs_info->up_work, cpu_up_work);
	INIT_WORK(&dbs_info->down_work, cpu_down_work);

	queue_delayed_work_on(dbs_info->cpu, dvfs_workqueueplus,
			      &dbs_info->work, delay + 2 * HZ);
}

static inline void dbs_timer_exit(struct cpu_dbs_info_s *dbs_info)
{
	cancel_delayed_work_sync(&dbs_info->work);
	cancel_work_sync(&dbs_info->up_work);
	cancel_work_sync(&dbs_info->down_work);
}

static int pm_notifier_call(struct notifier_block *this,
			    unsigned long event, void *ptr)
{
	static unsigned int prev_hotplug_lock;
	switch (event) {
	case PM_SUSPEND_PREPARE:
		prev_hotplug_lock = atomic_read(&g_hotplug_lock);
		atomic_set(&g_hotplug_lock, 1);
		apply_hotplug_lock();
		pr_debug("%s enter suspend\n", __func__);
		return NOTIFY_OK;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		atomic_set(&g_hotplug_lock, prev_hotplug_lock);
		if (prev_hotplug_lock)
			apply_hotplug_lock();
		prev_hotplug_lock = 0;
		pr_debug("%s exit suspend\n", __func__);
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static struct notifier_block pm_notifier = {
	.notifier_call = pm_notifier_call,
};

static int reboot_notifier_call(struct notifier_block *this,
				unsigned long code, void *_cmd)
{
	atomic_set(&g_hotplug_lock, 1);
	return NOTIFY_DONE;
}

static struct notifier_block reboot_notifier = {
	.notifier_call = reboot_notifier_call,
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend early_suspend;
unsigned int prev_freq_stepplus;
unsigned int prev_sampling_rateplus;
#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
int prev_lcdfreq_enable;
#endif
static void cpufreq_pegasusqplus_early_suspend(struct early_suspend *h)
{
#if EARLYSUSPEND_HOTPLUGLOCK
	dbs_tuners_ins.early_suspend =
		atomic_read(&g_hotplug_lock);
#endif
#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
	prev_lcdfreq_enable = dbs_tuners_ins.lcdfreq_enable;
	dbs_tuners_ins.lcdfreq_enable = false;
#endif
	prev_freq_stepplus = dbs_tuners_ins.freq_step;
	prev_sampling_rateplus = dbs_tuners_ins.sampling_rate;
	dbs_tuners_ins.freq_step = 20;
	dbs_tuners_ins.sampling_rate *= 4;
#if EARLYSUSPEND_HOTPLUGLOCK
	atomic_set(&g_hotplug_lock,
	    (dbs_tuners_ins.min_cpu_lock) ? dbs_tuners_ins.min_cpu_lock : 1);
	apply_hotplug_lock();
	stop_rq_work();
#endif
}
static void cpufreq_pegasusqplus_late_resume(struct early_suspend *h)
{
#if EARLYSUSPEND_HOTPLUGLOCK
	atomic_set(&g_hotplug_lock, dbs_tuners_ins.early_suspend);
#endif
#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
	dbs_tuners_ins.lcdfreq_enable = prev_lcdfreq_enable;
#endif
	dbs_tuners_ins.early_suspend = -1;
	dbs_tuners_ins.freq_step = prev_freq_stepplus;
	dbs_tuners_ins.sampling_rate = prev_sampling_rateplus;
#if EARLYSUSPEND_HOTPLUGLOCK
	apply_hotplug_lock();
	start_rq_work();
#endif
}
#endif

/* BIKETRONIC_GOV note */
/* main loop ? start_rq_work?*/
static int cpufreq_governor_dbs(struct cpufreq_policy *policy,
				unsigned int event)
{
	unsigned int cpu = policy->cpu;
	struct cpu_dbs_info_s *this_dbs_info;
	unsigned int j;
	int rc;

	this_dbs_info = &per_cpu(od_cpu_dbs_info, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) || (!policy->cur))
			return -EINVAL;

		dbs_tuners_ins.max_freq = policy->max;
		dbs_tuners_ins.min_freq = policy->min;
		hotplug_historyplus->num_hist = 0;
		hotplug_historyplus->oc_cool_time = 0; //BIKETRONIC_GOV add here?
		start_rq_work();

		mutex_lock(&dbs_mutex);

		dbs_enable++;
		for_each_cpu(j, policy->cpus) {
			struct cpu_dbs_info_s *j_dbs_info;
			j_dbs_info = &per_cpu(od_cpu_dbs_info, j);
			j_dbs_info->cur_policy = policy;

			j_dbs_info->prev_cpu_idle = get_cpu_idle_time(j,
				&j_dbs_info->prev_cpu_wall);
			if (dbs_tuners_ins.ignore_nice) {
				j_dbs_info->prev_cpu_nice =
					kstat_cpu(j).cpustat.nice;
			}
		}
		this_dbs_info->cpu = cpu;
		this_dbs_info->rate_mult = 1;
#ifdef CONFIG_CPU_FREQ_GOV_ONDEMAND_FLEXRATE
		this_dbs_info->flex_hotplug_sample_delay = 0;
		this_dbs_info->flex_hotplug_sample_delay_count = 0;
#endif
		/*
		 * Start the timerschedule work, when this governor
		 * is used for first time
		 */
		if (dbs_enable == 1) {
			rc = sysfs_create_group(cpufreq_global_kobject,
						&dbs_attr_group);
			if (rc) {
				mutex_unlock(&dbs_mutex);
				return rc;
			}

			min_sampling_rate = MIN_SAMPLING_RATE;
			dbs_tuners_ins.sampling_rate = DEF_SAMPLING_RATE;
		}
		mutex_unlock(&dbs_mutex);

		register_reboot_notifier(&reboot_notifier);

		mutex_init(&this_dbs_info->timer_mutex);
		dbs_timer_init(this_dbs_info);

#if !EARLYSUSPEND_HOTPLUGLOCK
		register_pm_notifier(&pm_notifier);
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
		register_early_suspend(&early_suspend);
#endif
		break;

	case CPUFREQ_GOV_STOP:
#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&early_suspend);
#endif
#if !EARLYSUSPEND_HOTPLUGLOCK
		unregister_pm_notifier(&pm_notifier);
#endif
#ifdef CONFIG_CPU_FREQ_LCD_FREQ_DFS
		_lcdfreq_lock(0);
#endif

		dbs_timer_exit(this_dbs_info);

		mutex_lock(&dbs_mutex);
		mutex_destroy(&this_dbs_info->timer_mutex);

		unregister_reboot_notifier(&reboot_notifier);

		dbs_enable--;
		mutex_unlock(&dbs_mutex);
		
		stop_rq_work();

		if (!dbs_enable)
			sysfs_remove_group(cpufreq_global_kobject,
					   &dbs_attr_group);

		break;

	case CPUFREQ_GOV_LIMITS:
		mutex_lock(&this_dbs_info->timer_mutex);

		if (policy->max < this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(this_dbs_info->cur_policy,
						policy->max,
						CPUFREQ_RELATION_H);
		else if (policy->min > this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(this_dbs_info->cur_policy,
						policy->min,
						CPUFREQ_RELATION_L);

		mutex_unlock(&this_dbs_info->timer_mutex);
		break;
	}
	return 0;
}

static int __init cpufreq_gov_dbs_init(void)
{
	int ret;

	ret = init_rq_avg();
	if (ret)
		return ret;

	hotplug_historyplus = kzalloc(sizeof(struct cpu_usage_history), GFP_KERNEL);
	if (!hotplug_historyplus) {
		pr_err("%s cannot create hotplug history array\n", __func__);
		ret = -ENOMEM;
		goto err_hist;
	}

	dvfs_workqueueplus = create_workqueue("kpegasusqplus");
	if (!dvfs_workqueueplus) {
		pr_err("%s cannot create workqueue\n", __func__);
		ret = -ENOMEM;
		goto err_queue;
	}

	ret = cpufreq_register_governor(&cpufreq_gov_pegasusqplus);
	if (ret)
		goto err_reg;

#ifdef CONFIG_HAS_EARLYSUSPEND
	early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	early_suspend.suspend = cpufreq_pegasusqplus_early_suspend;
	early_suspend.resume = cpufreq_pegasusqplus_late_resume;
#endif

	return ret;

err_reg:
	destroy_workqueue(dvfs_workqueueplus);
err_queue:
	kfree(hotplug_historyplus);
err_hist:
	kfree(rq_data);
	return ret;
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_pegasusqplus);
	destroy_workqueue(dvfs_workqueueplus);
	kfree(hotplug_historyplus);
	kfree(rq_data);
}

MODULE_AUTHOR("ByungChang Cha <bc.cha@samsung.com>");
MODULE_DESCRIPTION("'cpufreq_pegasusqplus' - A dynamic cpufreq/cpuhotplug governor");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_PEGASUSQPLUS
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
