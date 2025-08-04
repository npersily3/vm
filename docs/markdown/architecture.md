# System Architecture {#architecture}

## Overview

The Virtual Memory Manager implements a sophisticated multi-layered architecture designed for high performance and scalability.

## Thread Architecture

### User Threads
- Handle page faults from application code
- Implement rescue logic for transition pages
- Manage per-thread transfer VA spaces
- Coordinate with system threads for memory pressure

### System Threads
- **Trimmer**: Moves pages from active to modified lists
- **Writer**: Performs batch disk I/O operations
- **Zero**: Maintains pool of zeroed pages (future enhancement)

## Memory Management Layers

### Virtual Address Management
- 1GB virtual address space (configurable)
- Page table with transition state tracking
- PTE regions for lock batching optimization

### Physical Memory Management
- PFN database with per-page metadata
- Multiple page lists (Free, Active, Modified, Standby)
- Lock-free length tracking for performance

### Backing Store Management
- Virtual disk with bitmap allocation
- Batch I/O operations for efficiency
- Automatic space reclamation

## Synchronization Design

### Lock Hierarchy
1. Page Table Region Locks (coarse-grained)
2. Page Lists Locks (medium-grained)
3. Individual Page Locks (fine-grained)

### Performance Optimizations
- Slim Reader-Writer Locks for list operations
- Lock-free atomic operations where possible
- Careful lock ordering to prevent deadlocks
