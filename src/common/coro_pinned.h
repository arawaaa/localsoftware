#pragma once

#include <coroutine>
#include <optional>

#include "coro_task.h"
#include "async_thread_shared.h"

using namespace std;

template <typename T>
struct pinned : task {
    struct promise_type {
        coroutine_handle<> continuation;
        optional<T> return_obj;
        void return_value(T& returned) { return_obj.emplace(returned); }
        pinned<T> get_return_object() { return pinned<T>(*this); }
        suspend_always initial_suspend() noexcept { return {}; }
        suspend_never final_suspend() noexcept { return {}; }
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

    pinned operator=(pinned&& other) {
        handle = other.handle;
        other.handle = nullptr;
    }

    bool await_ready() { return false; }

    coroutine_handle<> await_suspend(coroutine_handle<> current) {
        handle.promise().continuation = current;
        return handle;
    }

    T await_resume() { return handle.promise().return_obj().value(); }

    /* We typically hand over control on co_await, but this is useful
     * when calling from root node, as we don't want coroutines in the runtime
     *
     * TODO: only for calling after initial suspend, will skip over any co_awaits
     */
    void resume() override {
        handle.resume();
    }

    using Handle = coroutine_handle<promise_type>;
    Handle handle;
};

// Can't specialize outside of a namespace scope, so we are not able to use a specialized promise type
// thus we specialize the whole task type
template <>
struct pinned<void> : task {
    struct promise_type {
        coroutine_handle<> continuation;
        // No return object!
        void return_void() {}
        pinned<void> get_return_object() { return pinned<void>(*this); }
        suspend_always initial_suspend() noexcept { return {}; }
        suspend_never final_suspend() noexcept { return {}; }
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

    bool await_ready() { return false; }

    coroutine_handle<> await_suspend(coroutine_handle<> current) {
        handle.promise().continuation = current;
        return handle;
    }

    void await_resume() { }

    /* We typically hand over control on co_await, but this is useful
     * when calling from root node, as we don't want coroutines in the runtime
     *
     * TODO: only for calling after initial suspend, will skip over any co_awaits
     */
    void resume() override {
        handle.resume();
    }

    using Handle = coroutine_handle<promise_type>;
    Handle handle;
};
