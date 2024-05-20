# Design Notes

Architecture, implementation decisions, trade-offs, correctness guarantees, and measured performance.

---

## 0. Technical Scope

MiniSDK combines C++20 language features, explicit resource ownership, concurrent execution, device abstractions, and performance-oriented memory management in one codebase. The following concepts are represented in the implementation.

**Types and object lifetime**
- Class design: default and parameterised constructors, destructors, `explicit`
- Rule of Five: copy constructor, copy assignment, move constructor, move assignment, destructor
- Copy-and-swap, non-member `swap`, self-assignment safety
- How a user-declared destructor silently suppresses the implicit move operations
- `const` correctness, and why `const` does not propagate through a pointer
- Operator overloading: `operator[]` in both qualifications, `==`, `+=`, `<<`
- Rule of Zero where no resource is owned

**Polymorphism and templates**
- Abstract interfaces, pure virtual functions, virtual destructors
- Preventing slicing by making copy and move operations `protected` in a polymorphic base
- Class templates: `RegisterMap<T>`
- STL algorithms (`transform`, `copy_if`, `all_of`) and containers (`unordered_map`, `vector`,

  `queue`)

**Resource management**
- RAII, both hand-written (`ScopedDeviceLock`) and standard (`lock_guard`, `unique_lock`)
- Smart pointers: `unique_ptr` for exclusive ownership, `shared_ptr` where a shared object must

  outlive its removal from a container, `make_unique` / `make_shared`
- Raw `new[]` / `delete[]` and manual ownership, where explicit allocation semantics are required
- Alignment: `alignof(std::max_align_t)`, `std::align_val_t`, aligned `operator new`
- A pool allocator with an intrusive free list

**Concurrency**
- `std::thread`, `std::mutex`, `std::shared_mutex`, `std::condition_variable`
- A thread pool: producer/consumer queue, waiting on a predicate, graceful shutdown, joining
- Lock granularity: per-device locks instead of one lock over everything
- Non-atomic read-modify-write races, and a test that fails without the lock
- `std::atomic`, memory ordering, `thread_local`
- False sharing and cache-line padding
- Reentrancy: a callback that mutates the structure currently being iterated
- Exception safety across thread boundaries

**Functional and event-driven style**
- Lambdas, captures, `std::function`
- A publish/subscribe bus, and why `std::function` cannot be compared for unsubscription

**Tooling and engineering practice**
- CMake: library and executable targets, options, INTERFACE targets for warning flags
- Unit testing with Catch2 and CTest
- Continuous integration across two compilers and two build types
- AddressSanitizer, UndefinedBehaviorSanitizer, ThreadSanitizer, Valgrind
- Benchmarking methodology, and how easy it is to measure the wrong thing
- C++20 specifics: `if constexpr`, `std::bit_ceil`, `std::bit_floor`, `std::countr_zero`,

  `contains()`

The sections below record the decisions that came out of all this, including the ones that were

wrong first.

---

## 1. Buffer owns its payload by hand

`Buffer` manages a raw `unsigned char*` instead of holding a `std::vector<unsigned char>`.

A vector would be the correct choice in production code. The raw pointer is here because the

point of the type is to make ownership explicit: copy constructor, copy assignment, move

constructor, move assignment, destructor and a non-member `swap` are all written out, and each

one is pinned by a test.

Two details that are easy to get wrong and are covered deliberately:
- **Self-assignment.** Copy assignment is implemented with copy-and-swap, so `a = a` cannot

  free the payload it is about to read.
- **Self-append.** `a += a` allocates the new payload before releasing the old one, so the

  second half of the copy does not read freed memory.

A third one was a real bug until a test caught it: a sized `Buffer` must be **value-initialised**

(`new unsigned char[n]()`). Without the parentheses, reading untouched bytes is indeterminate,

and because the pool recycles memory, a pooled buffer would hand back whatever the previous

owner left behind.

### Why `operator=` is not both copy-and-swap and a separate move

An earlier version declared both:

```cpp

Buffer& operator=(Buffer other);        // copy-and-swap

Buffer& operator=(Buffer&& other);      // move assignment

```

For an rvalue argument both are equally good matches, so `b = std::move(x)` is ambiguous and

does not compile. It went unnoticed only because nothing in the codebase move-assigned a buffer

yet. The class now has the conventional pair: `operator=(const Buffer&)` and

`operator=(Buffer&&)`.

---

## 2. The pool allocates payloads, not Buffer objects

A class-level `operator new` on `Buffer` would not optimize the relevant allocation path.

`Buffer` objects are 24 bytes and are almost always created on the stack:

```cpp

Buffer b(1500);                      // the object never goes through operator new

this->data = new unsigned char[n];   // this is the allocation that happens 8000 times

```

So the pool sits under the **payload**, not under the object.

Payloads are variable sized while a free list needs uniform blocks. The pool therefore serves

any request up to `blockSize` from its arena and forwards everything larger to the global

allocator - the same shape as a packet pool sized around an MTU. It is also why the benchmark

contains a case the pool cannot win.

### Buffer learns about the pool through a constructor argument

```cpp

Buffer b(1500);          // new[] / delete[], unchanged behaviour

Buffer b(1500, &pool);   // payload from the pool

```

A global or singleton pool would have been shorter to write. It was rejected because the

benchmark has to compare both paths **in the same process**, tests would share mutable state

between cases, and TSan would have one more global to reason about.

The cost: `Buffer` grew from 16 to 24 bytes, and the pool must outlive every `Buffer` that was

given it, as well as every thread that allocated from it.

### The pool zeroes its memory on purpose

`new unsigned char[n]()` zeroes; the pool returns recycled memory. The pool path therefore runs

a `memset`. That is not a safety afterthought, it is what keeps the benchmark honest - without

it, part of the "speedup" would just be work that was skipped.

It also explains why the measured gain on a 1500 B payload is 1.64x rather than something

dramatic: zeroing 1500 bytes costs more than either allocator does.

---

## 3. Allocator Iterations and Performance Trade-offs

Each row was measured with the same harness; the ratio is `new[]` divided by pool, so higher is

better for the pool.

| Iteration | 1500 B / 1 thread | 64 B / 1 thread | 9000 B | 1500 B / 8 threads |

| --- | --- | --- | --- | --- |

| One mutex, one free list | 1.41x | 1.13x | 0.99x | **0.12x** |

| Cache-line separated shards | 0.68x | 0.98x | 0.98x | 1.43x |

| Power-of-two sizes | 1.29x | **0.89x** | 1.00x | 1.40x |

| Per-thread cache | **1.64x** | **1.53x** | 0.98x | **1.57x** |

How much to trust each row. The first two were taken with the original harness, which timed all

runs of one variant and then all runs of the other; repeating it disagreed with itself by up to

60%, which is what prompted the interleaving described in section 6. Treat those two rows as

indicative of direction, not as precise figures - the `0.12x` and the `0.68x` regression are far

larger than that error bar, which is why the conclusions still hold. The last two rows come from

the interleaved harness; the power-of-two row is the middle of three repetitions, the

per-thread-cache row is a single run.

**One mutex, one free list.** Fine single-threaded, catastrophic at 8 threads: 8x slower than

doing nothing special. glibc `malloc` keeps per-thread arenas, so threads barely interact, while

every pooled operation queued behind one lock and fought over one cache line.

**Shards.** The arena was split into independent free lists, one per hardware thread, each

`alignas(64)` so their heads do not share a cache line. A thread starts at its own shard and

only visits the others when it runs dry; a freed block returns to **the shard it came from**,

computed from its address, otherwise blocks would drain into whichever thread frees the most.

This fixed the threaded case and broke the single-threaded one: 1.41x down to 0.68x. Picking a

shard and locating a block introduced three integer divisions per cycle, each costing more than

the allocation they were meant to accelerate.

**Power-of-two sizes.** Rounding `blockSize` up and `shardCount` down to powers of two turns

`/ blockSize` into a shift and `% shardCount` into a mask. The dynamically-initialised

`thread_local` holding the shard index was also replaced with a constant-initialised one, which

removes a guard check from every access.

That recovered the single-threaded case but left 64 B payloads at 0.89x - consistently, across

runs. The cause is specific: glibc's **tcache** serves small allocations from a lock-free

per-thread cache, and the pool still took a mutex on its fast path.

**Per-thread cache.** A four-entry cache per thread in front of the shards, which is the same

mechanism tcache uses. The common allocate/free pair now touches no lock at all.

Three things this required to stay correct:
- `available()` must still be exact, but blocks can now sit in another thread's cache. A

  registry of live caches is walked by `available()` only - the hot path never touches it.
- A thread that exits must return its parked blocks, or the arena shrinks with every thread.

  The thread-local object's destructor flushes them back.
- `available()` reads counters owned by other threads, which is a data race. The counters are

  relaxed atomics: plain `mov` instructions on x86, but no longer a race that TSan reports.

### What is still left on the table
- The free lists are mutex-protected rather than lock-free. A CAS-based stack would avoid the

  lock entirely, at the price of the ABA problem.
- Block size is fixed per pool. Real allocators keep several size classes.
- `blockSize` is rounded up to a power of two, so a request for 3000 B occupies 4096 B.
- The per-thread cache tracks a limited number of pools; beyond that, allocation falls back to

  the shard path, which is correct but slower.

---

## 4. Locking is per device, not per manager

`DeviceManager` keeps `id -> {device, mutex}` in one map, guarded by a `std::shared_mutex`:

shared while looking a device up, exclusive while adding or removing one. The per-device mutex

is held only for the operation itself.

Storing the device and its lock in the same entry removes a whole class of bug: the two cannot

get out of step.

**Entries are `shared_ptr`.** `removeDevice` erases the entry from the map while another thread

may still be using that device. With a `unique_ptr` the mutex would be destroyed while locked,

which is undefined behaviour and nearly impossible to reproduce on demand. With a `shared_ptr`

the entry stays alive until the last user drops it.

### An earlier version lied with const

```cpp

void sendBuffer(unsigned int id, const Buffer& buffer) const;  // and it mutated the device

```

`const` does not propagate through a pointer. Inside a const member function the vector element

is a `const unique_ptr<IDevice>&`, but `operator->` still yields a non-const `IDevice*`, so

incrementing the device's packet counter compiled without complaint. The `mutable` on the lock

map was a symptom of the same thing. Both are gone.

### The event bus copies its subscribers before calling them

`eventHappen()` copies the subscriber list under the lock, releases the lock, and only then

invokes the callbacks. Iterating the live list would mean a callback that registers an event

invalidates the iterator, and invoking callbacks while holding the lock would deadlock the

moment one of them called back into the bus. A test registers an event from inside a callback.

### The thread pool catches what jobs throw

An exception escaping a job would propagate out of the thread function and call

`std::terminate`, killing the process because one task misbehaved. Jobs run inside a

`try`/`catch`; the pool reports and keeps going.

`enqueue` after `shutdown` throws rather than accepting work that would never run.

---

## 5. Errors are reported, not swallowed

Earlier versions returned quietly on bad input: `sendBuffer` to an unknown id did nothing,

`getRegister` returned `0`, `writeRegister` created a register that did not exist, `add` ignored

duplicates.

The last two are the dangerous ones. `0` is a legitimate register value, so a caller cannot tell

"the register is zero" from "that device does not exist". And a typo in a register name silently

created a new register that nothing would ever read.

The rule now: lookups and insertions throw (`std::out_of_range`, `std::invalid_argument`);

removals are idempotent, since removing something twice is not a mistake worth an exception.

---

## 6. Benchmark methodology

An allocator benchmark is easy to get wrong, so the harness documents its own defences:
- **Interleaved variants.** The first version timed all runs of `new[]`, then all runs of the

  pool. Repeated runs disagreed by 60%, because machine load drifting between the two halves

  landed directly in the ratio. Each run now measures both back to back and the reported figure

  is the median of the per-run ratios.
- **Spread column.** The range of per-run ratios is printed. If it straddles 1.00, the result is

  noise and should be read as "no difference" - which is exactly how the 9000 B row behaves.
- **Warmup discarded**, to keep page faults and frequency scaling out of the first sample.
- **Checksum printed.** Every iteration touches the first and last byte and folds them into a

  value printed at the end, otherwise the optimiser is free to delete the allocation entirely.
- **Release build enforced.** The benchmark prints a warning if `NDEBUG` is not set.

What the numbers do not isolate: the cost of zeroing the payload is included in both paths, so

the ratios describe `Buffer` construction, not the allocator in isolation.

---

## 7. Environment notes

**ThreadSanitizer on Ubuntu 24.04.** TSan segfaults at process start, before any test runs.

Ubuntu 24.04 raised `vm.mmap_rnd_bits` from 28 to 32, and with that much ASLR entropy the

runtime cannot place its shadow memory. The fix is to lower it:

```bash

sudo sysctl -w vm.mmap_rnd_bits=28

```

CI does the same, because the `ubuntu-latest` runners are 24.04.

**Warnings are not applied to dependencies.** `-Wall -Wextra -Wshadow -Werror` is carried by an

INTERFACE target that only the project's own targets link. Putting it on everything would mean a

warning inside Catch2 fails our build.

**Sanitizer flags are global.** They are added before any target is defined, so fetched

dependencies are instrumented too - TSan needs that to avoid false reports.

**Test discovery runs at test time**, not at build time (`DISCOVERY_MODE PRE_TEST`). Catch2

otherwise executes the test binary during the build to enumerate cases, which turned the TSan

startup crash above into a build failure instead of a failing test.

---

## 8. Compile-Time Tracing

Every component used to print to `std::cout` from its constructors and hot paths. Running the

demo produced roughly 24000 lines, the output of concurrent threads interleaved mid-line, and

any benchmark would have measured `iostream` rather than the code under test.

The statements now sit behind `if constexpr (traceEnabled)`, switched by `-DMINISDK_TRACE=ON`

and off by default. A `constexpr` flag was chosen over a macro so the logging statements stay

ordinary code that the compiler still parses and type-checks.
