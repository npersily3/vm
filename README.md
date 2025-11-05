## **By Noah Persily**

### **Summer 2025, Reach out to me with more questions at *nrpersily@gmail.com***

## **The Goal**

The goal of this program is to simulate the memory manager in the Windows OS. I reserve a large portion of virtual address space, but I do not back it with physical memory.  Then, in a loop, I simulate access to the user VA space. Because the space is not committed, pagefaults are generated. Once I get a page fault it is the program’s duty to map a page as quickly as possible. The core loop of the program is that pages are mapped to virtual space, then once we are out of pages, we unmap them and write their contents out to disk. Once that virtual address is reaccessed, the contents are read back in from the disk

## **Roadmap**

### **Single Threaded State Machine**

I started this journey by making a single threaded virtual memory manager. The main focus was to use the Windows APIs effectively and ti understand the moving parts of the state machine. The key primitives that were used in this implementation were page table entries (PTEs), page frame numbers (PFNs), linked lists, and disk metadata.

A PTE is a 64 bit code that corresponds to a page of virtual space. In the PTE 40 bits are dedicated to storing the physical frame number and the other 24 bits are mine to use. I use exactly 40 bits because the address bus line in a computer is only 52 bits wide, and since each PTE maps to a page, I do not need the lower 12 bits to get the offset within the page (252 / 212 \= 240). Of the 24 other bits, the most relevant bit is the valid bit which says whether the virtual space is backed by physical memory. In a real operating system that does not have to use Windows APIs, a page is mapped and unmapped by setting and clearing that bit. When the PTE is invalid, the 40 bits that hold a frame number store a disk index instead, so when that PTE is faulted on, the program knows where to go to get the contents of the virtual memory.

A PFN is a data structure that contains information about exactly one physical page of memory. At first, the struct had three fields, one for the actual frame number, the corresponding PTE, and a Windows ListEntry struct which is just two pointers. For my final single threaded design, I took out the frame number field because I realized I could use a sparse array to get the frame number in constant time.

	At the start, I had two lists of PFNs, an active list which represents pages with valid PTEs and a free list which contains free pages. An advantage of an active list is that it keeps the pages automatically sorted based on age, so it is always possible to find the oldest page in constant time.

	To simulate a disk, I allocated a large portion of memory. The diskfile region allocated has two pieces of metadata that inform the program where to look for free disk slots, a bytemap and a count for the number of free disk slots in a region. The bytemap keeps track of whether a slot is free for a page of data that needs to be written out to disk. The disk is split into arbitrarily-sized regions, and my counts inform the program of which region has the most free disk space. 

	With all these structures, we can create a program that manages page faults. When a virtual address is faulted on, it is translated into its PTE that should have a cleared valid bit. Then, the program checks the pagelists. First, the program checks the freelist to see if there is any physical memory not in use. Then it gets a page off of the active list. The contents of the page are written to disk and the victim PTE’s valid bit is cleared. The faulting PTE’s contents are then read onto the physical page and the valid bit is set.

### **Basic Multithreaded Machine**

Now that I was very familiar with the moving parts of this state machine, I had to figure out what parts to separate into their own threads. Initially, I only thought about having a user thread and a trimmer/writer thread, but performance traces showed that both writing and trimming were costly, so I decided to split them into their own threads.

A new challenge to tackle in multithreaded was PTEs in transition between threads. I needed to create two new page lists, modified and standby. The modified list contains pages that have been unmapped from their virtual addresses, but not yet written to disk. The standby list contains pages that have contents in both a disk slot and the physical page. Since the pages on these lists still have the contents of the virtual page on them, we can save ourselves from doing a disk read by mapping that specific frame if the previous virtual address was faulted on again. I have used the term rescue to describe this sequence of events. Additionally, I had to use one more of the 24 status bits to mark a PTE as transition, and one more bit in the PFN to encode which list the page was on (modified or standby). If a page is on stand-by that means it can be repurposed for a new PTE as long as the PTE that currently maps to the page has its transition bit cleared.

Another new consideration in the multithreaded world was a lock hierarchy. In this simple multithreaded state machine I only had three types of locks to consider, page table entry, list locks, and disk locks. Since I need to look at a page table entry to determine a list to go to, I thought that they should be at the top of my hierarchy. Next, I realized that disk locks are pretty self contained, so they should be the last lock I acquire.

This order made sense, but there were a few times I had to break it. In the case where I was repurposing a page off of the standby list, I first needed to look at the standby list then edit the PTE of the page at the head. I cannot lock the PTE first as I need to look at the standby list to determine the PTE, and I cannot lock them out of order because a deadlock could occur. In order to solve this problem, I need to try and acquire the PTE lock and if I cannot get it, I need to release the standby lock and redo the fault.

### **Complex Multithreaded State Machine**

To achieve better scalability, I implemented sophisticated locking techniques and optimizations across multiple areas.

#### **Fine-Grained Disk Management**

I replaced the single disk lock with per-slot atomic interlocked operations and switched from a byte map to a bitmap representation. This eliminated disk contention as a bottleneck and allowed multiple threads to allocate disk slots simultaneously.

#### **Advanced List Management with Page Locks**

To reduce list contention, particularly on the standby list, I implemented embedded locks within each page frame number (PFN) and added slim read-write locks to list head structures. This allows concurrent operations: removing from head, adding to tail, and removing from middle simultaneously. The page locks also serve as substitutes for page table entry locks when a PTE is linked to a PFN, reducing overall lock contention. A case where a PTE has to refault was eliminated.

#### **Batching Optimizations**

I implemented batching across all thread types to amortize system call costs. The trimmer removes multiple pages from the active list and unmaps them with a single function call. The writer pre-acquires multiple disk slots and performs batched disk operations. If there are not enough pages on the modified list, the program frees the extra disk slots.

User threads batch unmap operations on kernel virtual address space and batch transfers from standby to free lists and free lists to local caches.

#### **Multidimensioned Free Lists**

To eliminate rather than just move contention, I dimensioned the free list across multiple instances. Unlike the standby list where ordering matters for aging, the free list can be safely partitioned, allowing multiple user threads to satisfy faults without interfering with each other.

#### **Basic Aging Model**

I implemented a page aging system that exploits the non-random nature of real-world memory access patterns. Rather than assuming uniform access probability across all pages, the aging model maintains age-lists that track page access recency. Pages are organized into different age categories, and the trimmer preferentially selects older pages for eviction. This leverages temporal locality \- pages accessed recently are more likely to be accessed again soon, while pages that haven't been touched in a while are good candidates for trimming.

#### **Local Caches**

Each user thread maintains local caches of pages to minimize lock contention. With local caches, user threads only need to acquire PTE locks (which I consider inevitable for correctness) and can operate on their cached pages without additional synchronization overhead. This design dramatically reduces the frequency of expensive list operations. The trimmer intelligently targets pages from these local caches during memory pressure, as it's preferable to reclaim a page that hasn't been mapped to a virtual address rather than evicting an actively used page from the working set.

## **Current Project**

### **Scheduling**

Right now I am working on making my age and trim activations elastic. Based on the time it takes to write and trim a page, I want to find the goldilocks number of ptes to age and pages to trim.

State Machine as of October 23rd  
