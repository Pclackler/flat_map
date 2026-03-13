
## milo::FlatMap
A deterministic Flat , open addressed hash map for low latency C++ systems designed for a smoother latency distribution.
milo::FlatMap was designed to allow me to sketch systems quickly with verbose access to resources while not introducing wild tail latencies.




While `std::unordered_map` is great for general-purpose computing, it suffers from a poor latency distribution where >p90 results are orders of magnitude slower than the mean, and insert/erase operations are very expensive.

std::unordered_map is typically a "bucket-of-linked-lists" (chaining) design, where each insertion can trigger a separate heap allocation for a new node. milo::HashMap uses Open Addressing, where all data lives in a single, contiguous array.

In std::unordered_map, Pointer/Iterator stability mandates in the C++ standard requires that once an element is inserted it's address must remain constant until that specific element is erased. Even if the map rehashes and grows to 1,000x its original size, the pointers to existing elements must remain valid. This leads to frequent cache misses and a fragmented memory layout. 

FlatMap is faster precisely because it ignores this directive.

How and Why?

FlatMap performs Linear Probing with prefetch and branch predictor hints over slot metadata stored contiguously in memory, seperate from key-value storage.
modern CPU's are VERY good at pre-fetching linear arrays, and these clock cycles we save here allows for cheap re-addressing (shifting) on deletion of any element, ensuring our data stays packed tight in one linear memory space. 

In std::unordered_map or data structures with "tombstones" (marking a slot as deleted but not moving data), the map gets "polluted" over time. Lookups have to jump over these dead slots, making the search take longer, and forcing data packed into buckets to be evaluated before the next bucket can be looked at. Almost every lookup will have cache misses.


## Key Features

* **No per‑element heap allocations:** Memory is allocated in bulk up front.

* **Excellent cache locality:** Avoids pointer chasing and utilizes a separated metadata array. A probe loop touches only a dense byte array, checking 64 slots per cache miss.
  
* **Deterministic Deletions:** Uses backward-shift deletion instead of tombstones. Erasing is O(1) amortized.
 
* **Tightly bounded operation times:** Designed specifically for high-frequency environments where microsecond spikes are unacceptable.


## Why Determinism Matters Beyond Speed

When attempting to build high-frequency trading (HFT) systems, microseconds can be the difference between a won or lost trade. We need to be absolutely sure we are not missing opportunities due to structural flaws in our own containers.

* **Smarter Provisioning:** Provision your resources based on the mean latency plus a small safety margin, rather than massively over‑provisioning just to absorb p99.9 spikes.
* **Better Debuggability:** Performance anomalies and systemic issues are no longer hidden inside the natural variance of your data structures.


**Why Determinism Matters Beyond Speed**

  When attempting to build high-frequency trading or latency sensitive systems, microseconds can be the difference between a won or lost trade.
  We need to be absolutely sure we are not missing opportunities due to structural flaws in our own containers.
    
  milo::FlatMap is intended to **help** with these issues. It can't solve them :) 
  
 

For string keys, `milo::char32` is recommended. It is a thin wrapper over `char[32]`. Because `milo::FlatMap` stores data contiguously,
pairing it with a 32-byte value struct aligns perfectly with standard 64-byte cache lines.

'milo::char32' contains a string literal constructor, so standard milo::FlatMap["StringKey"].item() calls will work.

For example:

'''

  struct alignas(32) Position{
      bool open = false;
      float pnl = 0.0;

      // flatten everything
      void flatten(){ 
      ; 
      
      } 
  };
  
  // char[32] = 32 bytes, Position = 32 bytes
  
  milo::FlatMap<milo::char32,std::vector<<Position>> open_positions;
  
  
  if(CATASTROPHIC_NEWS_EVENT){
      // Because data is stored contiguously,
      a 'Flatten All' operation 
      // is a highly cache-friendly linear sweep.
      
     for (auto& position : open_positions["NVDA"]) {
        position.flatten();
    }  
  }
  
  // This closes EVERY position in the map
  for (auto& [ticker, position] : open_positions) {
      position.close(); 
  }
  
'''

Because milo::FlatMap stores data contiguously a 'Flatten All'(perform an action on EVERY entry) operation is a cache-friendly linear sweep.

Conversely, specific removals utilize backward-shift deletion, ensuring that the map remains packed and deterministic without the performance degradation caused by tombstones.











# milo::FlatMap

A deterministic, open-addressed flat hash map for low-latency C++ systems, designed for an ultra-smooth latency distribution.

While `std::unordered_map` is great for general-purpose computing, it suffers from a poor latency distribution where >p90 results can be orders of magnitude slower than the mean. `milo::FlatMap` is designed to remove sources of unpredictable tail latencies, allowing you to sketch systems quickly with verbose access while maintaining strict performance bounds.




