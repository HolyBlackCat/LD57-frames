#include "critical_error.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

namespace em
{
    static std::mutex entries_mutex;

    // By default this contains one function that logs the error to `stderr`.
    std::list<CriticalErrorHandler::Entry> CriticalErrorHandler::list = []{
        std::list<CriticalErrorHandler::Entry> ret;
        ret.emplace_back([](zstring_view message)
        {
            std::fprintf(stderr, "Critical error: %s\n", message.c_str());
        });
        return ret;
    }();

    // `message` MUST be null-terminated here.
    // This would be `static`, but we must friend it in `CriticalErrorHandler` and you can't friend static functions.
    [[noreturn]] void CriticalError(zstring_view message)
    {
        { // Handle this function being reentered.
            static std::atomic<std::thread::id> global_id{};
            const std::thread::id this_id = std::this_thread::get_id();
            std::thread::id existing_id{};
            if (!global_id.compare_exchange_strong(existing_id, this_id, std::memory_order_relaxed))
            {
                // Some thread has already entered this function.

                if (existing_id == this_id)
                {
                    // It was the same thread as this one. This shouldn't happen, kill the application.
                    std::abort();
                }
                else
                {
                    // It was a different thread. Deadlock this one and let the first one show its error.
                    while (true)
                        std::this_thread::sleep_for(std::chrono::hours(1));
                }
            }
        }

        { // Run the handlers.
            std::lock_guard _(entries_mutex);
            for (const auto &elem : CriticalErrorHandler::list)
                elem.func(message);
        }

        // Lastly, kill the application.
        std::abort();
    }

    CriticalErrorHandler::CriticalErrorHandler(Func func)
    {
        std::lock_guard _(entries_mutex);
        list.emplace_front(std::move(func));
        iter = list.begin();
    }

    CriticalErrorHandler::CriticalErrorHandler(CriticalErrorHandler &&other) noexcept
    {
        iter = std::move(other.iter);
        other.iter.reset();
    }

    CriticalErrorHandler &CriticalErrorHandler::operator=(CriticalErrorHandler other) noexcept
    {
        std::swap(iter, other.iter);
        return *this;
    }

    CriticalErrorHandler::~CriticalErrorHandler()
    {
        if (!iter)
            return;
        std::lock_guard _(entries_mutex);
        list.erase(*iter);
    }
}
