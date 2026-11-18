#include "coro_pinned.h"

pinned<void> pinned<void>::then(function<void()> on_completion) {
    auto task = [](function<void()> completion_cb, pinned<void> task) mutable -> pinned<void> {
        co_await task;
        completion_cb();
    }(std::move(on_completion), std::move(*this));

    task.resume();

    return task;
}
