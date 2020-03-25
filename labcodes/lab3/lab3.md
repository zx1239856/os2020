# Lab 3实验报告

计76 张翔 2017011568

## 练习1

实现方法：先用`get_pte`检查页表中是否存在该虚拟地址，不存在则使用`pgdir_alloc_page`分配一个页即可

+ 页表/页目录项中对实现替换算法的用途

  | 位   | 名称 | 含义             | 用途                                                         |
  | ---- | ---- | ---------------- | ------------------------------------------------------------ |
  | 0    | P    | 页面是否在内存中 | 指示页面的存在信息，如果不存在，则其他位可以用来存放swap后该页在磁盘中的位置等信息 |
  | 5    | A    | 页面是否被访问过 | 可用于(Extended) CLOCK替换算法的实现                         |
  | 6    | D    | 该页是否被写过   | 可用于Extended CLOCK替换算法的实现（优先替换D=A=0的页）      |

  

+ 如果 ucore 的缺页服务例程在执行过程中访问内存，出现了页访问异常，请问硬件要做哪些事情？

  执行一般异常的硬件流程：在当前内核栈保存当前被打断的程序现场，依次压入`EFLAGS，CS，EIP，errorCode`(`errorCode`保存页访问异常的类型)；通过`IDT`查找中断服务例程的地址，并加载到`CS`和`EIP`寄存器中，开始执行中断服务例程

  除此之外，CPU还会将引发页访问异常的线性地址保存到`CR2`寄存器中，以供处理程序使用。

## 练习2

实现：翻译注释即可，将需要的页使用`swap_in`换入；FIFO算法通过环形双向链表维护一个queue，根据需要从指定位置插入/删除即可。

如果要在 ucore 上实现"extended clock 页替换算法"请给你的设计方案，现有的 swap_manager 框架是否足以支持在 ucore 中实现此算法？

答案是肯定的，见Challenge 1的实现。

+ 需要被换出的页的特征是什么？

  需要被换出的页`PTE_A,PTE_D`均为0，直接在链表中顺序查找即可

+ 在 ucore 中如何判断具有这样特征的页？

  使用位运算取出PTE中相应位即可判断，如`*pte & PTE_A`取出A位，`*pte & PTE_D`取出D位

+ 何时进行换入和换出操作？

  + 换入：发生page fault后，处理函数最终调用`do_pagefault`，如果虚拟地址对应的PTE为swap entry（`*ptep != 0`）且`swap_init`正常，则可以将PTE对应的页面从SWAP换入
  + 换出：在`alloc_page`函数中，如果`pmm_manager`无法`alloc_pages`，且需要分配的页面数为1、`swap_init`正常，此时则可以进行页换出的操作。

## Challenge 1

**注：使用`make DEFS=-DUSE_EXCLOCK_SWAP`可以在编译时开启Extended CLOCK算法**。

### 算法设计

算法使用课上介绍的算法，源代码在`kern/mm/swap_exclock.c`中。由于框架实现了统一的`swap_manager`，只需要将`swap_out_victim`, `map_swappable`改成实现Extended CLOCK算法的函数即可。

注意到课上介绍的实现方式是使用新的页替换victim页，如果使用链表实现，需要记住被swap的page在链表中的原来位置，从而在换入新page时可以插入到正确的位置。因此，维护一个CLOCK指针，大致按如下伪代码实现即可

```pseudocode
global clock_ptr;
def init():
	clock_ptr = list_head;

def map_swappable(page):
	add_after(clock_ptr, page);
	// the list sequence: most recently added --> least recently added

def swap_out_victim():
	victim = find_victim(clock_ptr);
	// now clock points to victim
	clock_ptr = clock_ptr->before;
```

按上述代码，初始时内存中可用页没有被完全分配，`clock_ptr == list_head`成立，使得页面在链表中的顺序同FIFO；完全分配后，发生page fault，此时选取victim，并将`clock_ptr`置于比victim晚插入的元素(在链表中是before)，从而换入页插入时，在`clock_ptr`之后插入的位置就是前面被换出页面的位置。

在查找Victim时，根据A位和D位，共4种情况：

1. A=0, D=0 : 直接替换该页
2. A=0, D=1 : 将该页写回SWAP（使用`swapfs_write`），然后将D清零，继续遍历
3. A=1 : 将A清零，继续遍历（同标准的CLOCK算法）

### 测试

使用课上的课件中关于Extended CLOCK算法的测例作为测试，在结束时应缺页7次，且内存中的页为`a,d,e,c`。测试代码在`kern/mm/swap_exclock.c`的100-146行。

### 测试结果

（每次寻找Victim时输出了操作前后链表中的元素，使用`-DEXCLOCK_VERBOSE`编译选项开启）

```
set up init env for check_swap begin!
page fault at 0x00001000: K/W [no page found].
page fault at 0x00002000: K/W [no page found].
page fault at 0x00003000: K/W [no page found].
page fault at 0x00004000: K/W [no page found].
set up init env for check_swap over!
read Virt Page c in exclock_check_swap
write Virt Page a in exclock_check_swap
read Virt Page d in exclock_check_swap
write Virt Page b in exclock_check_swap
read Virt Page e in exclock_check_swap
page fault at 0x00005000: K/R [no page found].
Page: 0x1000 (A:1, D:1)--> Page: 0x2000 (A:1, D:1)--> Page: 0x3000 (A:1, D:0)--> Page: 0x4000 (A:1, D:0)--> end
CLOCK Ptr ==> 0x1000 (A:1, D:1)
After operation: 
Page: 0x1000 (A:0, D:0)--> Page: 0x2000 (A:0, D:0)--> Page: 0x4000 (A:0, D:0)--> end
 CLOCK Ptr ==> 0x4000 (A:0, D:0)
swap_out: i 0, store page in vaddr 0x3000 to disk swap entry 4
read Virt Page b in exclock_check_swap
write Virt Page a in exclock_check_swap
read Virt Page b in exclock_check_swap
read Virt Page c in exclock_check_swap
page fault at 0x00003000: K/R [no page found].
Page: 0x1000 (A:1, D:1)--> Page: 0x2000 (A:1, D:0)--> Page: 0x5000 (A:1, D:0)--> Page: 0x4000 (A:0, D:0)--> end
CLOCK Ptr ==> 0x4000 (A:0, D:0)
After operation: 
Page: 0x1000 (A:1, D:1)--> Page: 0x2000 (A:1, D:0)--> Page: 0x5000 (A:1, D:0)--> end
 CLOCK Ptr ==> 0x1000 (A:1, D:1)
swap_out: i 0, store page in vaddr 0x4000 to disk swap entry 5
swap_in: load disk swap entry 4 with swap_page in vadr 0x3000
read Virt Page d in exclock_check_swap
page fault at 0x00004000: K/R [no page found].
Page: 0x3000 (A:1, D:0)--> Page: 0x1000 (A:1, D:1)--> Page: 0x2000 (A:1, D:0)--> Page: 0x5000 (A:1, D:0)--> end
CLOCK Ptr ==> 0x1000 (A:1, D:1)
After operation: 
Page: 0x3000 (A:0, D:0)--> Page: 0x1000 (A:0, D:0)--> Page: 0x5000 (A:0, D:0)--> end
 CLOCK Ptr ==> 0x5000 (A:0, D:0)
swap_out: i 0, store page in vaddr 0x2000 to disk swap entry 3
swap_in: load disk swap entry 5 with swap_page in vadr 0x4000
read Virt Page e in exclock_check_swap
read Virt Page a in exclock_check_swap
count is 0, total is 7
check_swap() succeeded!
```

与课件比较，可知结果是正确的。

## 总结

#### 本实验中重要的知识点，以及与对应的OS原理中的知识点

+ FIFO算法
+ Extended CLOCK算法
+ 缺页异常的处理

#### 本实验没有对应的知识点

+ Belady现象
+ 抖动和负载控制
+ 全局页面替换
+ OPT算法

