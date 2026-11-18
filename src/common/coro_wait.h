#pragma once

#include <coroutine>

using namespace std;

struct pause {
    coroutine_handle<> continuation;

    bool await_ready() {
        return false;
    }

    void await_suspend(coroutine_handle<> handle) {
        continuation = handle;
    }

    void await_resume() { }

    void resume() {
        continuation.resume();
    }
};
