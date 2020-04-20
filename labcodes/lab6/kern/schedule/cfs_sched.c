#include <defs.h>
#include <list.h>
#include <proc.h>
#include <assert.h>
#include <cfs_sched.h>

#define MAX_PRIO 19
#define UINT_MAX 0xffffffff
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

static uint32_t tick = 0;

static const int prio_to_weight[20] = {
    /*   0 */1024,  820,    655,    526,    423,
    /*   5 */335,   272,    215,    172,    137,
    /*  10 */110,   87,     70,     56,     45,
    /*  15 */36,    29,     23,     18,     15,
};

static inline int get_delta_time(struct proc_struct *proc) {
    if(proc->last_sched_tick <= tick) {
        return tick - proc->last_sched_tick;
    } else {
        return ((uint32_t)UINT_MAX - proc->last_sched_tick) + 1 + tick;
    }
}

static int
proc_cfs_comp_f(void *a, void *b)
{
     struct proc_struct *p = le2proc(a, lab6_run_pool);
     struct proc_struct *q = le2proc(b, lab6_run_pool);
     int32_t c = p->lab6_stride - q->lab6_stride;
     if (c > 0) return 1;
     else if (c == 0) return 0;
     else return -1;
}

static void
cfs_init(struct run_queue *rq) {
     rq->lab6_run_pool = NULL;
     rq->proc_num = 0;
}

static struct proc_struct *
cfs_pick_next(struct run_queue *rq) {
     if(rq->lab6_run_pool == NULL) return NULL;
     struct proc_struct *proc = le2proc(rq->lab6_run_pool, lab6_run_pool);
     return proc;
}

static void
cfs_enqueue(struct run_queue *rq, struct proc_struct *proc) {
     if(proc->sched_flag & CFS_INIT) {
         // is a new process
         if(proc->parent != NULL) {
             proc->lab6_stride = proc->parent->lab6_stride;
         }
         proc->lab6_stride -= MAX_TIME_SLICE;
         proc->sched_flag &= ~CFS_WAKE_UP;
     }
     else {
         // similar to `sched_vslide` in Linux
         proc->lab6_stride += 1024 * get_delta_time(proc) / prio_to_weight[MAX_PRIO - MIN(proc->lab6_priority, MAX_PRIO)];
         // proc->lab6_stride += 60 * 5 / (proc->lab6_priority > 0 ? proc->lab6_priority : 1);
     }
     if(proc->sched_flag & CFS_WAKE_UP) {
         struct proc_struct *min_proc = cfs_pick_next(rq);
         if(min_proc != NULL) {
            uint32_t vruntime = min_proc->lab6_stride;
            vruntime -= MAX_TIME_SLICE;
            proc->lab6_stride = MAX(proc->lab6_stride, vruntime);
         }
     }
     // remember to clear flag
     proc->sched_flag = 0;
     rq->lab6_run_pool = skew_heap_insert(rq->lab6_run_pool, &(proc->lab6_run_pool), proc_cfs_comp_f);
     if (proc->time_slice == 0 || proc->time_slice > rq->max_time_slice)
     {
          proc->time_slice = rq->max_time_slice;
     }
     proc->rq = rq;
     rq->proc_num++;
}

static void
cfs_dequeue(struct run_queue *rq, struct proc_struct *proc) {
     rq->lab6_run_pool = skew_heap_remove(rq->lab6_run_pool, &(proc->lab6_run_pool), proc_cfs_comp_f);
     rq->proc_num--;
     proc->last_sched_tick = tick;
}

static void
cfs_proc_tick(struct run_queue *rq, struct proc_struct *proc) {
     tick += 1;
     if(proc->time_slice > 0) {
         proc->time_slice--;
     }
     if(proc->time_slice == 0)
          proc->need_resched = 1;
}

struct sched_class cfs_sched_class = {
     .name = "cfs_scheduler",
     .init = cfs_init,
     .enqueue = cfs_enqueue,
     .dequeue = cfs_dequeue,
     .pick_next = cfs_pick_next,
     .proc_tick = cfs_proc_tick,
};