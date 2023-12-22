Virtual Memory Manager.

## Features:
• Logical addresses (`addresses.txt` file) from `0` to `65 535`;
• Physical addresses from `0` to `32 768` match the size of the virtual address space;
• Page table size: `256` match the size of one page;
• TLB size: `16`;
• Frames number in physical memory: `128` with a single frame size of `256` bytes.

Demand paging is performed from the `BACKING_STORE.bin` file.
Physical memory pages replacement algorithm as well as TLB update are performed using the `LRU` caching policy.

## Getting started
```bash
git clone https://github.com/ghub-ayrtom/virtual-memory-manager.git
cd virtual-memory-manager

gcc main.c
./a.out addresses.txt > output.txt
vimdiff output.txt correct.txt
```
