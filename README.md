# Smart Sort Library (by Vo1ic)

![C++17](https://img.shields.io/badge/C++-17-blue.svg)
![Header Only](https://img.shields.io/badge/Header-Only-success.svg)
![Zero Dependencies](https://img.shields.io/badge/Dependencies-None-brightgreen.svg)

**Smart Sort** is an ultra-fast, highly optimized, header-only C++ sorting library designed as a zero-friction, drop-in replacement for `std::sort`. It automatically analyzes your data at compile-time and runtime to select the absolute best sorting algorithm for your specific use case.

Stop guessing which algorithm to use. Just call `Algorithms::Sorting::sort(arr)` and get up to **18x performance boost**.

## 🚀 Why Smart Sort?

*   **Mind-Blowing Speed:** Outperforms `std::sort` (parallel) by 3-4x on integers/floats, and single-threaded `std::sort` by up to **18x**. Beats `Boost::sort::block_indirect_sort` on primitives and strings.
*   **Zero Brain Power Required:** One function. One argument. No need to pass execution policies or specify which radix pass to use. The library does the heavy lifting.
*   **Header-Only & Zero Dependencies:** Just drop `Sorting.hpp` into your project. No building, no linking, no Boost required.
*   **Built-in Safety:**
    *   **OOM (Out Of Memory) Protection:** If the system lacks memory for radix buffers, it instantly falls back to in-place `std::sort(par)` without crashing.
    *   **NaN Safety:** Correctly handles `NaN` values in floating-point arrays (pushes them to the end, just like `std::sort`).
    *   **Overflow Guard:** Safely handles arrays exceeding `INT_MAX`.

## 📊 Benchmarks (8-core CPU)

| Data Type | Count | `std::sort` | `std::sort(par)` | Boost `block_indirect` | **Smart Sort** | Speedup vs `std::sort` |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `int32_t` | 100M | 8.8 s | 1.9 s | 1.0 s | **0.58 s** | **~15x** |
| `float` | 100M | 11.1 s | 2.2 s | 1.3 s | **0.57 s** | **~19x** |
| `double` | 50M | - | 1.2 s | **0.71 s** | 0.75 s | **~1.6x** (vs par) |
| `std::string` | 10M | 8.7 s | 3.5 s | 2.6 s | **2.0 s** | **~4.3x** |
| `struct` (large) | 25M | 5.6 s | **1.6 s** | 1.4 s | **1.6 s** | **~3.5x** |

*(Tested on random distributions. Real-world data with narrow ranges triggers our **Skip-Pass optimization**, offering even higher speeds!)*

## 🧠 Under the Hood

Smart Sort isn't just one algorithm; it's an intelligent dispatcher:

1.  **32-bit Primitives (`int`, `float`):** Uses an aggressive **8-bit Parallel Radix Sort** (4 passes).
2.  **64-bit Primitives (`double`, `int64_t`):** Uses a specialized **11-bit Parallel Radix Sort** (6 passes) to minimize cache misses.
3.  **Strings (`std::string`):** Uses a highly optimized **Recursive MSD Radix Sort** with dynamic thread pooling (up to 64 threads).
4.  **Complex Structs:** Automatically falls back to In-Place Parallel Sort for massive arrays to prevent memory bandwidth bottlenecks, or uses Radix if you provide a key extractor.
5.  **Tiny Arrays (N < 64):** Instantly delegates to a branchless Insertion Sort.

## 🛠️ Quick Start

```cpp
#include <vector>
#include <iostream>
#include "Sorting.hpp"

int main() {
    std::vector<int> data = {5, 2, 9, 1, 5, 6, 3};

    // Replace this:
    // std::sort(data.begin(), data.end());
    
    // With this:
    Algorithms::Sorting::sort(data);

    for(int val : data) std::cout << val << " ";
    return 0;
}
```

### Sorting Custom Structs (Key Extractor)

Want to sort millions of players by score at Radix speed? Just provide an extractor:

```cpp
struct Player {
    int id;
    int score;
    double rating;
};

std::vector<Player> players = /* ... load data ... */;

// Sorts by score using Parallel Radix Sort!
Algorithms::Sorting::sort(players, [](const Player& p) { return p.score; });
```
