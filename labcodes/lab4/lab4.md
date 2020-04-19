# Lab 4实验报告
计76 张翔 2017011568
## 练习1

根据注释，`init`时需要初始化`proc_struct`的如下成员变量，其余置0即可：

+ `state <= PROC_UNINIT` 状态为未初始化
+ `pid <= -1`，此时未分配`pid`
+ `cr3 <= boot_cr3`，kernel初始化时页目录表的地址

`context`是当前内核进程的上下文，它保存了进程切换时可能被修改的寄存器，不同进程切换时需要使用以保存/恢复上下文，由于`uCore`的内核进程共享相同的内核地址空间，诸如段寄存器等无需保存。

`tf`是中断帧的指针，在中断帧中保存了中断/异常发生时进程被打断前的状态，在中断处理结束恢复现场时需要，用以恢复当前进程的上下文。

## 练习2

翻译注释即可，实现时需要注意以下几点：

+ 为新进程设置`pid`，加入hash表、链表时需要使用`local_instr_save`等关中断，以保证操作为原子操作
+ 根据不同的错误情况进行处理：
  + `alloc_proc`出错，表示无可用内存，跳转`fork_out`，返回`-E_NO_MEM`
  + `setup_kstack`出错，跳转`bad_fork_cleanup_proc`，将`kmalloc`得到的`proc_struct`释放
  + `copy_mm`出错，跳转`bad_fork_cleanup_kstack`，不仅需要释放内核栈，还需要`kfree`

新进程`pid`是唯一的，从`get_pid()`函数的代码容易看出它能够返回唯一的`pid`，而该函数调用时已关中断，保证原子操作，不会发生竞争，因此可以保证`pid`的唯一性。

## 练习3

对`proc_run`的分析如下：

```C
void proc_run(struct proc_struct *proc) {
    if (proc != current) { // switch if current is not proc to switch
        bool intr_flag;
        struct proc_struct *prev = current, *next = proc;
        local_intr_save(intr_flag); // disable interrupt --> atomic operation
        {
            current = proc; // change current process
            load_esp0(next->kstack + KSTACKSIZE);  // change esp0 --> start addr of stack of new process
            lcr3(next->cr3); // cr3 start addr of page dir --> that of the new process
            switch_to(&(prev->context), &(next->context)); // switch context
        }
        local_intr_restore(intr_flag); // restore interrupt
    }
}
```

其中`switch_to`使用汇编实现，如下

```assembly
.text
.globl switch_to
switch_to:                      # switch_to(from, to)
	##  Stack frame after called switch_to
	##  | to       8(esp) |
	##  | from     4(esp) |
	##  | ret_addr   esp  |
    # save from's registers
    movl 4(%esp), %eax          # eax points to from
    popl 0(%eax)                # save eip !popl
    movl %esp, 4(%eax)          # save esp::context of from
    movl %ebx, 8(%eax)          # save ebx::context of from
    movl %ecx, 12(%eax)         # save ecx::context of from
    movl %edx, 16(%eax)         # save edx::context of from
    movl %esi, 20(%eax)         # save esi::context of from
    movl %edi, 24(%eax)         # save edi::context of from
    movl %ebp, 28(%eax)         # save ebp::context of from

    # restore to's registers
    movl 4(%esp), %eax          # not 8(%esp): popped return address already
                                # eax now points to to
    movl 28(%eax), %ebp         # restore ebp::context of to
    movl 24(%eax), %edi         # restore edi::context of to
    movl 20(%eax), %esi         # restore esi::context of to
    movl 16(%eax), %edx         # restore edx::context of to
    movl 12(%eax), %ecx         # restore ecx::context of to
    movl 8(%eax), %ebx          # restore ebx::context of to
    movl 4(%eax), %esp          # restore esp::context of to

    pushl 0(%eax)               # push eip

    ret
```

这里的汇编将switch_to的返回地址写入from进程控制块的`eip`位置，这样切换回时能够恢复到相应位置；`ret`时，跳转到to进程控制块的`eip`的位置，而它实际指向`forkret`，从而`forkret`被调用，它将栈顶设置为to进程trap frame，然后使用`iret`返回，从而完成上下文的切换。

+ 本实验中，创建并运行了2个内核进程，其中一个为`idleproc`，`pid`为0，空闲时它会一直循环，如果找到需要运行的进程就进行调度执行；另一个为`initproc`，它会打印一段字符串。
+ `local_intr_save(intr_flag);....local_intr_restore(intr_flag);`语句能够关闭中断，保证被它保护的语句是原子操作。



## 总结

##### 本实验中重要的知识点，以及与对应的OS原理中的知识点

+ 进程状态模型

+ 内核线程的控制

+ 进程的上下文切换

##### 本实验中没有对应的

+ 用户进程