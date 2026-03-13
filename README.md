
## milo::flat_map
A minminal open‑addressed hash map for low‑latency C++ systems.  

 `std::unordered` is nice for assembling systems in C++. So I decided to make my own.
 `flat_map` was designed to be a faster alternative and provide better latency distribution through p99.
 
The C++ standard guarantees that pointers and references to elements in an `std::unordered_map` remain valid from the moment an element is inserted until it is erased. This stability holds even if other elements are inserted, removed, or if the map rehashes—growing to many times its original size—without affecting the memory address of existing elements. Iterators, however, may be invalidated by a rehash. This requirement forces the implementation to store each element in a separately allocated node, leading to a fragmented memory layout with the cache‑unfriendly behavior of a bucket‑of‑linked‑lists

## How and Why?

 `flat_map` deliberately does not provide pointer/iterator stability.
 
 It stores data in contiguous memory, using open addressing with linear probing combined with branch/prefetch hints, and re-packing on erasure. A separate metadata array (one byte per slot) holds the probe state, allowing the CPU to scan 64 slots per cache miss – a technique that modern hardware prefetchers handle extremely well.

The cycles we save here allows for backward‑shift deletion to keep the array tightly packed. This avoids the “pollution” problem seen in tombstone‑based maps, where deleted slots are simply marked dead, and can degrade lookup performance over time. 

 In latency sensitive(or highly deterministic) systems, microseconds can be the difference between a positive or negative outcome.
 If you are designing for that it can be very difficult to identify sources of large tail latencies once systems become complex.
 
 I made this so I can assemble programs quickly with a bit more peace of mind, kwoning the outcome is not notably affected by structural flaws in the containers I'm using. 
  
## Key Features

* **No per‑element heap allocations:** Memory is allocated in bulk up front.

* **Excellent cache locality:** Avoids pointer chasing and utilizes a separated metadata array. A probe loop touches only a dense byte array, checking 64 slots per cache miss.
  
* **Deterministic Deletions:** Uses backward-shift deletion instead of tombstones. Erasing is O(1) amortized.
 
* **Tightly bounded operation times:** Designed specifically for high-frequency environments where microsecond spikes are unacceptable.

 ## Performance 
 flat_map vs unordered_map.
 
 10mm operations ea. Lookup, insert, lookup+append,erase. 
<img src="./images/bar_lookup.png" width="400" alt="A descriptive text"><img src="./images/bar_lookup_append.png" width="400" alt="A descriptive text">
<img src="./images/bar_insert.png" width="400" alt="A descriptive text"><img src="./images/bar_erase.png" width="400" alt="A descriptive text">



  
## Example
  
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


## Example
  
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
  
  ## Usage

  git clone pclackler/flat_map
  
  cd flat_map
  
  mkdir build && cd build
  
  cmake .. -DCMAKE_BUILD_TYPE=Release
  cmake --build .
  ./bin/milo_benchmark   # or ./milo_benchmark depending on platform




  




