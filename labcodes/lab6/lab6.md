# Lab 6实验报告

计76 张翔 2017011568

## 练习0

实验中我发现ucore在代码段增多到一定数量后，`check_pgfault`在`0x100`处无法触发缺页异常，使得后面会尝试`free`不存在的页面，导致kernel无法正常启动。如果将`kernel.ld`按如下方式改动，即可解决这个问题

```diff
--- a/labcodes/lab6/tools/kernel.ld
+++ b/labcodes/lab6/tools/kernel.ld
@@ -37,7 +37,7 @@ SECTIONS {
     }
 
     /* Adjust the address for the data segment to the next page */
-    . = ALIGN(0x1000);
+    . = ALIGN(0x80000);
 
     /* The data segment */
     .data : {
```



## 练习1

- 请理解并分析 sched_class 中各个函数指针的用法，并结合 Round Robin 调度算法描述 ucore 的调度执行过程

  `sched_class`如下

  ```c++
  struct sched_class {
      const char *name;
      void (*init)(struct run_queue *rq);
      void (*enqueue)(struct run_queue *rq, struct proc_struct *proc);
      void (*dequeue)(struct run_queue *rq, struct proc_struct *proc);
      struct proc_struct *(*pick_next)(struct run_queue *rq);
      void (*proc_tick)(struct run_queue *rq, struct proc_struct *proc);
  };
  ```

  + `name`: 调度器的名字
  + `init`: 初始化调度器需要的数据结构，如`run_queue`，以及进程数量计数器
  + `enqueue`: 将进程插入`run_queue`，然后增加计数器
  + `dequeue`: 从`run_queue`中取出指定进程，计数器-=1
  + `pick_next`: 从`run_queue`中选取下一个可执行的进程
  + `proc_tick`: 时钟中断发生时被调用的处理函数，可以维护基于时间片调度算法需要的一些属性

  ucore的RR算法调度过程（假设当前运行的进程剩余时间片为5）：

  1. 前4次clock tick，每次剩余时间片-1
  2. 最后1次clock tick，时间片减到0，于是将`need_resched`设为1，这种情况下在`trap`函数中会调用`schedule()`，从而开始调度（或者进程主动放弃CPU，进行`schedule()`）
  3. 如果当前进程仍然Runnable，调用`enqueue`将当前进程放入调度队列
  4. 调用`dequeue`从调度队列中取一个进程，如果为`NULL`，则`next`为`idleproc`，使用`proc_run`运行之

  由于每次`enqueue`时，进程的剩余时间片恢复为最大可用时间片，而算法使用的数据结构是FIFO队列，实际运行效果是时间片的Round Robin。

- 请在实验报告中简要说明如何设计实现”多级反馈队列调度算法“，给出概要设计，鼓励给出详细设计

  进程初始化时，需要将优先级设置为最高。

  调度器的相应函数设计如下：

  `init`: 对每个优先级初始化对应的队列；

  `enqueue`: 如果当前进程时间片为0（用完了给定的时间片），降低优先级，放入低一级的队列中；如果时间片>0，优先级不变，放入同级队列中；

  `dequeue`: 取进程时优先从高优先级队列取出即可；

  `pick_next`: 如果当前进程在队列中不是最后一个元素，直接取出即可；否则到更低优先级的队列中寻找后一个元素；

  `proc_tick`: 这里最好维护一个全局变量用于记录proc_tick被调用的次数，当累计到一定次数后，将所有进程都移动到高优先级队列中去，缓解持续的饥饿现象。

## 练习2

原调度器在时钟中断时的`proc_tick`处理无需改变，但其他部分需要做如下调整:

1. `init`时需要初始化stride调度器需要的数据结构，这里使用斜堆，置`run_pool`为空即可
2. 修改`enqueue`与`dequeue`的操作，调用斜堆的插入、删除接口，使用`proc_stride_comp_f`作为比较函数；对`proc_num`的维护不变；
3. `pick_next`时，取堆顶元素(`stride`最小)，并根据`priority`计算`stride`值
4. 由于`stride`值使用`uint32_t`，`BIG_STRIDE`应设置为`INT_MAX`，即`0x7fffffff`，因为`max_stride - min_stride <= PASS_MAX <= BIG_STRIDE`，需要保证差值不超过32位有符号整数的范围

## Challenge: 简化版本的CFS算法

这里参照了Linux的CFS，在ucore上实现了一个简化版本的CFS调度算法。CFS的基本思路是尽量平衡每个进程的运行时间，它定义了通过优先级进行归一化的`vruntime`，每次选择`vruntime`最小的进程进行调度。

Linux的CFS使用红黑树来对进程按`vruntime`排序，这里为了简化，仍然使用`skew_heap`，在最坏情况下`skew_heap`可退化成有序链表，在实际操作系统中可能会影响性能，不过在ucore中问题不大。

首先需要修改`proc_struct`，增加成员变量

```C++
struct proc_struct {
	...
	uint32_t sched_flag;
    uint32_t last_sched_tick;
}
```

注意`vruntime`使用现有的`lab6_stride`成员变量即可，而Stride算法的`stride`和`run_pool`可以被复用。

这里的`sched_flag`是仿造Linux内核的`static void enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int flags)`，它使用`flags`中的一些位来判断进程enqueue时是否是刚被唤醒。如果一个进程`sleep`了较长时间，它的`vruntime`可能比其他进程都要小很多，容易造成饥饿现象，此时enqueue时需要重新设置它的`vruntime`。`last_sched_tick`是进程上次被调度运行时的`tick`值，等效于Linux内核`struct sched_entity`中定义的`exec_start`，当进程下次enqueue时可以计算间隔时间，从而得到相应的`vruntime`。

同时需要在`wakeup_proc`处增加如下代码：

```C++
proc->sched_flag |= CFS_WAKE_UP;
```

`do_fork`处需要增加

```C++
proc->sched_flag |= CFS_INIT;
```



调度器本身的实现如下：

注意到`vruntime`也可能溢出，但类似于Stride算法的处理即可，因为`vruntime`每次增加量不超过`INT_MAX`。

在我的设定中，ucore的CFS调度器支持20个优先级（对应Linux内核中`nice`值`0-19`）

```C++
static const int prio_to_weight[20] = {
    /*   0 */1024,  820,    655,    526,    423,
    /*   5 */335,   272,    215,    172,    137,
    /*  10 */110,   87,     70,     56,     45,
    /*  15 */36,    29,     23,     18,     15,
};
```

`proc_tick`只需要处理进程的时间片

```C++
static void
cfs_proc_tick(struct run_queue *rq, struct proc_struct *proc) {
     tick += 1;
     if(proc->time_slice > 0) {
         proc->time_slice--;
     }
     if(proc->time_slice == 0)
          proc->need_resched = 1;
}
```

`pick_next, dequeue`操作只与相应数据结构有关，无需其他操作（相比Stride算法需要`pick_next`时计算stride值），但`enqueue`时需要有如下操作

```C++
static void cfs_enqueue(struct run_queue *rq, struct proc_struct *proc) {
     if(proc->sched_flag & CFS_INIT) {
         // is a new process
         if(proc->parent != NULL) {
             proc->lab6_stride = proc->parent->lab6_stride;
         }
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
            vruntime -= proc->rq->max_time_slice;
            proc->lab6_stride = MAX(proc->lab6_stride, vruntime);
         }
     }
     proc->sched_flag = 0;
     ... // insert to skew heap and proc_num++, which are same as STRIDE
}
```

其中对sleep后被唤醒进程的操作`proc->lab6_stride = MAX(proc->lab6_stride, vruntime);`是为了防止短期sleep进程的`vruntime`被补偿。

#### 测试

使用`make DEFS=-DUSE_CFS_SCHED`可以开启CFS调度器，使用`priority`可以测试它的调度，输出如下：

```
kernel_execve: pid = 2, name = "priority".
main: fork ok,now need to wait pids.
child pid 6, acc 1832000, time 1001
child pid 5, acc 1480000, time 1001
child pid 7, acc 2340000, time 1001
child pid 3, acc 940000, time 1002
child pid 4, acc 1212000, time 1002
main: pid 3, acc 940000, time 1002
main: pid 4, acc 1212000, time 1002
main: pid 5, acc 1480000, time 1003
main: pid 6, acc 1832000, time 1003
main: pid 7, acc 2340000, time 1003
main: wait pids over
stride sched correct result: 1 1 2 2 2
all user-mode processes have quit.
```

`priority`是CPU密集型程序，它每次均能用完分配的时间片。注意`priority`的`stride sched correct result`输出为`1 2 3 4 5`时要求调度器分配给相应进程的时间片长度与优先级呈正比（如Stride算法，根据实验指导书，“该调度方案为每个进程分配的时间将与其优先级成正比”）。不过可以通过输出的`acc`值，求出相邻优先级进程的比值，可以得到

```
1.2905    1.2251    1.2362    1.2739
```

取平均得到1.26。而注意CFS算法给对应优先级分配的时间片长度在`prio_to_weight`中有所体现，该表是由
$$
1024\times(1.25)^{nice}
$$
得到的，由此容易得到相邻优先级的时间片长度比值为1.25，说明程序得到的结果是正确的。



## 总结

##### 重要的知识点

+ Round Robin调度算法
+ 进程调度
+ 单处理机调度

#### 没有对应的知识点

+ 多处理机调度
+ 优先级反置
+ 实时调度
+ O(1), BFS等调度算法

