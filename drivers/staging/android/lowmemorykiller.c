/* drivers/misc/lowmemorykiller.c
 *
 * The lowmemorykiller driver lets user-space specify a set of memory thresholds
 * where processes with a range of oom_score_adj values will get killed. Specify
 * the minimum oom_score_adj values in
 * /sys/module/lowmemorykiller/parameters/adj and the number of free pages in
 * /sys/module/lowmemorykiller/parameters/minfree. Both files take a comma
 * separated list of numbers in ascending order.
 *
 * For example, write "0,8" to /sys/module/lowmemorykiller/parameters/adj and
 * "1024,4096" to /sys/module/lowmemorykiller/parameters/minfree to kill
 * processes with a oom_score_adj value of 8 or higher when the free memory
 * drops below 4096 pages and kill processes with a oom_score_adj value of 0 or
 * higher when the free memory drops below 1024 pages.
 *
 * The driver considers memory used for caches to be free, but if a large
 * percentage of the cached memory is locked this can be very inaccurate
 * and processes may not get killed until the normal oom killer is triggered.
 *
 * Copyright (C) 2007-2008 Google, Inc.
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
/*******************change log***********************/
/* jiemin.zhu@oppo.com, 2014-10-23
 * 1. add kill duplicate tasks
 * 2. add kill burst
 * 3. add kernel thread to calculate
 * 4. other minor changes
 */
/*********************end***********************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

//#define CONFIG_ENHANCED_LMK

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/oom.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/swap.h>
#include <linux/fs.h>
#ifdef VENDOR_EDIT
//Lycan.Wang@Prd.BasicDrv, 2014-07-29 Add for workaround method for SHM memleak
#include <linux/ipc_namespace.h>
#endif /* VENDOR_EDIT */
#ifdef CONFIG_ENHANCED_LMK
//jiemin.zhu@oppo.com, 2014-10-23 Add for kill duplicate tasks
#include <linux/version.h>
#include <linux/list.h>
#include <linux/slab.h>
#endif /* CONFIG_ENHANCED_LMK */

#ifdef CONFIG_HIGHMEM
#define _ZONE ZONE_HIGHMEM
#else
#define _ZONE ZONE_NORMAL
#endif
#ifdef CONFIG_ENHANCED_LMK
//jiemin.zhu@oppo.com, 2014-10-23 Add for kill duplicate tasks
//you should reconfig your kernel for using this feature:
//devices drivers->staging drivers->android->enhanced lowmemorykiller 

#define MAX_PROCESS 2048	/* maximun kinds of processes in the system */
#define MAX_DUPLICATE_TASK   20 /* threshold */
#define LOWMEM_DEATHPENDING_DEPTH	3 /* default NUM kill in one burst */
#define MIN_ENHANCE_ADJ		58
#define DEFAULT_CAMERA 	"com.oppo.camera"
#define DEBAULT_LAUNCHER	"m.oppo.launcher"
#define MAX_FG_TASK	1

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0)
#define hlist_traversal_safe(tn, n, tmp, head, node)	tmp = NULL; hlist_for_each_entry_safe(tn, n, head, node)
#define hlist_traversal(tn, n, head, node)	n = NULL; hlist_for_each_entry(tn, head, node)
#else
#define hlist_traversal_safe(tn, n, tmp, head, node)	hlist_for_each_entry_safe(tn, n, tmp, head, node)
#define hlist_traversal(tn, n, head, node)	hlist_for_each_entry(tn, n, head, node)
#endif

struct task_node {
    struct hlist_node node;
    struct task_struct *task;
};
struct hlist_table {
    struct hlist_head head;
    int count;
};

struct rb_task_entry{
		struct rb_node adj_node;
			struct task_struct *task;	
};

static struct hlist_table *name_hash = NULL;
static struct work_struct lmk_work;
#endif /* CONFIG_ENHANCED_LMK */
static uint32_t lowmem_debug_level = 1;
static short lowmem_adj[6] = {
	0,
	1,
	6,
	12,
};
static int lowmem_adj_size = 4;
static int lowmem_minfree[6] = {
	3 * 512,	/* 6MB */
	2 * 1024,	/* 8MB */
	4 * 1024,	/* 16MB */
	16 * 1024,	/* 64MB */
};
static int lowmem_minfree_size = 4;
static int lmk_fast_run = 1;

static unsigned long lowmem_deathpending_timeout;

#define lowmem_print(level, x...)			\
	do {						\
		if (lowmem_debug_level >= (level))	\
			pr_info(x);			\
	} while (0)

static int test_task_flag(struct task_struct *p, int flag)
{
	struct task_struct *t = p;

	do {
		task_lock(t);
		if (test_tsk_thread_flag(t, flag)) {
			task_unlock(t);
			return 1;
		}
		task_unlock(t);
	} while_each_thread(p, t);

	return 0;
}

static DEFINE_MUTEX(scan_mutex);

int can_use_cma_pages(gfp_t gfp_mask)
{
	int can_use = 0;
	int mtype = allocflags_to_migratetype(gfp_mask);
	int i = 0;
	int *mtype_fallbacks = get_migratetype_fallbacks(mtype);

	if (is_migrate_cma(mtype)) {
		can_use = 1;
	} else {
		for (i = 0;; i++) {
			int fallbacktype = mtype_fallbacks[i];

			if (is_migrate_cma(fallbacktype)) {
				can_use = 1;
				break;
			}

			if (fallbacktype == MIGRATE_RESERVE)
				break;
		}
	}
	return can_use;
}

void tune_lmk_zone_param(struct zonelist *zonelist, int classzone_idx,
					int *other_free, int *other_file,
					int use_cma_pages)
{
	struct zone *zone;
	struct zoneref *zoneref;
	int zone_idx;

	for_each_zone_zonelist(zone, zoneref, zonelist, MAX_NR_ZONES) {
		zone_idx = zonelist_zone_idx(zoneref);
		if (zone_idx == ZONE_MOVABLE) {
			if (!use_cma_pages)
				*other_free -=
				    zone_page_state(zone, NR_FREE_CMA_PAGES);
			continue;
		}

		if (zone_idx > classzone_idx) {
			if (other_free != NULL)
				*other_free -= zone_page_state(zone,
							       NR_FREE_PAGES);
			if (other_file != NULL)
				*other_file -= zone_page_state(zone,
							       NR_FILE_PAGES)
					      - zone_page_state(zone, NR_SHMEM);
		} else if (zone_idx < classzone_idx) {
			if (zone_watermark_ok(zone, 0, 0, classzone_idx, 0)) {
				if (!use_cma_pages) {
					*other_free -= min(
					  zone->lowmem_reserve[classzone_idx] +
					  zone_page_state(
					    zone, NR_FREE_CMA_PAGES),
					  zone_page_state(
					    zone, NR_FREE_PAGES));
				} else {
					*other_free -=
					  zone->lowmem_reserve[classzone_idx];
				}
			} else {
				*other_free -=
					   zone_page_state(zone, NR_FREE_PAGES);
			}
		}
	}
}

#ifdef CONFIG_HIGHMEM
void adjust_gfp_mask(gfp_t *gfp_mask)
{
	struct zone *preferred_zone;
	struct zonelist *zonelist;
	enum zone_type high_zoneidx;

	if (current_is_kswapd()) {
		zonelist = node_zonelist(0, *gfp_mask);
		high_zoneidx = gfp_zone(*gfp_mask);
		first_zones_zonelist(zonelist, high_zoneidx, NULL,
				&preferred_zone);

		if (high_zoneidx == ZONE_NORMAL) {
			if (zone_watermark_ok_safe(preferred_zone, 0,
					high_wmark_pages(preferred_zone), 0,
					0))
				*gfp_mask |= __GFP_HIGHMEM;
		} else if (high_zoneidx == ZONE_HIGHMEM) {
			*gfp_mask |= __GFP_HIGHMEM;
		}
	}
}
#else
void adjust_gfp_mask(gfp_t *unused)
{
}
#endif

void tune_lmk_param(int *other_free, int *other_file, struct shrink_control *sc)
{
	gfp_t gfp_mask;
	struct zone *preferred_zone;
	struct zonelist *zonelist;
	enum zone_type high_zoneidx, classzone_idx;
	unsigned long balance_gap;
	int use_cma_pages;

	gfp_mask = sc->gfp_mask;
	adjust_gfp_mask(&gfp_mask);

	zonelist = node_zonelist(0, gfp_mask);
	high_zoneidx = gfp_zone(gfp_mask);
	first_zones_zonelist(zonelist, high_zoneidx, NULL, &preferred_zone);
	classzone_idx = zone_idx(preferred_zone);
	use_cma_pages = can_use_cma_pages(gfp_mask);

	balance_gap = min(low_wmark_pages(preferred_zone),
			  (preferred_zone->present_pages +
			   KSWAPD_ZONE_BALANCE_GAP_RATIO-1) /
			   KSWAPD_ZONE_BALANCE_GAP_RATIO);

	if (likely(current_is_kswapd() && zone_watermark_ok(preferred_zone, 0,
			  high_wmark_pages(preferred_zone) + SWAP_CLUSTER_MAX +
			  balance_gap, 0, 0))) {
		if (lmk_fast_run)
			tune_lmk_zone_param(zonelist, classzone_idx, other_free,
				       other_file, use_cma_pages);
		else
			tune_lmk_zone_param(zonelist, classzone_idx, other_free,
				       NULL, use_cma_pages);

		if (zone_watermark_ok(preferred_zone, 0, 0, _ZONE, 0)) {
			if (!use_cma_pages) {
				*other_free -= min(
				  preferred_zone->lowmem_reserve[_ZONE]
				  + zone_page_state(
				    preferred_zone, NR_FREE_CMA_PAGES),
				  zone_page_state(
				    preferred_zone, NR_FREE_PAGES));
			} else {
				*other_free -=
				  preferred_zone->lowmem_reserve[_ZONE];
			}
		} else {
			*other_free -= zone_page_state(preferred_zone,
						      NR_FREE_PAGES);
		}

		lowmem_print(4, "lowmem_shrink of kswapd tunning for highmem "
			     "ofree %d, %d\n", *other_free, *other_file);
	} else {
		tune_lmk_zone_param(zonelist, classzone_idx, other_free,
			       other_file, use_cma_pages);

		if (!use_cma_pages) {
			*other_free -=
			  zone_page_state(preferred_zone, NR_FREE_CMA_PAGES);
		}

		lowmem_print(4, "lowmem_shrink tunning for others ofree %d, "
			     "%d\n", *other_free, *other_file);
	}
}

#ifdef CONFIG_ENHANCED_LMK
//jiemin.zhu@oppo.com, 2014-10-21 Add for sort tasks in rb tree and kill duplicate tasks
DEFINE_MUTEX(hlist_lock);
EXPORT_SYMBOL(hlist_lock);
static struct workqueue_struct *lowmemorykiller_wq = NULL;

unsigned long task_hash(const char *str)
{
	unsigned long hash = 0;
	unsigned long seed = 131313;

	while (*str) {
		hash = hash * seed + (*str++);
	}

	return (hash & (MAX_PROCESS - 1));
}

static bool protected_apps(char *comm)
{
	if (strcmp(comm, "system_server") == 0 ||
		strcmp(comm, "m.android.phone") == 0 ||
		strcmp(comm, "d.process.media") == 0 ||
		strcmp(comm, "ureguide.custom") == 0 ||
		strcmp(comm, "ndroid.systemui") == 0 ||
		strcmp(comm, "nManagerService") == 0) {
		return 1;
	}
	return 0;
}

int remove_task_hlist(const struct task_struct *task)
{
	unsigned long idx;
	struct hlist_head *head;
	struct task_node *tn;
    struct hlist_node *n, *tmp;

    if (!name_hash)
    	return 0;

	idx = task_hash(task->comm);
	head = &(name_hash[idx].head);

	 hlist_traversal_safe(tn, n, tmp, head, node) {
	 	if (task->pid == tn->task->pid) {
	 		if (name_hash[idx].count > 0)
        		name_hash[idx].count--;
	 		hlist_del(&(tn->node));
	 		kfree(tn);
	 	}
	 }

	 return 0;
}
EXPORT_SYMBOL(remove_task_hlist);

static int resident_task_kill(void)
{
	struct task_struct *tsk;
	struct task_struct *selected = NULL;
	int tasksize;
	int selected_tasksize = 0;
	int selected_oom_score_adj;
	int fg_camera = 0, i = 0, j;
	int ret = 0;
	struct task_struct *fg_task[MAX_FG_TASK] = {NULL, };

	for_each_process(tsk) {
		struct task_struct *p;
		int oom_score_adj;

		if (tsk->flags & PF_KTHREAD)
			continue;

		/* if task no longer has any memory ignore it */
		if (test_task_flag(tsk, TIF_MM_RELEASED))
			continue;	

		if (test_task_flag(tsk, TIF_MEMDIE) || tsk->exit_state == EXIT_ZOMBIE) {
			continue;
		}

		if (protected_apps(tsk->comm)) {
			continue;
		}

		p = find_lock_task_mm(tsk);
		if (!p)
			continue;

		oom_score_adj = p->signal->oom_score_adj;
		tasksize = get_mm_rss(p->mm);
		task_unlock(p);
		if (oom_score_adj > 0 || oom_score_adj == -941)
			continue;
		if (tasksize <= 0)
			continue;
		/* camera task is in the fg, kill other fg tasks */
		if (oom_score_adj == 0) {
			if (strcmp(p->comm, DEFAULT_CAMERA) == 0)
				fg_camera = 1;
			else if (strncmp("Bind", p->comm, 4) && i < MAX_FG_TASK) {
				fg_task[i] = p;
				i++;
			}
			continue;
		}		

		if (selected) {
			if (oom_score_adj < selected_oom_score_adj)
				continue;
			if (oom_score_adj == selected_oom_score_adj &&
			    tasksize <= selected_tasksize)
				continue;
		}
		selected = p;
		selected_tasksize = tasksize;
		selected_oom_score_adj = oom_score_adj;
		lowmem_print(1, "select %d (%s), adj %d, size %d, to kill\n",
			     p->pid, p->comm, oom_score_adj, tasksize);
	}
	
	if (selected) {
		lowmem_print(1, "Killing %d (%s), adj %d, size %d from %s\n",
			     selected->pid, selected->comm,
			     selected_oom_score_adj, selected_tasksize, current->comm);

		send_sig(SIGKILL, selected, 0);
		set_tsk_thread_flag(selected, TIF_MEMDIE);
		ret++;
	}
	if (fg_camera) {
		for (j = 0; j < MAX_FG_TASK; j++) {
			if (fg_task[j]) {
				lowmem_print(1, "send sigkill to fg task %d (%s)\n",
			     		fg_task[j]->pid, fg_task[j]->comm);
				send_sig(SIGKILL, fg_task[j], 0);
				set_tsk_thread_flag(fg_task[j], TIF_MEMDIE);
				ret++;
			}
		}
	}
	return ret;
}

/* whether task is duplicate in one list */
static int duplicate_task(const struct task_struct *task, struct hlist_head *head, int idx)                                                               
{
    struct task_node *tn;
    struct hlist_node *n, *tmp; 
    pid_t pid1, pid2;
    bool ret = false;

	mutex_lock(&hlist_lock);
    pid1 = task->pid;
    hlist_traversal_safe(tn, n, tmp, head, node) {
        pid2 = tn->task->pid;
        if (pid2 == pid1) {
            ret = true;
            break;
        }   
    }   
    mutex_unlock(&hlist_lock);

    return ret;
}

/* Find the most frequently name in the list, preventing collision */
static int __kill_task(struct hlist_head *head)
{
	struct task_node *tn;
	struct hlist_node *n, *tmp; 
	struct task_struct *p;
	int task_size = 0;

	hlist_traversal_safe(tn, n, tmp, head, node) {
		p = find_lock_task_mm(tn->task);
		if (!p) {
			hlist_del(&(tn->node));
			kfree(tn);
			continue;
		}
		task_size += get_mm_rss(p->mm);
		task_unlock(p);

		set_tsk_thread_flag(p, TIF_MEMDIE);
		send_sig(SIGKILL, p, 0); 
		hlist_del(&(tn->node));
		kfree(tn);
	}
	return task_size;
}

static int kill_task(unsigned long idx)
{
	int task_size = 0;
	
	/* kill all task in the list */
	task_size = __kill_task(&(name_hash[idx].head));
	lowmem_print(1, "kill %d tasks, free %ldkB memory\n", 
	 			name_hash[idx].count, task_size * (long)(PAGE_SIZE / 1024));
	name_hash[idx].count = 0;
	 
    return task_size;
}
// Add for kill duplicate tasks end

//jiemin.zhu@oppo.com, 2014-10-21 Add for workqueue function
/* Be very carefull, this function is in kworker thread context */
static void lmk_wq_func(struct work_struct *work)
{
	struct task_struct *tsk;
	struct hlist_head *head;
	struct task_node *tn;
	unsigned long idx;

	rcu_read_lock();	
	for_each_process(tsk) {
		if (tsk->flags & PF_KTHREAD)
			continue;

		/* protectd apps do not put in the hlist */
		if (protected_apps(tsk->comm))
			continue;

		/* if task no longer has any memory ignore it */
		if (test_task_flag(tsk, TIF_MM_RELEASED))
			continue;

		if (test_task_flag(tsk, TIF_MEMDIE) || tsk->exit_state == EXIT_ZOMBIE) {
			continue;
		}		
		
		/* find which list to insert */
		idx = task_hash(tsk->comm);
		head = &(name_hash[idx].head);

		if (duplicate_task(tsk, head, idx))
			continue;

		if (tsk->flags & PF_EXITING) {
			/* avoid of reinput into hlist */
			continue;
		}	

		tn = (struct task_node*)kmalloc(sizeof(struct task_node), GFP_ATOMIC);
		if (!tn) {
			lowmem_print(1, "alloc task node failed\n");
			rcu_read_unlock();
			return;
		}
		mutex_lock(&hlist_lock);
		tn->task = tsk;
		hlist_add_head(&(tn->node), head);
		name_hash[idx].count++;
		if (name_hash[idx].count >= MAX_DUPLICATE_TASK) {
			kill_task(idx);
		}
		mutex_unlock(&hlist_lock);
	}	

	rcu_read_unlock();
}
#endif /* CONFIG_ENHANCED_LMK */

static int lowmem_shrink(struct shrinker *s, struct shrink_control *sc)
{
	struct task_struct *tsk;
#ifdef CONFIG_ENHANCED_LMK
	//jiemin.zhu@oppo.com, 2014-10-21 Add for kill tasks burst
	struct task_struct *selected[LOWMEM_DEATHPENDING_DEPTH] = {NULL,};
	int lowmem_enhance_threshold = 1;
	int have_selected = 0;
#else
	struct task_struct *selected = NULL;
#endif	/* CONFIG_ENHANCED_LMK */
	int rem = 0;
	int tasksize;
	int i;
#ifdef CONFIG_ENHANCED_LMK
//jiemin.zhu@oppo.com, 2014-10-21 Add for kill tasks burst
	int min_score_adj = OOM_SCORE_ADJ_MAX + 1;
	int selected_tasksize[LOWMEM_DEATHPENDING_DEPTH] = {0,};
	int selected_oom_score_adj[LOWMEM_DEATHPENDING_DEPTH] = {OOM_ADJUST_MAX,};
	int all_selected_oom = 0;
	int max_selected_oom_idx = 0;
#else
	short min_score_adj = OOM_SCORE_ADJ_MAX + 1;	
	int selected_tasksize = 0;
	short selected_oom_score_adj;
#endif	/* CONFIG_ENHANCED_LMK */
	int array_size = ARRAY_SIZE(lowmem_adj);
	int other_free;
	int other_file;
	int minfree = 0;
	unsigned long nr_to_scan = sc->nr_to_scan;

	if (nr_to_scan > 0) {
		if (mutex_lock_interruptible(&scan_mutex) < 0)
			return 0;
	}
	other_free = global_page_state(NR_FREE_PAGES);

	if (global_page_state(NR_SHMEM) + total_swapcache_pages() <
		global_page_state(NR_FILE_PAGES))
		other_file = global_page_state(NR_FILE_PAGES) -
						global_page_state(NR_SHMEM) -
						total_swapcache_pages();
	else
		other_file = 0;

	tune_lmk_param(&other_free, &other_file, sc);

	if (lowmem_adj_size < array_size)
		array_size = lowmem_adj_size;
	if (lowmem_minfree_size < array_size)
		array_size = lowmem_minfree_size;
	for (i = 0; i < array_size; i++) {
		minfree = lowmem_minfree[i];
		if (other_free < minfree && other_file < minfree) {
			min_score_adj = lowmem_adj[i];
			break;
		}
	}
	if (nr_to_scan > 0)
		lowmem_print(3, "lowmem_shrink %lu, %x, ofree %d %d, ma %hd\n",
				nr_to_scan, sc->gfp_mask, other_free,
				other_file, min_score_adj);
	rem = global_page_state(NR_ACTIVE_ANON) +
		global_page_state(NR_ACTIVE_FILE) +
		global_page_state(NR_INACTIVE_ANON) +
		global_page_state(NR_INACTIVE_FILE);
	if (nr_to_scan <= 0 || min_score_adj == OOM_SCORE_ADJ_MAX + 1) {
		lowmem_print(5, "lowmem_shrink %lu, %x, return %d\n",
			     nr_to_scan, sc->gfp_mask, rem);

		if (nr_to_scan > 0)
			mutex_unlock(&scan_mutex);

		return rem;
	}
#ifdef CONFIG_ENHANCED_LMK
//jiemin.zhu@oppo.com, 2014-10-21 Add for kill tasks burst
	for (i = 0; i < LOWMEM_DEATHPENDING_DEPTH; i++)
		selected_oom_score_adj[i] = min_score_adj;
	if (min_score_adj <= 117)
		lowmem_enhance_threshold = LOWMEM_DEATHPENDING_DEPTH;
#else
	selected_oom_score_adj = min_score_adj;
#endif	/* CONFIG_ENHANCED_LMK */	
	
	rcu_read_lock();
	for_each_process(tsk) {
		struct task_struct *p;
		short oom_score_adj;
#ifdef CONFIG_ENHANCED_LMK
//jiemin.zhu@oppo.com, 2014-10-21 Add for kill tasks burst
		int is_exist_oom_task = 0;
#endif /* CONFIG_ENHANCED_LMK */		

		if (tsk->flags & PF_KTHREAD)
			continue;

		/* if task no longer has any memory ignore it */
		if (test_task_flag(tsk, TIF_MM_RELEASED))
			continue;

		if (time_before_eq(jiffies, lowmem_deathpending_timeout)) {
			if (test_task_flag(tsk, TIF_MEMDIE) || tsk->exit_state == EXIT_ZOMBIE) {
				rcu_read_unlock();
				/* give the system time to free up the memory */
				msleep_interruptible(20);
				mutex_unlock(&scan_mutex);
#ifdef CONFIG_ENHANCED_LMK				
				queue_work(lowmemorykiller_wq, &lmk_work);
#endif				
				return 0;
			}
		}

		p = find_lock_task_mm(tsk);
		if (!p)
			continue;

		oom_score_adj = p->signal->oom_score_adj;
		if (oom_score_adj < min_score_adj) {
			task_unlock(p);
			continue;
		}
		tasksize = get_mm_rss(p->mm);
		task_unlock(p);
		if (tasksize <= 0)
			continue;

#ifdef CONFIG_ENHANCED_LMK
//jiemin.zhu@oppo.com, 2014-10-21 Add for kill tasks burst
		if (all_selected_oom < lowmem_enhance_threshold) {
			for (i = 0; i < lowmem_enhance_threshold; i++) {
				if (!selected[i]) {
					is_exist_oom_task = 1;
					max_selected_oom_idx = i;
					break;
				}
			}
		} else if (selected_oom_score_adj[max_selected_oom_idx] < oom_score_adj ||
			(selected_oom_score_adj[max_selected_oom_idx] == oom_score_adj &&
			selected_tasksize[max_selected_oom_idx] < tasksize)) {
			is_exist_oom_task = 1;
		}

		if (is_exist_oom_task) {
			selected[max_selected_oom_idx] = p;
			selected_tasksize[max_selected_oom_idx] = tasksize;
			selected_oom_score_adj[max_selected_oom_idx] = oom_score_adj;

			if (all_selected_oom < lowmem_enhance_threshold)
				all_selected_oom++;

			if (all_selected_oom == lowmem_enhance_threshold) {
				for (i = 0; i < lowmem_enhance_threshold; i++) {
					if (selected_oom_score_adj[i] < selected_oom_score_adj[max_selected_oom_idx])
						max_selected_oom_idx = i;
					else if (selected_oom_score_adj[i] == selected_oom_score_adj[max_selected_oom_idx] &&
						selected_tasksize[i] < selected_tasksize[max_selected_oom_idx])
						max_selected_oom_idx = i;
				}
			}

			lowmem_print(1, "select %d (%s), adj %d, \
					size %d, to kill\n",
				p->pid, p->comm, oom_score_adj, tasksize);
		}
	}
	for (i = 0; i < lowmem_enhance_threshold; i++) {
		if (selected[i]) {
			if (selected[i]->signal->oom_score_adj == 0) {
				have_selected += resident_task_kill();
				continue;
			}
			
			lowmem_print(1, "send sigkill to %d (%s), adj %d, size %d\n",
			     selected[i]->pid, selected[i]->comm,
			     selected_oom_score_adj[i], selected_tasksize[i]);
			#ifdef VENDOR_EDIT
 			//Added by Scott Huang for printing more info of lmk
			lowmem_print(1, "Killing '%s' (%d), adj %hd,\n" \
					"   to free %ldkB on behalf of '%s' (%d) because\n" \
					"   cache %ldkB is below limit %ldkB for oom_score_adj %hd\n" \
					"   Free memory is %ldkB above reserved\n",
			     	selected[i]->comm, selected[i]->pid,
			     	selected_oom_score_adj[i],
			    	 selected_tasksize[i] * (long)(PAGE_SIZE / 1024),
			     	current->comm, current->pid,
			     	other_file * (long)(PAGE_SIZE / 1024),
			     	minfree * (long)(PAGE_SIZE / 1024),
			     	min_score_adj,
			     	other_free * (long)(PAGE_SIZE / 1024));
			#endif			
			send_sig(SIGKILL, selected[i], 0);
			set_tsk_thread_flag(selected[i], TIF_MEMDIE);	
			rem -= selected_tasksize[i];
			have_selected++;
		}
	}
	rcu_read_unlock();
	if (have_selected) {
#ifdef VENDOR_EDIT
        //Lycan.Wang@Prd.BasicDrv, 2014-07-29 Add for workaround method for SHM memleak	
		if (totalram_pages / global_page_state(NR_SHMEM) < 10) {
			struct ipc_namespace *ns = current->nsproxy->ipc_ns;
			int backup_shm_rmid_forced = ns->shm_rmid_forced;

			lowmem_print(1, "Shmem too large (%ldKB)\n Try to release IPC shmem !\n", global_page_state(NR_SHMEM) * (long)(PAGE_SIZE / 1024));
			ns->shm_rmid_forced = 1;
			shm_destroy_orphaned(ns);
			ns->shm_rmid_forced = backup_shm_rmid_forced;
		}
#endif /* VENDOR_EDIT */
		lowmem_deathpending_timeout = jiffies + HZ;
		/* give the system time to free up the memory */
		msleep_interruptible(20);
	}
#else
		if (selected) {
			if (oom_score_adj < selected_oom_score_adj)
				continue;
			if (oom_score_adj == selected_oom_score_adj &&
			    tasksize <= selected_tasksize)
				continue;
		}
		selected = p;
		selected_tasksize = tasksize;
		selected_oom_score_adj = oom_score_adj;
		lowmem_print(2, "select '%s' (%d), adj %hd, size %d, to kill\n",
			     p->comm, p->pid, oom_score_adj, tasksize);
	}
	if (selected) {
		lowmem_print(1, "Killing '%s' (%d), adj %hd,\n" \
				"   to free %ldkB on behalf of '%s' (%d) because\n" \
				"   cache %ldkB is below limit %ldkB for oom_score_adj %hd\n" \
				"   Free memory is %ldkB above reserved\n",
			     selected->comm, selected->pid,
			     selected_oom_score_adj,
			     selected_tasksize * (long)(PAGE_SIZE / 1024),
			     current->comm, current->pid,
			     other_file * (long)(PAGE_SIZE / 1024),
			     minfree * (long)(PAGE_SIZE / 1024),
			     min_score_adj,
			     other_free * (long)(PAGE_SIZE / 1024));
		lowmem_deathpending_timeout = jiffies + HZ;
		send_sig(SIGKILL, selected, 0);
		set_tsk_thread_flag(selected, TIF_MEMDIE);
		rem -= selected_tasksize;
		rcu_read_unlock();
#ifdef VENDOR_EDIT
        //Lycan.Wang@Prd.BasicDrv, 2014-07-29 Add for workaround method for SHM memleak
        if (totalram_pages / global_page_state(NR_SHMEM) < 10) {
            struct ipc_namespace *ns = current->nsproxy->ipc_ns;
            int backup_shm_rmid_forced = ns->shm_rmid_forced;

            lowmem_print(1, "Shmem too large (%ldKB)\n Try to release IPC shmem !\n", global_page_state(NR_SHMEM) * (long)(PAGE_SIZE / 1024));
            ns->shm_rmid_forced = 1;
            shm_destroy_orphaned(ns);
            ns->shm_rmid_forced = backup_shm_rmid_forced;
        }
#endif /* VENDOR_EDIT */
		/* give the system time to free up the memory */
		msleep_interruptible(20);
	} else
		rcu_read_unlock();
#endif /* CONFIG_ENHANCED_LMK */	

	lowmem_print(4, "lowmem_shrink %lu, %x, return %d\n",
		     nr_to_scan, sc->gfp_mask, rem);

	mutex_unlock(&scan_mutex);
	
	return rem;
}

static struct shrinker lowmem_shrinker = {
	.shrink = lowmem_shrink,
	.seeks = DEFAULT_SEEKS * 16
};

static int __init lowmem_init(void)
{
#ifdef CONFIG_ENHANCED_LMK
//jiemin.zhu@oppo.com, 2014-10-23 Add for kill duplicate tasks
	unsigned int i;

	name_hash = (struct hlist_table*)kmalloc(sizeof(struct hlist_table) * MAX_PROCESS, 
							GFP_KERNEL);                 
	if (!name_hash) {
		printk("kmalloc hlist_table of name failed\n");
		return -ENOMEM;
	}
	for (i = 0; i < MAX_PROCESS; i++) {
		INIT_HLIST_HEAD(&(name_hash[i].head));
		name_hash[i].count = 0;
	}

	lowmemorykiller_wq = create_singlethread_workqueue("lowmemorykiller_wq");
	if (!lowmemorykiller_wq){
		return -ENOMEM;
	}
	
	INIT_WORK(&lmk_work, lmk_wq_func);
#endif /* CONFIG_ENHANCED_LMK */
	register_shrinker(&lowmem_shrinker);
	return 0;
}

static void __exit lowmem_exit(void)
{
#ifdef CONFIG_ENHANCED_LMK
//jiemin.zhu@oppo.com, 2014-10-23 Add for kill duplicate tasks
	int i;
	struct hlist_node *n;
	struct task_node *tn;
	struct hlist_head*head;

	if (lowmemorykiller_wq)
		destroy_workqueue(lowmemorykiller_wq);
		
	if (name_hash) {
		for (i = 0; i < MAX_PROCESS; i++) {
			head = &(name_hash[i].head);
			hlist_traversal(tn, n, head, node) {
				hlist_del(&(tn->node));
				kfree(tn);
			}
		}
		kfree(name_hash);
    }    
#endif /* CONFIG_ENHANCED_LMK */
	unregister_shrinker(&lowmem_shrinker);
}

#ifdef CONFIG_ANDROID_LOW_MEMORY_KILLER_AUTODETECT_OOM_ADJ_VALUES
static short lowmem_oom_adj_to_oom_score_adj(short oom_adj)
{
	if (oom_adj == OOM_ADJUST_MAX)
		return OOM_SCORE_ADJ_MAX;
	else
		return (oom_adj * OOM_SCORE_ADJ_MAX) / -OOM_DISABLE;
}

static void lowmem_autodetect_oom_adj_values(void)
{
	int i;
	short oom_adj;
	short oom_score_adj;
	int array_size = ARRAY_SIZE(lowmem_adj);

	if (lowmem_adj_size < array_size)
		array_size = lowmem_adj_size;

	if (array_size <= 0)
		return;

	oom_adj = lowmem_adj[array_size - 1];
	if (oom_adj > OOM_ADJUST_MAX)
		return;

	oom_score_adj = lowmem_oom_adj_to_oom_score_adj(oom_adj);
	if (oom_score_adj <= OOM_ADJUST_MAX)
		return;

	lowmem_print(1, "lowmem_shrink: convert oom_adj to oom_score_adj:\n");
	for (i = 0; i < array_size; i++) {
		oom_adj = lowmem_adj[i];
		oom_score_adj = lowmem_oom_adj_to_oom_score_adj(oom_adj);
		lowmem_adj[i] = oom_score_adj;
		lowmem_print(1, "oom_adj %d => oom_score_adj %d\n",
			     oom_adj, oom_score_adj);
	}
}

static int lowmem_adj_array_set(const char *val, const struct kernel_param *kp)
{
	int ret;

	ret = param_array_ops.set(val, kp);

	/* HACK: Autodetect oom_adj values in lowmem_adj array */
	lowmem_autodetect_oom_adj_values();

	return ret;
}

static int lowmem_adj_array_get(char *buffer, const struct kernel_param *kp)
{
	return param_array_ops.get(buffer, kp);
}

static void lowmem_adj_array_free(void *arg)
{
	param_array_ops.free(arg);
}

static struct kernel_param_ops lowmem_adj_array_ops = {
	.set = lowmem_adj_array_set,
	.get = lowmem_adj_array_get,
	.free = lowmem_adj_array_free,
};

static const struct kparam_array __param_arr_adj = {
	.max = ARRAY_SIZE(lowmem_adj),
	.num = &lowmem_adj_size,
	.ops = &param_ops_short,
	.elemsize = sizeof(lowmem_adj[0]),
	.elem = lowmem_adj,
};
#endif

module_param_named(cost, lowmem_shrinker.seeks, int, S_IRUGO | S_IWUSR);
#ifdef CONFIG_ANDROID_LOW_MEMORY_KILLER_AUTODETECT_OOM_ADJ_VALUES
__module_param_call(MODULE_PARAM_PREFIX, adj,
		    &lowmem_adj_array_ops,
		    .arr = &__param_arr_adj,
		    S_IRUGO | S_IWUSR, -1);
__MODULE_PARM_TYPE(adj, "array of short");
#else
module_param_array_named(adj, lowmem_adj, short, &lowmem_adj_size,
			 S_IRUGO | S_IWUSR);
#endif
module_param_array_named(minfree, lowmem_minfree, uint, &lowmem_minfree_size,
			 S_IRUGO | S_IWUSR);
module_param_named(debug_level, lowmem_debug_level, uint, S_IRUGO | S_IWUSR);
module_param_named(lmk_fast_run, lmk_fast_run, int, S_IRUGO | S_IWUSR);

module_init(lowmem_init);
module_exit(lowmem_exit);

MODULE_LICENSE("GPL");

