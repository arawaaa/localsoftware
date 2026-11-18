#pragma once

#include <coroutine>
#include <optional>
#include <mutex>
#include <atomic>

#include "coro_task.h"
#include "async_thread_shared.h"
#include "thread_data.h"

using namespace std;

// TODO Cancel on destroy semantics for .then() return object

struct Permission {
    Permission(unique_lock<mutex>&& lock) {
        lock = std::move(lock);
    }

    unique_lock<mutex> lock;
};

struct handoff {
    bool ready;
    coroutine_handle<> continuation;
    handoff(bool ready, coroutine_handle<> handle) noexcept: ready(ready), continuation(handle) { }

    bool await_ready() noexcept {
        return ready;
    }

    coroutine_handle<> await_suspend(coroutine_handle<> handle) noexcept {
        return continuation;
    }

    void await_resume() noexcept { }
};

template <typename T>
struct pinned_shared : task {
    struct promise_type {
        coroutine_handle<> continuation;
        optional<T> return_obj;
        void return_value(T& returned) { return_obj.emplace(returned); }
        pinned_shared<T> get_return_object() { return pinned_shared<T>(*this); }
        suspend_always initial_suspend() noexcept { return {}; }
        handoff final_suspend() noexcept { return handoff(static_cast<bool>(continuation), continuation); }
        void unhandled_exception() {}
    };

    pinned_shared(promise_type& promise) {
        handle = Handle::from_promise(promise);
    }

    ~pinned_shared() {
        handle.destroy();
    }

    bool await_ready() {
        lock_guard<mutex> lu(clas);
        switch (state) {
            case State::Empty:
                // Skip suspension and get constructible object
                state = State::Wait;
                return true;
            case State::Fulfillment:
            case State::Wait:
                return false;
        }
    }

    coroutine_handle<> await_suspend(coroutine_handle<> current) {
        handle.promise().continuation = current;
        return handle;
    }

    variant<T, Permission> await_resume() {
        unique_lock<mutex> lu(clas);
        switch (state) {
            case State::Empty:
                // Skip suspension and get constructible object
                state = State::Wait;
                return true;
            case State::Fulfillment:
                return handle.promise().return_obj().value();
            case State::Wait:
                return Permission(std::move(lu));
        }
    }

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

private:
    enum class State {
        Empty,
        Fulfillment,
        Wait
    };
    State state;
    mutex clas;
    list<pair<ThreadData*, coroutine_handle<>>> waiting;
    pinned_shared(pinned_shared& other) {

    };

    pinned_shared operator=(pinned_shared& other) {

    };
};

// Can't specialize outside of a namespace scope, so we are not able to use a specialized promise type
// thus we specialize the whole task type
template <>
struct pinned_shared<void> : task {
    struct promise_type {
        coroutine_handle<> continuation;
        // No return object!
        void return_void() {}
        pinned_shared<void> get_return_object() { return pinned_shared<void>(*this); }
        suspend_always initial_suspend() noexcept { return {}; }
        handoff final_suspend() noexcept { return handoff(static_cast<bool>(continuation), continuation); }
        void unhandled_exception() {}
    };

    pinned_shared() {
        handle = coroutine_handle<noop_coroutine_handle>();
    }

    pinned_shared(promise_type& promise) {
        handle = Handle::from_promise(promise);
    }

    pinned_shared(pinned_shared& other) = delete;
    pinned_shared operator=(pinned_shared& other) = delete;

    pinned_shared(pinned_shared&& other) {
        handle = other.handle;
        other.handle = nullptr;
    }

    pinned_shared& operator=(pinned_shared&& other) {
        handle = other.handle;
        other.handle = nullptr;
        return *this;
    }

    ~pinned_shared() { }

    bool await_ready() { return false; }

    coroutine_handle<> await_suspend(coroutine_handle<> current) {
        //
        if (!handle)
            return coroutine_handle<noop_coroutine_handle> {};

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
