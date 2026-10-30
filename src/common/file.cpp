#include "file.h"

#include <unistd.h>

File::File(int fd) : fd_(fd) {}

File::~File() {
    if (fd_ != -1) {
        close(fd_);
    }
}

File::File(File&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        if (fd_ != -1) close(fd_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

int File::operator()() const {
    return fd_;
}

int File::get() const {
    return fd_;
}
