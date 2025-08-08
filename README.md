# Multithreaded Virtual Memory Manager

## By Noah Persily
### Summer 2025, Reach out to me with more questions at *nrpersily@gmail.com*

---

## The Goal

--- 


The goal of this program is to simulate a virtual memory manager that exists in all modern operating systems. 
In order to mimic the experience of a user accessing places in memory, I reserve but not commit a large portion of virtual address space with the function VirtualAlloc.
Then in a loop the program tries to access parts of the reserved space. Since the space is not commited exceptions are generated. These exceptions are how I know there is a page fault. Once I get a page fault it is my duty to map a page as quickly as possible. 


## Roadmap

### Single Threaded State Machine

I started this journey by making single threaded virtual memory manager. The main focus was to use the Windows APIs effectively and understand the moving parts of the state machine. In this implementation the user would also serve the role of the system. If there was no page on the standby or free list it would trim an active page and write it to disk before eventually mapping it to the faulted on virtual address.

### Basic Multithreaded Machine 

Now that I was very familiar with the moving parts of this state machine, I had to figure out what parts to separate into their own threads. Initially, I only thought about having a user thread and a trimmer/writer thread, but performance traces showed that both writing and trimming were costly, so I decided to split them into their own threads. 
Another new consideration in the multithreaded was my lock hierarchy. 

