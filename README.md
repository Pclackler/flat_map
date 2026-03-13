
## milo::flat_map
A deterministic, open‑addressed hash map for low‑latency C++ systems, designed to provide a smoother latency distribution through p99.

`milo::flat_map` was designed to allow me to sketch systems quickly with verbose access to resources while not introducing wild tail latencies.

`std::unordered_map` operations above >p90 can be orders of magnitude slower than the mean, and insert/erase operations are expensive. Pointer/Iterator stability mandates in the C++ standard requires that once an element is inserted it's address must remain constant until that specific element is erased. Even if *OTHER* elements are removed or changed, and the map rehashes and grows to 1,000x its original size, the pointers to existing elements must remain valid. This causes a a fragmented memory layoutand frequent cache misses with behaivor closer to a "bucket-of-linked-lists" , where each insertion can trigger a separate heap allocation for a new node. 

flat_map is faster because it ignores this.

<img src="./images/bar_lookup.png" width="400" alt="A descriptive text"><img src="./images/bar_lookup_append.png" width="400" alt="A descriptive text">
<img src="./images/bar_insert.png" width="400" alt="A descriptive text"><img src="./images/bar_erase.png" width="400" alt="A descriptive text">

## How and Why?

 `milo::flat_map` deliberately does not provide pointer/iterator stability.
 
 Instead, it stores all data in a single contiguous array, using open addressing with linear probing combined with branch/prefetch hints. A separate metadata array (one byte per slot) holds the probe state, allowing the CPU to scan 64 slots per cache miss – a technique that modern hardware prefetchers handle extremely well.

The cycles we save here allows for backward‑shift deletion (instead of tombstones) to keep the array tightly packed. This avoids the “pollution” problem seen in tombstone‑based maps, where deleted slots degrade lookup performance over time. 

## Key Features

* **No per‑element heap allocations:** Memory is allocated in bulk up front.

* **Excellent cache locality:** Avoids pointer chasing and utilizes a separated metadata array. A probe loop touches only a dense byte array, checking 64 slots per cache miss.
  
* **Deterministic Deletions:** Uses backward-shift deletion instead of tombstones. Erasing is O(1) amortized.
 
* **Tightly bounded operation times:** Designed specifically for high-frequency environments where microsecond spikes are unacceptable.


## Why Determinism Matters Beyond Speed 

  When attempting to build high-frequency trading or latency sensitive systems, microseconds can be the difference between a positive or negative outcome.
  The first step in that process is making sure the outcome is not notably affected by structural flaws in our own containers.

  Operations need to be fast, but also *reliable*, something that takes 10ns *most* of the time, but 1ms *sometimes* isn't well suited for latency-sensitive applications.
   
        


  ## Usage
  
For string keys, `milo::char32` is recommended. It is a thin wrapper over `char[32]`. Because `milo::flat_map` stores data contiguously,
pairing it with a 32-byte value struct aligns perfectly with standard 64-byte cache lines.

'milo::char32' contains a string literal constructor, so standard milo::flat_map["StringKey"].item() calls will work.

For example, let's assume `milo::flat_map open_positions` is in our hotpath and accessed every program cycle :

```C++


  struct alignas(32) Position{
      bool open = false;
      float pnl = 0.0;
    
      void flatten(){ ; // flatten everything } 
  };
    
  // char[32] = 32 bytes, Position = 32 bytes 
  milo::flat_map<milo::char32,std::vector<Position>> open_positions;


  // Somewhere in main.cpp inside a tightly bound loop:
  while(true){

  // Because data is stored contiguously, a 'Flatten All' operation is a highly cache-friendly linear sweep. 
  if(CATASTROPHIC_NEWS_EVENT){
    for (auto& position : open_positions["NVDA"]) {       
        position.flatten();
    }  
 
  // This closes EVERY position in the map. Highlighting the benefits of backward-shift deletion over stale tombstone entries after accumulating many entries.
  for (auto& [ticker, position] : open_positions) {
      position.close(); 
  }
 }
}

  
```




  **DISCLAIMER** *It's worth mentioning that any hash based lookup is very likely, **not** the right tool for RTOS/embedded or deterministic low latency environments :)*






