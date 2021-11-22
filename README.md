# Memory allocator contest

This repo is an evaluation platform for memory allocators in C++. 
We comparing self-made allocators with default `malloc`/`free` and participants.

## Scores computation

To compare allocators we compute and sum scores for several tests: small, medium, large, random.
The more the total sum - the better.

The score for each test is inversely proportional to the computation time and memory overhead.


## Running the tests

The allocator test environment works in Windows, VS 2019.
You need at least 16 Gb of RAM, and 80 Gb of swap disks.

## Testing your own allocator

To develop and test your allocator, do the following:

1. Use `DefaultMallocAllocator` at `MemoryAllocatorContest.cpp` as a scratch.

2. Add your class to the list of allocators to test at the beginning of the `main()`:
`results.push_back(TestCase_MemoryPerformance<...>::RunTests());`

(Of course it's better to comment others allocator testing during your development.)

3. Run. The program outputs total scores and forms `results.html` with graphs of time and memory consumption.

## Results

The results are in folder `MemoryAllocatorResults`, especially `Results.txt` file.

## Allocators implementations discussion

* AlexeiMikhailov: uses pools 2^n and lists. 

* OlegApanasik: uses 2^n multi-pools with free-list and without any loop.

* DefaultMallocAlloc: default `malloc`/`free` allocator. Slow but very effective in memory.

* DaniilPavlenko: uses single multiset to store all free memory chunks.

* AlexeyAntropov: .   
 
* DenisPerevalov: uses several 2^n pools without reusing. Parameters (`table[]`) are tweaked specially to maximize the scores.
        
* AntonShatalov: uses binary-coded 6-tree with cache-usage optimizations. Extremely fast on Medium test.        
