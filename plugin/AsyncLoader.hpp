// Builds an object on a worker thread and hands it to the audio thread through a single
// atomic slot. Used for both the model and the IR. The builder runs off the audio thread;
// the audio thread only swaps pointers and never allocates, locks, or frees.
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

template <class T>
class AsyncLoader
{
public:
    using Builder = std::function<std::unique_ptr<T>()>;

    AsyncLoader()
        : mThread([this] { run(); })
    {
    }

    ~AsyncLoader()
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mQuit = true;
            mHasRequest = true;
        }
        mCv.notify_one();
        mThread.join();
        delete mReady.exchange(nullptr);
        delete mRetired.exchange(nullptr);
    }

    /// Non-RT. A null builder requests a clear (drop the active object).
    void request(Builder builder)
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mBuilder = std::move(builder);
            mHasRequest = true;
        }
        mCv.notify_one();
    }

    /// RT. Newly-built object (ownership transferred) or nullptr.
    T* popReady() noexcept { return mReady.exchange(nullptr); }

    /// RT. True once if the loader was asked to clear the active object.
    bool popClear() noexcept { return mClear.exchange(false); }

    /// RT. Hand the previous object back to be freed off the audio thread.
    void retire(T* obj) noexcept
    {
        if (obj == nullptr)
            return;
        if (T* leaked = mRetired.exchange(obj))
            delete leaked; // rare fallback (two swaps before the loader drained); avoids unbounded leak.
    }

private:
    void run()
    {
        for (;;)
        {
            Builder builder;
            {
                std::unique_lock<std::mutex> lock(mMutex);
                mCv.wait(lock, [this] { return mHasRequest; });
                if (mQuit)
                    return;
                mHasRequest = false;
                builder = std::move(mBuilder);
            }

            delete mRetired.exchange(nullptr); // free what the audio thread retired

            if (!builder)
            {
                mClear.store(true);
                delete mReady.exchange(nullptr);
                continue;
            }

            std::unique_ptr<T> obj = builder();
            if (obj) // a failed build leaves the active object untouched
                delete mReady.exchange(obj.release());
        }
    }

    std::thread mThread;
    std::mutex mMutex;
    std::condition_variable mCv;
    Builder mBuilder;
    bool mHasRequest = false;
    bool mQuit = false;

    std::atomic<T*> mReady{nullptr};
    std::atomic<T*> mRetired{nullptr};
    std::atomic<bool> mClear{false};
};
