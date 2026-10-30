#include <coroutine>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace std;

int* p;

struct task {
    struct promise_type {
        suspend_never initial_suspend() {return {};}
        suspend_never final_suspend() noexcept {return{};}
        void return_void() {}
        task get_return_object() {
            return task(*this);
        }
        void unhandled_exception() {}
    };

    task(promise_type& pt) {
        h = Handle::from_promise(pt);
    }

    void resume() {
        h.resume();
    }

    using Handle = coroutine_handle<promise_type>;
    Handle h;
};

auto switch_to_new_thread()
{
    struct awaitable
    {
        int a;
        bool await_ready() { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            p = &a;
        }
        int await_resume() { return a; }
    };
    return awaitable {};
}

task coro_fun() {
    std::cout << "before" << std::endl;
    int ret = co_await switch_to_new_thread();
    std::cout << ret << std::endl;
}

int main() {
    task t = coro_fun();
    *p = 3;
    t.resume();
}
