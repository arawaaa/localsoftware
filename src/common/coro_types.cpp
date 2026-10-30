#include "coro_types.h"

#include <functional>

#include "thread_data.h"

extern thread_local ThreadData* tdata;

readiness_api_awaitable::readiness_api_awaitable(function<void(io_uring_sqe*)> sfunc) {
    auto [uid, iter] = tdata->add_co_uring(sfunc, &ret);
    id = uid;
    it = iter;
}



int readiness_api_awaitable::await_resume() {
    return ret;
}
