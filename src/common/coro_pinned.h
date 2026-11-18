#pragma once

#include <coroutine>
#include <optional>

#include "coro_task.h"
#include "async_thread_shared.h"

using namespace std;

// Class is not thread-safe, use pinned_shared for multithreading

struct handoff {
    bool destroy_on_end;
    coroutine_handle<> continuation;
    handoff(bool destroy_on_end, coroutine_handle<> handle) noexcept: destroy_on_end(destroy_on_end), continuation(handle) { }

    bool await_ready() noexcept {
        return destroy_on_end;
    }

    coroutine_handle<> await_suspend(coroutine_handle<> handle) noexcept {
        if (continuation)
            return continuation;
        else
            return noop_coroutine();
    }

    void await_resume() noexcept { }
};

template <typename T>
struct promise_type;

template <typename T>
struct pinned : task {
    struct promise_type {
        coroutine_handle<> continuation;
        bool destroy_on_end = false;
        optional<T> return_obj;
        void return_value(T returned) { return_obj.emplace(std::move(returned)); }
        pinned<T> get_return_object() { return pinned<T>(*this); }
        suspend_always initial_suspend() noexcept { return {}; }
        handoff final_suspend() noexcept { return handoff(destroy_on_end, continuation); }
        void unhandled_exception() {}
    };

    pinned(promise_type& promise) {
        handle = Handle::from_promise(promise);
    }

    pinned(pinned& other) = delete;
    pinned operator=(pinned& other) = delete;

    pinned(pinned&& other) {
        handle = other.handle;
        other.handle = nullptr;
    }

    pinned& operator=(pinned&& other) {
        handle = other.handle;
        other.handle = nullptr;
        return *this;
    }

    ~pinned() {
        if (handle)
            handle.destroy();
    }

    bool await_ready() { return false; }

    coroutine_handle<> await_suspend(coroutine_handle<> current) {
        handle.promise().continuation = current;
        return handle;
    }

    T await_resume() { return std::move(handle.promise().return_obj.value()); }

    /* We typically hand over control on co_await, but this is useful
     * when calling from root node, as we don't want coroutines in the runtime
     *
     * TODO: only for calling after initial suspend, will skip over any co_awaits
     */
    void resume() override {
        handle.resume();
    }

    /*
     * Coroutine is either finished (and suspended) or it is not finished.
     * Either way, it is still observable. We delete if it is done, and
     * stop managing its lifetime if it is not, as well as tell it to clean itself
     * up.
     */
    void detach() {
        if (handle && handle.done()) {
            handle.destroy();
            handle = nullptr;
        } else if (handle) {
            handle.promise().destroy_on_end = true;
            handle = nullptr;
        }
    }

    // Will execute on same thread as caller of this function
    pinned<void> then(function<void(T)> on_completion);

    using Handle = coroutine_handle<promise_type>;
    Handle handle;
};

// Can't specialize outside of a namespace scope, so we are not able to use a specialized promise type
// thus we specialize the whole task type
template <>
struct pinned<void> : task {
    struct promise_type {
        coroutine_handle<> continuation;
        bool destroy_on_end = false;
        // No return object!
        void return_void() { }
        pinned<void> get_return_object() { return pinned<void>(*this); }
        suspend_always initial_suspend() noexcept { return {}; }
        handoff final_suspend() noexcept {
            return handoff(destroy_on_end, continuation);
        }
        void unhandled_exception() {}
    };

    pinned(promise_type& promise) {
        handle = Handle::from_promise(promise);
    }

    pinned(pinned& other) = delete;
    pinned operator=(pinned& other) = delete;

    pinned(pinned&& other) {
        handle = other.handle;
        other.handle = nullptr;
    }

    pinned& operator=(pinned&& other) {
        handle = other.handle;
        other.handle = nullptr;
        return *this;
    }

    ~pinned() {
        if (handle)
            handle.destroy();
    }

    bool await_ready() { return false; }

    coroutine_handle<> await_suspend(coroutine_handle<> current) {
        handle.promise().continuation = current;
        return handle;
    }

    void await_resume() { }

    void resume() override {
        handle.resume();
    }

    void detach() {
        if (handle && handle.done()) {
            handle.destroy();
            handle = nullptr;
        } else if (handle) {
            handle.promise().destroy_on_end = true;
            handle = nullptr;
        }
    }

    pinned<void> then(function<void()> on_completion);

    using Handle = coroutine_handle<promise_type>;
    Handle handle;
};

template <typename T>
pinned<void> pinned<T>::then(function<void(T)> on_completion) {
    auto task = [](function<void(T)> completion_cb, pinned<T> task) mutable -> pinned<void> {
        completion_cb(co_await task);
    }(std::move(on_completion), std::move(*this));

    task.resume();

    return task;
}
