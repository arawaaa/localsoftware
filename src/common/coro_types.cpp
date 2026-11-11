#include "coro_types.h"

#include <functional>

#include "thread_data.h"

extern thread_local ThreadData* tdata;

readiness_api_awaitable::readiness_api_awaitable(function<void(io_uring_sqe*)> sfunc)
{
    data = tdata->co_preferred();
    id = data->co_register(sfunc);
}

readiness_api_awaitable::readiness_api_awaitable(readiness_api_awaitable&& other)
    : id(other.id), data(other.data)
{
    other.data = nullptr;
}

readiness_api_awaitable& readiness_api_awaitable::operator=(readiness_api_awaitable&& other) {
    // Get rid of this handle
    data->co_deregister(id);
    id = other.id;
    data = other.data;

    return *this;
}

readiness_api_awaitable::~readiness_api_awaitable() {
    data->co_deregister(id);
}

void readiness_api_awaitable::await_suspend(coroutine_handle<> handle) {
    data->co_handle(id, handle);
    // data will be valid until destruction of this object
    // TODO if has been a while, thread will resume, and we will throw in the resume function
    data = data->co_preferred();
    // Fall off end, no coroutine to transfer to. Go to parent coroutine or per thread dispatcher
}

int readiness_api_awaitable::await_resume() {
    return data->co_result(id);
}
