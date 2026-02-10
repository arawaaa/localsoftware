#pragma once

#include <unistd.h>

/**
 * @brief Convenience class to encapsulate a file descriptor.
 * Handles automatic closure of the descriptor upon destruction.
 */
class File {
public:
    explicit File(int fd) : fd_(fd) {}
    
    ~File() {
        if (fd_ != -1) {
            close(fd_);
        }
    }

    // Move-only semantics
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    
    File(File&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    
    File& operator=(File&& other) noexcept {
        if (this != &other) {
            if (fd_ != -1) close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    /**
     * @brief Overloaded operator() to retrieve the raw file descriptor.
     */
    int operator()() const {
        return fd_;
    }

    int get() const {
        return fd_;
    }

private:
    int fd_;
};
