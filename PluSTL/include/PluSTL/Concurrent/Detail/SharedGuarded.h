//
// Created by Plutex on 2026-08-07.
//

#ifndef PLUSTL_SHAREDGUARDED_H
#define PLUSTL_SHAREDGUARDED_H

#include <mutex>
#include <shared_mutex>
#include <utility>

namespace Plu
{
    namespace Detail
    {
        // ====================================================================
        // SharedGuarded<T> — one value behind a shared_mutex
        // ====================================================================
        //
        // The lock half of ConcurrentString, kept separate so the wrapper above it
        // is nothing but "the same API as the single-threaded type, one line per
        // method". Every accessor a derived class writes funnels through Read() or
        // Write(), so there is exactly one place where the mutex is taken and no
        // way to forget it.
        //
        // Read()/Write() return whatever the callback returns, **by value** — the
        // deduced `auto` strips the reference on purpose. Handing out a reference
        // into the guarded value would survive the lock (Concurrent.h, rule 1);
        // this way `Read([](const T& v) { return v.Something(); })` is safe by
        // construction, whatever Something() returns.
        //
        // A callback must not re-enter the object: the lock is not recursive.
        template<typename T>
        class SharedGuarded
        {
        public:
            SharedGuarded() = default;

            explicit SharedGuarded(const T& value) : mValue(value) {}
            explicit SharedGuarded(T&& value) : mValue(static_cast<T&&>(value)) {}

            SharedGuarded(const SharedGuarded&) = delete;
            SharedGuarded& operator=(const SharedGuarded&) = delete;
            SharedGuarded(SharedGuarded&&) = delete;
            SharedGuarded& operator=(SharedGuarded&&) = delete;

            // fn(const T&) under a shared lock — several readers may run at once.
            template<typename Fn>
            auto Read(Fn&& fn) const
            {
                std::shared_lock<std::shared_mutex> guard(mMutex);
                return fn(static_cast<const T&>(mValue));
            }

            // fn(T&) under an exclusive lock — the escape hatch for read-modify-write in
            // one step ("append this line, and flush if the buffer got big").
            template<typename Fn>
            auto Write(Fn&& fn)
            {
                std::unique_lock<std::shared_mutex> guard(mMutex);
                return fn(mValue);
            }

            // A copy of the guarded value.
            [[nodiscard]] T Get() const
            {
                std::shared_lock<std::shared_mutex> guard(mMutex);
                return mValue;
            }

            void Assign(const T& value)
            {
                std::unique_lock<std::shared_mutex> guard(mMutex);
                mValue = value;
            }

            void Assign(T&& value)
            {
                std::unique_lock<std::shared_mutex> guard(mMutex);
                mValue = static_cast<T&&>(value);
            }

            // Resets the value and returns what it held, in one critical section — the
            // "flush what accumulated and start over" primitive. Nothing another thread
            // wrote can slip through between the read and the reset.
            [[nodiscard]] T Take()
            {
                std::unique_lock<std::shared_mutex> guard(mMutex);
                T taken = static_cast<T&&>(mValue);
                mValue = T{};
                return taken;
            }

        private:
            mutable std::shared_mutex mMutex;
            T mValue;
        };
    }
}

#endif //PLUSTL_SHAREDGUARDED_H
