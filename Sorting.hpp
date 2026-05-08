/**
 * @file Sorting.hpp
 * @brief Universal Smart Sorting Library (Production-Ready)
 * @date 2026
 *
 * Header-only C++ library with parallel Radix Sort and intelligent dispatcher.
 * Supports: int, float, double, int64_t, std::string, custom structs.
 * Features: OOM pre-check, NaN handling, skip-pass optimization, insertion sort for small N.
 */

#pragma once

#include <vector>
#include <thread>
#include <barrier>
#include <array>
#include <algorithm>
#include <bit>
#include <type_traits>
#include <cstdint>
#include <future>
#include <string>
#include <atomic>
#include <execution>
#include <cmath>
#include <new>
#include <limits>

namespace Algorithms {
namespace Sorting {
namespace detail {

    // ========== SMALL ARRAY OPTIMIZATION ==========
    template <typename T, typename KeyFunc>
    inline void insertion_sort(T* arr, int n, KeyFunc key_of) {
        for (int i = 1; i < n; i++) {
            T tmp = std::move(arr[i]);
            auto k = key_of(tmp);
            int j = i - 1;
            while (j >= 0 && key_of(arr[j]) > k) {
                arr[j + 1] = std::move(arr[j]);
                j--;
            }
            arr[j + 1] = std::move(tmp);
        }
    }

    // ========== 32-BIT RADIX KEY ==========
    template <typename T>
    inline uint32_t to_radix_key(T val) noexcept {
        if constexpr (std::is_integral_v<T> && std::is_signed_v<T> && sizeof(T) == 4) {
            return static_cast<uint32_t>(val) ^ 0x80000000u;
        }
        else if constexpr (std::is_same_v<T, float>) {
            uint32_t u = std::bit_cast<uint32_t>(val);
            uint32_t mask = (u & 0x80000000u) ? 0xFFFFFFFFu : 0x80000000u;
            return u ^ mask;
        }
        else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) <= 4) {
            return static_cast<uint32_t>(val);
        }
        return static_cast<uint32_t>(val);
    }

    // ========== 64-BIT RADIX KEY ==========
    template <typename T>
    inline uint64_t to_radix_key_64(T val) noexcept {
        if constexpr (std::is_integral_v<T> && std::is_signed_v<T> && sizeof(T) == 8) {
            return static_cast<uint64_t>(val) ^ 0x8000000000000000ull;
        }
        else if constexpr (std::is_same_v<T, double>) {
            uint64_t u = std::bit_cast<uint64_t>(val);
            uint64_t mask = (u & 0x8000000000000000ull) ? 0xFFFFFFFFFFFFFFFFull : 0x8000000000000000ull;
            return u ^ mask;
        }
        else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T> && sizeof(T) == 8) {
            return static_cast<uint64_t>(val);
        }
        return static_cast<uint64_t>(val);
    }

    struct DefaultKeyExtractor {
        template <typename T>
        decltype(auto) operator()(const T& obj) const noexcept { return obj; }
    };

    // ========== OPTIMAL THREAD COUNT ==========
    inline int optimal_threads(size_t n) noexcept {
        int T_cores = static_cast<int>(std::thread::hardware_concurrency());
        if (T_cores == 0) T_cores = 1;
        if (n < 300000)       T_cores = 1;
        else if (n < 700000)  T_cores = std::min<int>(T_cores, 2);
        else if (n < 2000000) T_cores = std::min<int>(T_cores, 4);
        else if (n < 6000000) T_cores = std::min<int>(T_cores, 8);
        return T_cores;
    }

    // ========== OOM-SAFE BUFFER ALLOCATION ==========
    // Allocates buffer BEFORE any data is touched. Returns empty vector on OOM.
    template <typename T>
    inline std::vector<T> try_alloc_buffer(size_t n) {
        std::vector<T> buf;
        try { buf.resize(n); } catch (const std::bad_alloc&) { /* returns empty */ }
        return buf;
    }

    // ========== SEQUENTIAL 8-BIT RADIX (32-bit keys) ==========
    template <typename T, typename KeyExtractor>
    inline void radix_sort_8bit_seq(T* arr, T* buffer, int n, KeyExtractor extract) {
        if (n < 64) {
            insertion_sort(arr, n, [&](const T& v) { return to_radix_key(extract(v)); });
            return;
        }

        int count[4][256] = { 0 };
        for (int i = 0; i < n; i++) {
            uint32_t key = to_radix_key(extract(arr[i]));
            count[0][key & 0xFF]++;
            count[1][(key >> 8) & 0xFF]++;
            count[2][(key >> 16) & 0xFF]++;
            count[3][(key >> 24) & 0xFF]++;
        }

        // Skip-pass detection
        bool skip[4] = {};
        for (int j = 0; j < 4; j++)
            for (int b = 0; b < 256; b++)
                if (count[j][b] == n) { skip[j] = true; break; }

        for (int j = 0; j < 4; j++) {
            if (skip[j]) continue;
            for (int i = 1; i < 256; i++) count[j][i] += count[j][i - 1];
        }

        T* src = arr;
        T* dst = buffer;
        for (int j = 0; j < 4; j++) {
            if (skip[j]) continue;
            int shift = j * 8;
            for (int i = n - 1; i >= 0; i--) {
                uint32_t key = to_radix_key(extract(src[i]));
                dst[--count[j][(key >> shift) & 0xFF]] = std::move(src[i]);
            }
            std::swap(src, dst);
        }
        if (src != arr)
            for (int i = 0; i < n; i++) arr[i] = std::move(src[i]);
    }

    // ========== SEQUENTIAL 11-BIT RADIX (32-bit keys, 3 passes) ==========
    template <typename T, typename KeyExtractor>
    inline void radix_sort_11bit_seq(T* arr, T* buffer, int n, KeyExtractor extract) {
        if (n < 64) {
            insertion_sort(arr, n, [&](const T& v) { return to_radix_key(extract(v)); });
            return;
        }

        int count[3][2048] = { 0 };
        for (int i = 0; i < n; i++) {
            uint32_t key = to_radix_key(extract(arr[i]));
            count[0][key & 0x7FF]++;
            count[1][(key >> 11) & 0x7FF]++;
            count[2][(key >> 22) & 0x3FF]++;
        }

        bool skip[3] = {};
        int bk[3] = { 2048, 2048, 1024 };
        for (int j = 0; j < 3; j++)
            for (int b = 0; b < bk[j]; b++)
                if (count[j][b] == n) { skip[j] = true; break; }

        for (int j = 0; j < 3; j++) {
            if (skip[j]) continue;
            for (int i = 1; i < bk[j]; i++) count[j][i] += count[j][i - 1];
        }

        T* src = arr;
        T* dst = buffer;
        for (int j = 0; j < 3; j++) {
            if (skip[j]) continue;
            int shift = j * 11;
            int mask = (j == 2) ? 0x3FF : 0x7FF;
            for (int i = n - 1; i >= 0; i--) {
                uint32_t key = to_radix_key(extract(src[i]));
                dst[--count[j][(key >> shift) & mask]] = std::move(src[i]);
            }
            std::swap(src, dst);
        }
        if (src != arr)
            for (int i = 0; i < n; i++) arr[i] = std::move(src[i]);
    }

    // ========== SEQUENTIAL 64-BIT RADIX (6 passes: 5×11 + 1×9) ==========
    template <typename T, typename KeyExtractor>
    inline void radix_sort_64bit_seq(T* arr, T* buffer, int n, KeyExtractor extract) {
        if (n < 64) {
            insertion_sort(arr, n, [&](const T& v) { return to_radix_key_64(extract(v)); });
            return;
        }

        int count[6][2048] = { 0 };
        for (int i = 0; i < n; i++) {
            uint64_t key = to_radix_key_64(extract(arr[i]));
            count[0][key & 0x7FF]++;
            count[1][(key >> 11) & 0x7FF]++;
            count[2][(key >> 22) & 0x7FF]++;
            count[3][(key >> 33) & 0x7FF]++;
            count[4][(key >> 44) & 0x7FF]++;
            count[5][(key >> 55) & 0x1FF]++;
        }

        bool skip[6] = {};
        int bk[6] = { 2048, 2048, 2048, 2048, 2048, 512 };
        for (int j = 0; j < 6; j++)
            for (int b = 0; b < bk[j]; b++)
                if (count[j][b] == n) { skip[j] = true; break; }

        for (int j = 0; j < 6; j++) {
            if (skip[j]) continue;
            for (int i = 1; i < bk[j]; i++) count[j][i] += count[j][i - 1];
        }

        T* src = arr;
        T* dst = buffer;
        for (int j = 0; j < 6; j++) {
            if (skip[j]) continue;
            int shift = j * 11;
            int mask = (j == 5) ? 0x1FF : 0x7FF;
            for (int i = n - 1; i >= 0; i--) {
                uint64_t key = to_radix_key_64(extract(src[i]));
                dst[--count[j][(key >> shift) & mask]] = std::move(src[i]);
            }
            std::swap(src, dst);
        }
        if (src != arr)
            for (int i = 0; i < n; i++) arr[i] = std::move(src[i]);
    }

    // ========== PARALLEL 8-BIT RADIX ==========
    template <typename T, typename KeyExtractor = DefaultKeyExtractor>
    inline bool parallel_radix_sort_8bit(std::vector<T>& input, KeyExtractor extract = KeyExtractor{}) {
        size_t ns = input.size();
        if (ns < 2) return true;
        if (ns > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
        int n = static_cast<int>(ns);

        int T_cores = optimal_threads(ns);

        // OOM-safe: allocate buffer BEFORE touching data
        auto buffer = try_alloc_buffer<T>(ns);
        if (buffer.empty()) return false;

        if (T_cores == 1) {
            radix_sort_8bit_seq(input.data(), buffer.data(), n, extract);
            return true;
        }

        int chunk = (n + T_cores - 1) / T_cores;
        T* src = input.data();
        T* dst = buffer.data();

        std::vector<std::array<int, 256>> hist(T_cores);
        std::vector<std::array<int, 256>> thr_off(T_cores);

        // Skip-pass: global histograms for passes 1-3 (built during pass 0)
        std::vector<std::array<int, 256>> ghist1(T_cores), ghist2(T_cores), ghist3(T_cores);
        std::array<bool, 4> skip_pass = {};
        int pass_counter_h = 0, pass_counter_s = 0;

        auto calc_prefix = [&]() noexcept {
            int pass = pass_counter_h++;

            if (pass == 0) {
                auto check_skip = [&](int p, auto& gh) {
                    for (int b = 0; b < 256; b++) {
                        int total = 0;
                        for (int t = 0; t < T_cores; t++) total += gh[t][b];
                        if (total == n) { skip_pass[p] = true; return; }
                    }
                };
                for (int b = 0; b < 256; b++) {
                    int total = 0;
                    for (int t = 0; t < T_cores; t++) total += hist[t][b];
                    if (total == n) { skip_pass[0] = true; break; }
                }
                check_skip(1, ghist1);
                check_skip(2, ghist2);
                check_skip(3, ghist3);
            }

            if (skip_pass[pass]) return;

            int bpos[256] = {};
            for (int b = 0; b < 256; b++)
                for (int t = 0; t < T_cores; t++)
                    bpos[b] += hist[t][b];
            int pos = 0;
            for (int b = 0; b < 256; b++) {
                int c = bpos[b]; bpos[b] = pos; pos += c;
            }
            for (int b = 0; b < 256; b++) {
                int p = bpos[b];
                for (int t = 0; t < T_cores; t++) {
                    thr_off[t][b] = p; p += hist[t][b];
                }
            }
        };

        auto swap_ptrs = [&]() noexcept {
            if (!skip_pass[pass_counter_s]) std::swap(src, dst);
            pass_counter_s++;
        };

        std::barrier sync_hist(T_cores, calc_prefix);
        std::barrier sync_scatter(T_cores, swap_ptrs);

        auto worker = [&](int t) {
            int s = t * chunk;
            int e = std::min<int>(n, s + chunk);
            for (int pass = 0; pass < 4; pass++) {
                int shift = pass * 8;
                hist[t].fill(0);

                if (pass == 0) {
                    ghist1[t].fill(0); ghist2[t].fill(0); ghist3[t].fill(0);
                    for (int i = s; i < e; i++) {
                        uint32_t u = to_radix_key(extract(src[i]));
                        hist[t][u & 0xFF]++;
                        ghist1[t][(u >> 8) & 0xFF]++;
                        ghist2[t][(u >> 16) & 0xFF]++;
                        ghist3[t][(u >> 24) & 0xFF]++;
                    }
                } else if (!skip_pass[pass]) {
                    for (int i = s; i < e; i++) {
                        uint32_t u = to_radix_key(extract(src[i]));
                        hist[t][(u >> shift) & 0xFF]++;
                    }
                }

                sync_hist.arrive_and_wait();

                if (!skip_pass[pass]) {
                    auto& off = thr_off[t];
                    for (int i = s; i < e; i++) {
                        uint32_t u = to_radix_key(extract(src[i]));
                        int b = (u >> shift) & 0xFF;
                        dst[off[b]++] = std::move(src[i]);
                    }
                }

                sync_scatter.arrive_and_wait();
            }
        };

        std::vector<std::thread> threads;
        threads.reserve(T_cores);
        for (int t = 0; t < T_cores; t++) threads.emplace_back(worker, t);
        for (auto& th : threads) th.join();

        if (src != input.data()) std::move(src, src + n, input.data());
        return true;
    }

    // ========== PARALLEL 11-BIT RADIX ==========
    template <typename T, typename KeyExtractor = DefaultKeyExtractor>
    inline bool parallel_radix_sort_11bit(std::vector<T>& input, KeyExtractor extract = KeyExtractor{}) {
        size_t ns = input.size();
        if (ns < 2) return true;
        if (ns > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
        int n = static_cast<int>(ns);

        int T_cores = optimal_threads(ns);

        auto buffer = try_alloc_buffer<T>(ns);
        if (buffer.empty()) return false;

        if (T_cores == 1) {
            radix_sort_11bit_seq(input.data(), buffer.data(), n, extract);
            return true;
        }

        int chunk = (n + T_cores - 1) / T_cores;
        T* src = input.data();
        T* dst = buffer.data();

        std::vector<std::array<int, 2048>> hist(T_cores);
        std::vector<std::array<int, 2048>> thr_off(T_cores);

        auto calc_prefix = [&]() noexcept {
            std::array<int, 2048> bpos = {};
            for (int b = 0; b < 2048; b++)
                for (int t = 0; t < T_cores; t++)
                    bpos[b] += hist[t][b];
            int pos = 0;
            for (int b = 0; b < 2048; b++) {
                int c = bpos[b]; bpos[b] = pos; pos += c;
            }
            for (int b = 0; b < 2048; b++) {
                int p = bpos[b];
                for (int t = 0; t < T_cores; t++) {
                    thr_off[t][b] = p; p += hist[t][b];
                }
            }
        };

        auto swap_ptrs = [&]() noexcept { std::swap(src, dst); };
        std::barrier sync_hist(T_cores, calc_prefix);
        std::barrier sync_scatter(T_cores, swap_ptrs);

        auto worker = [&](int t) {
            int s = t * chunk;
            int e = std::min<int>(n, s + chunk);
            for (int pass = 0; pass < 3; pass++) {
                int shift = pass * 11;
                int mask = (pass == 2) ? 0x3FF : 0x7FF;
                hist[t].fill(0);
                for (int i = s; i < e; i++) {
                    uint32_t u = to_radix_key(extract(src[i]));
                    hist[t][(u >> shift) & mask]++;
                }
                sync_hist.arrive_and_wait();
                auto& off = thr_off[t];
                for (int i = s; i < e; i++) {
                    uint32_t u = to_radix_key(extract(src[i]));
                    int b = (u >> shift) & mask;
                    dst[off[b]++] = std::move(src[i]);
                }
                sync_scatter.arrive_and_wait();
            }
        };

        std::vector<std::thread> threads;
        threads.reserve(T_cores);
        for (int t = 0; t < T_cores; t++) threads.emplace_back(worker, t);
        for (auto& th : threads) th.join();

        if (src != input.data()) std::move(src, src + n, input.data());
        return true;
    }

    // ========== PARALLEL 64-BIT RADIX ==========
    template <typename T, typename KeyExtractor = DefaultKeyExtractor>
    inline bool parallel_radix_sort_64bit(std::vector<T>& input, KeyExtractor extract = KeyExtractor{}) {
        size_t ns = input.size();
        if (ns < 2) return true;
        if (ns > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
        int n = static_cast<int>(ns);

        int T_cores = optimal_threads(ns);

        auto buffer = try_alloc_buffer<T>(ns);
        if (buffer.empty()) return false;

        if (T_cores == 1) {
            radix_sort_64bit_seq(input.data(), buffer.data(), n, extract);
            return true;
        }

        int chunk = (n + T_cores - 1) / T_cores;
        T* src = input.data();
        T* dst = buffer.data();

        std::vector<std::array<int, 2048>> hist(T_cores);
        std::vector<std::array<int, 2048>> thr_off(T_cores);

        // Skip-pass: per-thread global histograms for passes 1-5 (built during pass 0)
        std::vector<std::array<std::array<int, 2048>, 5>> ghist(T_cores);
        std::array<bool, 6> skip_pass = {};
        int pass_counter_h = 0, pass_counter_s = 0;

        auto calc_prefix = [&]() noexcept {
            int pass = pass_counter_h++;

            if (pass == 0) {
                // Check pass 0
                for (int b = 0; b < 2048; b++) {
                    int total = 0;
                    for (int t = 0; t < T_cores; t++) total += hist[t][b];
                    if (total == n) { skip_pass[0] = true; break; }
                }
                // Check passes 1-5 from ghist
                for (int p = 1; p < 6; p++) {
                    int bk = (p == 5) ? 512 : 2048;
                    for (int b = 0; b < bk; b++) {
                        int total = 0;
                        for (int t = 0; t < T_cores; t++) total += ghist[t][p-1][b];
                        if (total == n) { skip_pass[p] = true; break; }
                    }
                }
            }

            if (skip_pass[pass]) return;

            std::array<int, 2048> bpos = {};
            for (int b = 0; b < 2048; b++)
                for (int t = 0; t < T_cores; t++)
                    bpos[b] += hist[t][b];
            int pos = 0;
            for (int b = 0; b < 2048; b++) {
                int c = bpos[b]; bpos[b] = pos; pos += c;
            }
            for (int b = 0; b < 2048; b++) {
                int p = bpos[b];
                for (int t = 0; t < T_cores; t++) {
                    thr_off[t][b] = p; p += hist[t][b];
                }
            }
        };

        auto swap_ptrs = [&]() noexcept {
            if (!skip_pass[pass_counter_s]) std::swap(src, dst);
            pass_counter_s++;
        };
        std::barrier sync_hist(T_cores, calc_prefix);
        std::barrier sync_scatter(T_cores, swap_ptrs);

        auto worker = [&](int t) {
            int s = t * chunk;
            int e = std::min<int>(n, s + chunk);
            for (int pass = 0; pass < 6; pass++) {
                int shift = pass * 11;
                int mask = (pass == 5) ? 0x1FF : 0x7FF;
                hist[t].fill(0);

                if (pass == 0) {
                    for (auto& g : ghist[t]) g.fill(0);
                    for (int i = s; i < e; i++) {
                        uint64_t u = to_radix_key_64(extract(src[i]));
                        hist[t][u & 0x7FF]++;
                        ghist[t][0][(u >> 11) & 0x7FF]++;
                        ghist[t][1][(u >> 22) & 0x7FF]++;
                        ghist[t][2][(u >> 33) & 0x7FF]++;
                        ghist[t][3][(u >> 44) & 0x7FF]++;
                        ghist[t][4][(u >> 55) & 0x1FF]++;
                    }
                } else if (!skip_pass[pass]) {
                    for (int i = s; i < e; i++) {
                        uint64_t u = to_radix_key_64(extract(src[i]));
                        hist[t][(u >> shift) & mask]++;
                    }
                }

                sync_hist.arrive_and_wait();

                if (!skip_pass[pass]) {
                    auto& off = thr_off[t];
                    for (int i = s; i < e; i++) {
                        uint64_t u = to_radix_key_64(extract(src[i]));
                        int b = (u >> shift) & mask;
                        dst[off[b]++] = std::move(src[i]);
                    }
                }

                sync_scatter.arrive_and_wait();
            }
        };

        std::vector<std::thread> threads;
        threads.reserve(T_cores);
        for (int t = 0; t < T_cores; t++) threads.emplace_back(worker, t);
        for (auto& th : threads) th.join();

        if (src != input.data()) std::move(src, src + n, input.data());
        return true;
    }

    // ========== PARALLEL MSD STRING SORT ==========
    inline void parallel_msd_step(std::string* a, int n, size_t depth, std::string* aux, std::atomic<int>& active_threads) {
        if (n <= 32) {
            std::sort(a, a + n, [depth](const std::string& x, const std::string& y) {
                if (depth >= x.length() || depth >= y.length()) return x.length() < y.length();
                return x.compare(depth, std::string::npos, y, depth, std::string::npos) < 0;
            });
            return;
        }

        int count[258] = {0};
        for (int i = 0; i < n; i++) {
            int c = depth < a[i].length() ? (unsigned char)a[i][depth] : -1;
            count[c + 2]++;
        }
        for (int r = 0; r < 257; r++) count[r + 1] += count[r];

        for (int i = 0; i < n; i++) {
            int c = depth < a[i].length() ? (unsigned char)a[i][depth] : -1;
            aux[count[c + 1]++] = std::move(a[i]);
        }
        for (int i = 0; i < n; i++) a[i] = std::move(aux[i]);

        std::vector<std::future<void>> futures;
        for (int r = 0; r < 256; r++) {
            int start = count[r + 1];
            int end = count[r + 2];
            if (end - start > 1) {
                if (end - start > 10000 && active_threads.load(std::memory_order_relaxed) < 64) {
                    active_threads.fetch_add(1, std::memory_order_relaxed);
                    futures.push_back(std::async(std::launch::async, [a, start, end, depth, aux, &active_threads]() {
                        parallel_msd_step(a + start, end - start, depth + 1, aux + start, active_threads);
                        active_threads.fetch_sub(1, std::memory_order_relaxed);
                    }));
                } else {
                    parallel_msd_step(a + start, end - start, depth + 1, aux + start, active_threads);
                }
            }
        }
        for (auto& f : futures) f.get();
    }

    inline bool parallel_string_sort_impl(std::vector<std::string>& arr) {
        int n = static_cast<int>(arr.size());
        if (n <= 1) return true;
        auto aux = try_alloc_buffer<std::string>(n);
        if (aux.empty()) return false;
        std::atomic<int> active_threads(1);
        parallel_msd_step(arr.data(), n, 0, aux.data(), active_threads);
        return true;
    }

} // namespace detail

/**
 * @brief Universal Sort with Functor (Extractor or Comparator).
 */
template <typename T, typename Functor>
inline void sort(std::vector<T>& arr, Functor fn) {
    if constexpr (std::is_same_v<T, std::string>) {
        if (!detail::parallel_string_sort_impl(arr))
            std::sort(std::execution::par, arr.begin(), arr.end());
    } else if constexpr (std::is_invocable_v<Functor, T, T>) {
        // Comparator -> In-Place Parallel Sort
        std::sort(std::execution::par, arr.begin(), arr.end(), fn);
    } else {
        // Extractor
        using ExtractedType = std::invoke_result_t<Functor, T>;
        auto fallback = [&]() {
            auto cmp = [&fn](const T& a, const T& b) { return fn(a) < fn(b); };
            std::sort(std::execution::par, arr.begin(), arr.end(), cmp);
        };

        if constexpr (sizeof(ExtractedType) <= 4) {
            if constexpr (sizeof(T) <= 8) {
                if (!detail::parallel_radix_sort_8bit(arr, fn)) fallback();
            } else {
                if (arr.size() < 100000) {
                    if (!detail::parallel_radix_sort_11bit(arr, fn)) fallback();
                } else {
                    fallback();
                }
            }
        } else if constexpr (sizeof(ExtractedType) == 8) {
            // 64-bit extractor (double, int64_t) -> Use 64-bit Radix!
            if constexpr (sizeof(T) <= 16) {
                if (!detail::parallel_radix_sort_64bit(arr, fn)) fallback();
            } else {
                if (arr.size() < 100000) {
                    if (!detail::parallel_radix_sort_64bit(arr, fn)) fallback();
                } else {
                    fallback();
                }
            }
        } else {
            fallback();
        }
    }
}

/**
 * @brief Universal Sort without Functor (auto-detect type).
 */
template <typename T>
inline void sort(std::vector<T>& arr) {
    if constexpr (std::is_same_v<T, std::string>) {
        if (!detail::parallel_string_sort_impl(arr))
            std::sort(std::execution::par, arr.begin(), arr.end());
    } else if constexpr (std::is_arithmetic_v<T> && sizeof(T) <= 4) {
        if (!detail::parallel_radix_sort_8bit(arr, detail::DefaultKeyExtractor{}))
            std::sort(std::execution::par, arr.begin(), arr.end());
    } else if constexpr (std::is_arithmetic_v<T> && sizeof(T) == 8) {
        if (!detail::parallel_radix_sort_64bit(arr, detail::DefaultKeyExtractor{}))
            std::sort(std::execution::par, arr.begin(), arr.end());
    } else {
        std::sort(std::execution::par, arr.begin(), arr.end());
    }
}

} // namespace Sorting
} // namespace Algorithms
