#ifndef SLICEFORGE_SLICING_PIPELINE_H
#define SLICEFORGE_SLICING_PIPELINE_H

// =============================================================================
// SlicingPipeline — Multithreaded Parallel Slicing
// =============================================================================
//
// This is where we get the ~60% speedup mentioned on the resume.
//
// KEY INSIGHT: Each layer is independent. The contour at z=5.0mm doesn't
// depend on the contour at z=4.8mm. This makes slicing "embarrassingly
// parallel" — we can compute all layers simultaneously without any
// synchronization between them.
//
// ARCHITECTURE: Thread Pool Pattern
//
// Instead of creating a new thread for each layer (expensive — thread
// creation involves a system call), we create a fixed pool of worker
// threads at startup and distribute layers among them.
//
//   Main Thread                    Thread Pool
//   ──────────                    ───────────
//   Create N workers ──────────→  Worker 1: idle
//                                 Worker 2: idle
//                                 Worker 3: idle
//                                 Worker 4: idle
//
//   Push 200 layer tasks ──────→  Worker 1: slice z=0.1
//                                 Worker 2: slice z=0.3
//                                 Worker 3: slice z=0.5
//                                 Worker 4: slice z=0.7
//                                 Worker 1: slice z=0.9 (finished 0.1, picks up next)
//                                 ...
//
//   Wait for all complete ←─────  All workers: done
//
// WHY THIS MATTERS FOR THE INTERVIEW:
// Both roles list "multithreaded" as a key skill. Be ready to explain:
// - Why slicing is parallelizable (layers are independent)
// - Thread pool vs creating threads per task (overhead)
// - How mutex protects shared results vector
// - What a condition variable does (signals waiting threads)
// - Potential issues: false sharing, lock contention

#include "Slicer.h"
#include "Mesh.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <future>
#include <chrono>

namespace SliceForge {

// Simple thread pool implementation
class ThreadPool {
public:
    // Create a pool with the specified number of threads.
    // Default: use as many threads as the CPU has cores.
    explicit ThreadPool(size_t numThreads = 0) : stop_(false) {
        if (numThreads == 0) {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 4; // Fallback
        }

        for (size_t i = 0; i < numThreads; i++) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            stop_ = true;
        }
        condition_.notify_all(); // Wake up all waiting workers
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join(); // Wait for each worker to finish
            }
        }
    }

    // Submit a task and get a future for the result.
    //
    // std::future is like a "promise of a result" — you can call
    // future.get() later to retrieve the result once the task completes.
    // If the task threw an exception, future.get() re-throws it.
    template<typename Func>
    auto submit(Func&& f) -> std::future<decltype(f())> {
        using ReturnType = decltype(f());

        // Package the function into a shared_ptr<packaged_task>
        // so we can store it in the queue and retrieve its future.
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::forward<Func>(f)
        );

        std::future<ReturnType> future = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (stop_) {
                throw std::runtime_error("Cannot submit to stopped thread pool");
            }
            // Wrap in a void() lambda so all tasks have the same type
            tasks_.push([task]() { (*task)(); });
        }

        condition_.notify_one(); // Wake up one waiting worker
        return future;
    }

    size_t threadCount() const { return workers_.size(); }

private:
    // Each worker thread runs this loop forever:
    // 1. Wait for a task to appear in the queue
    // 2. Pop it and execute it
    // 3. Go back to waiting
    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                // Wait until there's a task OR we're told to stop.
                // The lambda is the "predicate" — we only wake up if
                // it returns true (prevents spurious wakeups).
                condition_.wait(lock, [this] {
                    return stop_ || !tasks_.empty();
                });

                if (stop_ && tasks_.empty()) return; // Shutdown

                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task(); // Execute outside the lock
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queueMutex_;
    std::condition_variable condition_;
    bool stop_;
};


// The main pipeline that orchestrates multithreaded slicing
class SlicingPipeline {
public:
    struct Config {
        float layerHeight = 0.2f;  // mm between layers (typical for FDM printing)
        size_t numThreads = 0;     // 0 = auto-detect CPU cores
    };

    struct Result {
        std::vector<Layer> layers;
        double elapsedMs;          // Total time in milliseconds
        size_t threadsUsed;
    };

    // Slice the mesh using multiple threads
    static Result sliceParallel(const Mesh& mesh, const Config& config) {
        if (mesh.empty()) {
            throw std::runtime_error("Cannot slice an empty mesh");
        }

        auto startTime = std::chrono::high_resolution_clock::now();

        // Calculate all Z heights we need to slice at
        const auto& bounds = mesh.getBounds();
        float startZ = bounds.min.z + config.layerHeight / 2.0f;
        float endZ = bounds.max.z;

        std::vector<float> zHeights;
        for (float z = startZ; z < endZ; z += config.layerHeight) {
            zHeights.push_back(z);
        }

        if (zHeights.empty()) {
            throw std::runtime_error("Model is too thin for the given layer height");
        }

        // Create thread pool and submit all layers as tasks
        ThreadPool pool(config.numThreads);
        std::vector<std::future<Layer>> futures;
        futures.reserve(zHeights.size());

        for (float z : zHeights) {
            // Each task captures the mesh by reference (it's read-only,
            // so sharing is safe without locks) and the z value by copy.
            futures.push_back(pool.submit([&mesh, z]() {
                return Slicer::sliceAtZ(mesh, z);
            }));
        }

        // Collect results — future.get() blocks until that layer is done
        Result result;
        result.threadsUsed = pool.threadCount();
        result.layers.reserve(zHeights.size());

        for (auto& future : futures) {
            result.layers.push_back(future.get());
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        result.elapsedMs = std::chrono::duration<double, std::milli>(
            endTime - startTime
        ).count();

        return result;
    }

    // Slice single-threaded for comparison / benchmarking
    static Result sliceSequential(const Mesh& mesh, const Config& config) {
        auto startTime = std::chrono::high_resolution_clock::now();

        std::vector<Layer> layers = Slicer::sliceAll(mesh, config.layerHeight);

        auto endTime = std::chrono::high_resolution_clock::now();

        Result result;
        result.layers = std::move(layers);
        result.threadsUsed = 1;
        result.elapsedMs = std::chrono::duration<double, std::milli>(
            endTime - startTime
        ).count();

        return result;
    }
};

} // namespace SliceForge

#endif // SLICEFORGE_SLICING_PIPELINE_H
