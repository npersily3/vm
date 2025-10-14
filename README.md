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

---

![diagram 1](images/figure1vector.svg)
---

### Complex Multithreaded State Machine

In order to improve my scalability, I needed to get smarter. I had to implement more sophisticated locking techniques among other strategies.

The first place I released contention from was the disk. Instead of having one big lock around the whole disk, I created a lock per slot. Then, I started using atomic interlocked operations to lock and unlock disk slots. Additionally, I switched my disk to a bitmap instead of a byte map. 

Next, I started to look for a way to alleviate list contention, specifically on the standby list. Ideally, I wanted to be able to remove from the head, add to the tail, and remove from the middle simultaneously. 
The way I thought to do this was to add locks on individual pages. Now, I could first try to lock all the pages I needed to edit, before grabbing the list lock exclusive and shutting everyone out. To implement this, I embedded a lock in my pfn and added a slim read-write lock to my listhead structure. 

The addition of pagelocks also helped me reduce my pagetable lock contention. In scenarios, where a pte was linked to a pfn I could use the page's lock as a stand-in for a pagetable lock. 
Both my writes and my victimization of standby pages could now be done with only the page-lock. The only caveat was that in my rescue I had to check if the pte changed after I acquire the pagelock.

Another key strategy I implemented was batching. I was able to implement batching in all three types of threads.

In the trimmer, I now remove multiple pages from the active list and unmap them all simultaneously with a call to the function "map user physical pages scatter". 

In the writer, I now preacquire multiple disk slots, instead of searching the disk individually. Moreover, during the actual process to write to disk, I could only do one map and unmap call per batch.

The user thread used batching two places. First, I started to batch unmap my kernel virtual address space that I use to read disk contents into physical pages. Then, I started to batch remove pages from the standby list and place them onto the freelist in order to alleviate standby list contention.

To actually fix the contention instead of moving it from the standby to freelist was to dimension my freelist. I could not do this with my standby list since the order in which they were added matters, but in the freelist it does not. In a multidimensioned free list, multiple users could be satisfying faults without interruption themselves

![legend](images/legend.svg)



<br />
<br />
<br />
<br />


![diagram 2](images/figure2vector.svg)

State Machine as of August 1st




