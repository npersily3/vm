# Multithreaded Virtual Memory Manager

## By Noah Persily
### Summer 2025, Reach out to me with more questions at *nrpersily@gmail.com*



## The Goal



The goal of this program is to simulate a virtual memory manager that exists in all modern operating systems. 
In order to mimic the experience of a user accessing places in memory, I reserve but not commit a large portion of virtual address space with the function VirtualAlloc.
Then in a loop the program tries to access parts of the reserved space. Since the space is not commited exceptions are generated. These exceptions are how I know there is a page fault. Once I get a page fault it is my duty to map a page as quickly as possible. 


## Roadmap

### Single Threaded State Machine

I started this journey by making single threaded virtual memory manager. The main focus was to use the Windows APIs effectively and understand the moving parts of the state machine. In this implementation the user would also serve the role of the system. If there was no page on the standby or free list it would trim an active page and write it to disk before eventually mapping it to the faulted on virtual address.

### Basic Multithreaded Machine 

Now that I was very familiar with the moving parts of this state machine, I had to figure out what parts to separate into their own threads.
Initially, I only thought about having a user thread and a trimmer/writer thread, but performance traces showed that both writing and trimming were costly, so I decided to split them into their own threads.

A new challenge to tackle in multithreaded was page table entries in transition between threads. I needed to create two new lists, modified and standby. The modified list had contain pages that have been unmapped from their virtual addresses, but not yet written to disk. The standby list contains pages that have contents in both a disk slot and a physical page.
Since the pages on these lists still have the contents of the virtual page on them, we can save ourselves from doing a disk read by mapping that specific frame if the previous virtual address was faulted on again. I have used the term rescue to describe this sequence of events. 

Another new consideration in the multithreaded world was a lock hierarchy. In this simple multithreaded state machine I only had three types of locks to consider, page table entry, list locks, and disk locks. 
Since I need to look at a page table entry to determine a list to go to, I thought that they should be at the top of my hierarchy. 
Next, I realized that disk locks are pretty self contained, so they should be the last lock I acquire.  
This order made sense, but there were a few times I had to break it. In the case where I was repurposing a page off of the standby list, I first need to look at the standby list then edit the pte of the page at the head.
I cannot lock the pte first as I need to look at the standby list to determine the pte, and I cannot lock them out of order because a deadlock could occur. In order to solve this problem, I need to try and acquire the pte lock and if I cannot get it, I need to release the standby lock and redo the fault. 

[//]: # (---)

[//]: # ()
[//]: # (![diagram 1]&#40;images/figure1vector.svg&#41;)

[//]: # (---)

### Complex Multithreaded State Machine

To achieve better scalability, I implemented sophisticated locking techniques and optimizations across multiple areas.

#### Fine-Grained Disk Management
I replaced the single disk lock with per-slot atomic interlocked operations and switched from a byte map to a bitmap representation. This eliminated disk contention as a bottleneck and allowed multiple threads to allocate disk slots simultaneously.

#### Advanced List Management with Page Locks
To reduce list contention, particularly on the standby list, I implemented embedded locks within each page frame number (PFN) and added slim read-write locks to list head structures. This allows concurrent operations: removing from head, adding to tail, and removing from middle simultaneously. The page locks also serve as substitutes for page table entry locks when a PTE is linked to a PFN, reducing overall lock contention.

#### Batching Optimizations
I implemented batching across all thread types to amortize system call costs. The trimmer removes multiple pages from the active list and unmaps them with a single "MapUserPhysicalPagesScatter" call. The writer pre-acquires multiple disk slots and performs batched disk operations. User threads batch unmap operations on kernel virtual address space and batch transfers from standby to free lists and free lists to local caches. 

#### Multidimensioned Free Lists
To eliminate rather than just move contention, I dimensioned the free list across multiple instances. Unlike the standby list where ordering matters for aging, the free list can be safely partitioned, allowing multiple user threads to satisfy faults without interfering with each other.

#### Basic Aging Model
I implemented a page aging system that exploits the non-random nature of real-world memory access patterns. Rather than assuming uniform access probability across all pages, the aging model maintains age-lists that track page access recency. Pages are organized into different age categories, and the trimmer preferentially selects older pages for eviction. This leverages temporal locality - pages accessed recently are more likely to be accessed again soon, while pages that haven't been touched in a while are good candidates for trimming.

#### Local Caches
Each user thread maintains local caches of pages to minimize lock contention. With local caches, user threads only need to acquire PTE locks (which I consider inevitable for correctness) and can operate on their cached pages without additional synchronization overhead. This design dramatically reduces the frequency of expensive list operations. The trimmer intelligently targets pages from these local caches during memory pressure, as it's preferable to reclaim a page that hasn't been mapped to a virtual address rather than evicting an actively used page from the working set.

[//]: # (![legend]&#40;images/legend.svg&#41;)

[//]: # ()
[//]: # ()
[//]: # ()
[//]: # (<br />)

[//]: # (<br />)

[//]: # (<br />)

[//]: # (<br />)

[//]: # ()
[//]: # ()
[//]: # (![diagram 2]&#40;images/figure2vector.svg&#41;)

## Current Project
### Scheduling

Right now I am working on making my age and trim activations elastic. Based on the time it takes to write and trim a page, I want to find the goldlocks number of ptes to age and pages to trim.    



State Machine as of October 23rd




