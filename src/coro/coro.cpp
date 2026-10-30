#include <coroutine>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace std;

int* p;

struct task {
    struct promise_type {
        suspend_never initial_suspend() { std::cout << "Initial suspend" << std::endl; return {};}
        suspend_never final_suspend() noexcept { std::cout << "Final suspend" << std::endl; return{};}
        void return_void() {}
        task get_return_object() {
            std::cout << "return" << std::endl;
            return task(*this);
        }
        void unhandled_exception() {}
    };

    task(promise_type& pt) {
        h = Handle::from_promise(pt);
    }

    bool await_ready() { return true; }

    void await_suspend(std::coroutine_handle<> _h)
    {
        std::cout << "suspend" << std::endl;
        // return h;
    }

    void await_resume() {
        std::cout << "resume" << std::endl;
    }

    void resume() {
        h.resume();
    }

    using Handle = coroutine_handle<promise_type>;
    Handle h;
};

task task2() {
    std::cout << "run" << std::endl;
    co_return;
}

task coro_fun() {
    std::cout << "before" << std::endl;
    co_await task2();
    std::cout << "after" << std::endl;
}

int main() {
    task t = coro_fun();
}
