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

