#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================================
// ЧАСТИНА 1. Відтворення та усунення Race Condition (Варіант 18: N = 18)
// ============================================================================
constexpr int ITERATIONS = 18 * 10000; // 180 000 ітерацій на потік

int sharedCounter = 0;
std::mutex counterMutex;
std::atomic<int> atomicCounter{0};

void incrementNoSync() {
    for (int i = 0; i < ITERATIONS; ++i) {
        int tmp = sharedCounter;
        asm volatile("" : "+r"(tmp)); // Запобігає згортанню циклу оптимізатором -O2
        sharedCounter = tmp + 1;
    }
}

void incrementMutex() {
    for (int i = 0; i < ITERATIONS; ++i) {
        std::lock_guard<std::mutex> lock(counterMutex);
        ++sharedCounter;
    }
}

void incrementAtomic() {
    for (int i = 0; i < ITERATIONS; ++i) {
        ++atomicCounter;
    }
}

void runPart1() {
    std::cout << "=== ЧАСТИНА 1. Race Condition (Варіант 18, ITERATIONS = " << ITERATIONS << ") ===\n";
    const int expected = 2 * ITERATIONS;
    int mismatches = 0;

    for (int run = 1; run <= 10; ++run) {
        sharedCounter = 0;
        std::thread t1(incrementNoSync);
        std::thread t2(incrementNoSync);
        t1.join();
        t2.join();
        if (sharedCounter != expected) {
            ++mismatches;
        }
        std::cout << "  [Без синхронізації] Запуск " << std::right << std::setw(2) << run
                  << ": Expected = " << expected << ", Actual = " << sharedCounter << '\n';
    }
    std::cout << "  Втрата оновлень зафіксована у " << mismatches << " з 10 запусків.\n";

    double mutexMs = 0.0;
    for (int run = 1; run <= 5; ++run) {
        sharedCounter = 0;
        auto t0 = std::chrono::steady_clock::now();
        std::thread t1(incrementMutex);
        std::thread t2(incrementMutex);
        t1.join();
        t2.join();
        auto t1_time = std::chrono::steady_clock::now();
        mutexMs += std::chrono::duration<double, std::milli>(t1_time - t0).count();
    }
    mutexMs /= 5.0;
    std::cout << "  [std::mutex]         Actual = " << sharedCounter
              << " | Середній час (5 запусків): " << std::fixed << std::setprecision(2) << mutexMs << " мс\n";

    double atomicMs = 0.0;
    for (int run = 1; run <= 5; ++run) {
        atomicCounter.store(0);
        auto t0 = std::chrono::steady_clock::now();
        std::thread t1(incrementAtomic);
        std::thread t2(incrementAtomic);
        t1.join();
        t2.join();
        auto t1_time = std::chrono::steady_clock::now();
        atomicMs += std::chrono::duration<double, std::milli>(t1_time - t0).count();
    }
    atomicMs /= 5.0;
    std::cout << "  [std::atomic<int>]   Actual = " << atomicCounter.load()
              << " | Середній час (5 запусків): " << std::fixed << std::setprecision(2) << atomicMs << " мс\n\n";
}

// ============================================================================
// ЧАСТИНА 2. Відтворення та усунення Deadlock (C++20 std::barrier)
// ============================================================================
std::mutex mutexA;
std::mutex mutexB;
std::barrier syncPoint(2);

// Відтворення взаємного блокування (запускається з прапорцем --deadlock)
void runPart2Deadlock() {
    std::cout << "=== ЧАСТИНА 2.1. Відтворення Deadlock (зупиніть через Ctrl+C) ===\n";
    std::thread t1([]() {
        std::cout << "Thread 1: locking mutexA" << std::endl;
        std::lock_guard<std::mutex> lockA(mutexA);
        syncPoint.arrive_and_wait();
        std::cout << "Thread 1: waiting for mutexB" << std::endl;
        std::lock_guard<std::mutex> lockB(mutexB);
    });
    std::thread t2([]() {
        std::cout << "Thread 2: locking mutexB" << std::endl;
        std::lock_guard<std::mutex> lockB(mutexB);
        syncPoint.arrive_and_wait();
        std::cout << "Thread 2: waiting for mutexA" << std::endl;
        std::lock_guard<std::mutex> lockA(mutexA);
    });
    t1.join();
    t2.join();
}

void runPart2Fixed() {
    std::cout << "=== ЧАСТИНА 2. Усунення Deadlock (std::scoped_lock та єдиний порядок) ===\n";
    // Спосіб 1: одночасне атомарне захоплення через std::scoped_lock
    {
        std::thread t1([]() {
            syncPoint.arrive_and_wait();
            std::scoped_lock lock(mutexA, mutexB);
            std::cout << "  [std::scoped_lock] Thread 1: обидва м'ютекси захоплено без дедлоку\n";
        });
        std::thread t2([]() {
            syncPoint.arrive_and_wait();
            std::scoped_lock lock(mutexB, mutexA);
            std::cout << "  [std::scoped_lock] Thread 2: обидва м'ютекси захоплено без дедлоку\n";
        });
        t1.join();
        t2.join();
    }
    // Спосіб 2: єдиний порядок захоплення м'ютексів (mutexA -> mutexB)
    {
        std::thread t1([]() {
            syncPoint.arrive_and_wait();
            std::lock_guard<std::mutex> lockA(mutexA);
            std::lock_guard<std::mutex> lockB(mutexB);
            std::cout << "  [Єдиний порядок]   Thread 1: mutexA -> mutexB захоплено коректно\n";
        });
        std::thread t2([]() {
            syncPoint.arrive_and_wait();
            std::lock_guard<std::mutex> lockA(mutexA);
            std::lock_guard<std::mutex> lockB(mutexB);
            std::cout << "  [Єдиний порядок]   Thread 2: mutexA -> mutexB захоплено коректно\n";
        });
        t1.join();
        t2.join();
    }
    std::cout << '\n';
}

// ============================================================================
// ЧАСТИНА 3. Producer-Consumer (Варіант 18: Моніторинг погодних станцій)
// ============================================================================
struct WeatherPacket {
    int id;
    std::string station;
    double rainMm;
    double windMs;
    double pressureHpa;
};

void runPart3() {
    std::cout << "=== ЧАСТИНА 3. Producer-Consumer (Варіант 18: Моніторинг погодних станцій) ===\n";
    std::queue<WeatherPacket> q;
    std::mutex qMtx;
    std::mutex outMtx;
    std::condition_variable cv;
    bool finished = false;
    std::atomic<int> processed{0};
    std::atomic<int> anomalies{0};

    std::thread producer([&]() {
        const std::vector<WeatherPacket> data = {
            {1,  "Kyiv-North",      0.2,  4.5, 1013.2},
            {2,  "Rivne-West",     18.4, 11.0, 1004.5}, // Аномалія: злива (>15 мм/год)
            {3,  "Odesa-Port",      1.0, 24.8,  998.0}, // Аномалія: шторм (>20 м/с)
            {4,  "Lviv-Center",     3.5,  6.2, 1016.0},
            {5,  "Kharkiv-East",    0.0,  5.1,  981.4}, // Аномалія: низький тиск (<985 гПа)
            {6,  "KryvyiRih-South", 0.5,  7.4, 1011.8},
            {7,  "Dnipro-River",   21.0, 22.5,  983.0}, // Аномалія: комплексна
            {8,  "Poltava-Field",   1.2,  3.8, 1018.5},
            {9,  "Vinnytsia-Hill",  0.0,  4.0, 1038.2}, // Аномалія: високий тиск (>1035 гПа)
            {10, "Zhytomyr-Forest", 4.1,  8.3, 1009.0},
            {11, "Chernihiv-North", 0.8, 12.1, 1012.4},
            {12, "Sumy-Border",    16.2, 19.5,  995.6}  // Аномалія: злива (>15 мм/год)
        };
        for (const auto& pkt : data) {
            {
                std::lock_guard<std::mutex> lock(qMtx);
                q.push(pkt);
            }
            cv.notify_one();
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        {
            std::lock_guard<std::mutex> lock(qMtx);
            finished = true;
        }
        cv.notify_all();
    });

    auto consumer = [&](int id) {
        while (true) {
            WeatherPacket pkt;
            {
                std::unique_lock<std::mutex> lock(qMtx);
                cv.wait(lock, [&]() { return !q.empty() || finished; });
                if (q.empty() && finished) {
                    break;
                }
                pkt = q.front();
                q.pop();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            std::string alert;
            if (pkt.rainMm > 15.0) alert += "ЗЛИВА; ";
            if (pkt.windMs > 20.0) alert += "ШТОРМ; ";
            if (pkt.pressureHpa < 985.0 || pkt.pressureHpa > 1035.0) alert += "ТИСК; ";

            if (!alert.empty()) {
                ++anomalies;
            }
            ++processed;

            std::lock_guard<std::mutex> lock(outMtx);
            std::cout << "  [Consumer " << id << "] #" << std::right << std::setw(2) << pkt.id
                      << " " << std::left << std::setw(16) << pkt.station
                      << " -> " << (alert.empty() ? "Норма" : ("АНОМАЛІЯ (" + alert + ")")) << '\n';
        }
    };

    std::thread c1(consumer, 1);
    std::thread c2(consumer, 2);
    producer.join();
    c1.join();
    c2.join();

    std::cout << "  Підсумок: оброблено " << processed.load() << "/12 пакетів, виявлено аномалій: "
              << anomalies.load() << "\n\n";
}

// ============================================================================
// ЧАСТИНА 4. std::async та std::future (Варіант 18: множення векторів + сума)
// ============================================================================
double seqVecMulSum(const std::vector<double>& a, const std::vector<double>& b) {
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

double parVecMulSum(const std::vector<double>& a, const std::vector<double>& b, std::size_t tasks) {
    std::vector<std::future<double>> futures;
    futures.reserve(tasks);
    std::size_t chunk = a.size() / tasks;
    for (std::size_t t = 0; t < tasks; ++t) {
        std::size_t begin = t * chunk;
        std::size_t end = (t + 1 == tasks) ? a.size() : (begin + chunk);
        futures.push_back(std::async(std::launch::async, [&a, &b, begin, end]() {
            double local = 0.0;
            for (std::size_t i = begin; i < end; ++i) {
                local += a[i] * b[i];
            }
            return local;
        }));
    }
    double total = 0.0;
    for (auto& f : futures) {
        total += f.get();
    }
    return total;
}

void runPart4() {
    std::cout << "=== ЧАСТИНА 4. std::async / std::future (Варіант 18) ===\n";
    std::cout << std::left << std::setw(12) << "Size N" << std::setw(9) << "Tasks"
              << std::setw(13) << "T_seq (ms)" << std::setw(13) << "T_par (ms)"
              << std::setw(12) << "Speedup S" << "Check\n";
    std::cout << std::string(65, '-') << '\n';

    for (std::size_t n : {1'000'000ULL, 10'000'000ULL, 50'000'000ULL}) {
        std::vector<double> a(n, 1.25), b(n, 2.40);

        auto t0 = std::chrono::steady_clock::now();
        double sRes = seqVecMulSum(a, b);
        auto t1 = std::chrono::steady_clock::now();
        double sMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        for (std::size_t k : {2ULL, 4ULL, 8ULL}) {
            auto p0 = std::chrono::steady_clock::now();
            double pRes = parVecMulSum(a, b, k);
            auto p1 = std::chrono::steady_clock::now();
            double pMs = std::chrono::duration<double, std::milli>(p1 - p0).count();
            bool ok = std::abs(sRes - pRes) <= 1e-4 * std::abs(sRes);

            std::cout << std::left << std::fixed << std::setprecision(3)
                      << std::setw(12) << n << std::setw(9) << k
                      << std::setw(13) << sMs << std::setw(13) << pMs
                      << std::setw(12) << (sMs / pMs) << (ok ? "OK" : "DIFF") << '\n';
        }
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif
    if (argc > 1 && std::string(argv[1]) == "--deadlock") {
        runPart2Deadlock();
        return 0;
    }

    runPart1();
    runPart2Fixed();
    runPart3();
    runPart4();

    std::cout << "\nНатисніть Enter для завершення...";
    std::cin.get();
    return 0;
}
