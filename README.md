## **By Noah Persily**

### **Summer/Fall 2025 — Reach out with questions at *nrpersily@gmail.com***

## **The Goal**

The goal of this program is to simulate the memory manager in the Windows OS. I reserve a large portion of virtual address space, but I do not back it with physical memory. Then, in a loop, I simulate access to the user VA space. Because the space is not committed, page faults are generated. Once I get a page fault it is the program's duty to map a page as quickly as possible. The core loop of the program is that pages are mapped to virtual space, then once we are out of pages, we unmap them and write their contents out to disk. Once that virtual address is re-accessed, the contents are read back in from the disk.

## **Roadmap**

### **Single Threaded State Machine**

I started this journey by making a single threaded virtual memory manager. The main focus was to use the Windows APIs effectively and to understand the moving parts of the state machine. The key primitives used in this implementation were page table entries (PTEs), page frame numbers (PFNs), linked lists, and disk metadata.

A PTE is a 64-bit code that corresponds to a page of virtual space. In the PTE, 40 bits are dedicated to storing the physical frame number and the other 24 bits are mine to use. I use exactly 40 bits because the address bus line in a computer is only 52 bits wide, and since each PTE maps to a page, I do not need the lower 12 bits to get the offset within the page (2^52 / 2^12 = 2^40). Of the 24 other bits, the most relevant is the valid bit, which says whether the virtual space is backed by physical memory. In a real operating system, a page is mapped and unmapped by setting and clearing that bit. When the PTE is invalid, the 40 bits that hold a frame number store a disk index instead, so when that PTE is faulted on, the program knows where to go to retrieve the contents of the virtual memory.

A PFN is a data structure that contains information about exactly one physical page of memory. At first, the struct had three fields: the actual frame number, the corresponding PTE, and a Windows ListEntry struct which is just two pointers. For my final single threaded design, I removed the frame number field because I realized I could use a sparse array to get the frame number in constant time.

At the start, I had two lists of PFNs: an active list which represents pages with valid PTEs and a free list which contains free pages. An advantage of an active list is that it keeps pages automatically sorted by age, so it is always possible to find the oldest page in constant time.

To simulate a disk, I allocated a large portion of memory. The disk region has two pieces of metadata that inform the program where to find free disk slots: a bytemap and a count for the number of free disk slots in a region. The bytemap tracks whether a slot is free for a page of data that needs to be written to disk. The disk is split into arbitrarily-sized regions, and the counts inform the program which region has the most free disk space.

With all these structures, we can create a program that manages page faults. When a virtual address is faulted on, it is translated into its PTE which should have a cleared valid bit. Then, the program checks the page lists. First, the program checks the free list to see if there is any physical memory not in use. Then it gets a page off of the active list. The contents of the page are written to disk and the victim PTE's valid bit is cleared. The faulting PTE's contents are then read onto the physical page and the valid bit is set.

### **Basic Multithreaded Machine**

Now that I was very familiar with the moving parts of this state machine, I had to figure out what parts to separate into their own threads. Initially, I only thought about having a user thread and a trimmer/writer thread, but performance traces showed that both writing and trimming were costly, so I decided to split them into their own threads.

A new challenge in the multithreaded world was PTEs in transition between threads. I needed to create two new page lists, modified and standby. The modified list contains pages that have been unmapped from their virtual addresses, but not yet written to disk. The standby list contains pages that have contents in both a disk slot and the physical page. Since the pages on these lists still hold the contents of the virtual page, we can save ourselves from doing a disk read by mapping that specific frame if the previous virtual address is faulted on again. I use the term "rescue" to describe this sequence of events. Additionally, I had to use one more of the 24 status bits to mark a PTE as in-transition, and one more bit in the PFN to encode which list the page was on (modified or standby). If a page is on standby, it can be repurposed for a new PTE as long as the PTE that currently maps to the page has its transition bit cleared.

Another new consideration was a lock hierarchy. In this simple multithreaded state machine I only had three types of locks: page table entry locks, list locks, and disk locks. Since I need to look at a PTE to determine which list to go to, PTEs sit at the top of my hierarchy. Disk locks are self-contained, so they are acquired last.

This order made sense, but there were a few cases where I had to break it. When repurposing a page off the standby list, I first need to look at the standby list and then edit the PTE of the page at the head. I cannot lock the PTE first because I need to examine the standby list to determine which PTE to target, and I cannot lock them out of order because that could cause a deadlock. To solve this, I try to acquire the PTE lock and if I cannot, I release the standby lock and redo the fault.

### **Complex Multithreaded State Machine**

To achieve better scalability, I implemented sophisticated locking techniques and optimizations across multiple areas.

#### **Fine-Grained Disk Management**

I replaced the single disk lock with per-slot atomic interlocked operations and switched from a bytemap to a bitmap representation. This eliminated disk contention as a bottleneck and allowed multiple threads to allocate disk slots simultaneously.

#### **Advanced List Management with Page Locks**

To reduce list contention, particularly on the standby list, I implemented embedded locks within each PFN and added slim read-write locks to list head structures. This allows concurrent operations — removing from head, adding to tail, and removing from the middle — simultaneously. The page locks also serve as substitutes for PTE locks when a PTE is linked to a PFN, reducing overall lock contention.

#### **Batching Optimizations**

I implemented batching across all thread types to amortize system call costs. The trimmer removes multiple pages from the active list and unmaps them with a single function call. The writer pre-acquires multiple disk slots and performs batched disk operations. If there are not enough pages on the modified list, the program frees the extra disk slots.

User threads batch unmap operations on kernel virtual address space and batch transfers from standby to free lists and from free lists to local caches.

#### **Multidimensioned Free Lists**

To eliminate rather than just move contention, I partitioned the free list across multiple instances. Unlike the standby list where ordering matters for aging, the free list can be safely split, allowing multiple user threads to satisfy faults without interfering with each other.

#### **Complex Aging Model and PTE Regions**

I implemented a multi-level page aging system that exploits the non-random nature of real-world memory access patterns. PTEs are grouped into regions, and regions are placed on one of several age lists based on how recently their entries have been accessed. The ager scans PTEs, checks a hardware-style access bit, and promotes or demotes regions between age buckets accordingly. The trimmer preferentially evicts pages from the oldest regions, leveraging temporal locality: pages accessed recently are likely to be accessed again soon, while untouched pages are good eviction candidates.

Grouping PTEs into regions rather than tracking individual PTEs amortizes the overhead of aging: one region lock covers many PTEs, and the trimmer can skip entire regions with no active entries.

#### **Local Caches**

Each user thread maintains local caches of pages to minimize lock contention. With local caches, user threads only need to acquire PTE locks (which are unavoidable for correctness) and can operate on cached pages without additional synchronization overhead. This dramatically reduces the frequency of expensive list operations. The trimmer intelligently targets pages from local caches during memory pressure, as it is preferable to reclaim a page that has not yet been mapped to a virtual address rather than evicting an actively used page from the working set.

#### **Adaptive Scheduler Thread**

I added a dedicated scheduler thread that wakes up periodically and decides how much work to dispatch to the ager, trimmer, and writer threads each cycle. Rather than waking these threads with fixed work targets, the scheduler observes each thread's recent throughput (pages processed per unit time) and the current rate of page consumption across user threads.

The scheduler solves a timing problem: aging must complete before trimming can produce pages, and trimming must complete before writing can recycle disk slots. Given the measured rates and an estimate of how long until the system runs out of available pages, the scheduler computes the minimum amount of aging needed this cycle and dispatches just that much — avoiding both under-aging (which stalls the trimmer) and over-aging (wasted CPU). A short calibration phase at startup collects baseline throughput data before the adaptive logic engages.

## **Current Focus: Optimizations**

Feature development is on hold. The current goal is to squeeze as much performance out of the existing state machine as possible before adding anything new. The areas of interest are:

**Reducing syscall overhead** — Event signaling and kernel transitions are expensive. Work is ongoing to ensure that `SetEvent` calls in the hot fault path fire only when necessary. For example, the trimmer wakeup signal from the fault handler is now gated behind an atomic flag so that only one thread pays the kernel-entry cost per trimmer cycle, regardless of how many threads are simultaneously above the memory pressure threshold.

**Reducing contention** — The lock hierarchy and batching strategies already help, but there are still hot spots in the standby and modified lists under high thread counts. The goal is to identify these through profiling and either partition them further or replace them with lock-free structures where feasible.

**Improving scheduler accuracy** — The adaptive scheduler's throughput estimates are rolling averages and can lag behind sudden changes in workload. Tightening the feedback loop so that the scheduler reacts faster to spikes in page consumption is an active area of work.

**Access pattern improvements** — The user thread's access pattern drives everything else in the system. Tuning the pattern to better stress the aging and eviction logic will help surface bottlenecks that are currently hidden.

State Machine as of May 2026
