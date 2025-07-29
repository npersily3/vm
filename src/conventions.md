# Conventions

## Lock ordering 

1. PageTable locks
2. PFN locks
3. List locks
4. listhead pseudo page-locks
5. 
## Exceptions and What they Actually Protect

In get victim from standBy a pagelock temporarily protects the pte

## Lock Free

My disk slots are protected using bitmaps that are edited using interlocked operations