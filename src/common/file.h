#pragma once

#include <unistd.h>

/**
 * @brief Convenience class to encapsulate a file descriptor.
 * Handles automatic closure of the descriptor upon destruction.
 */
class File {
public:
    explicit File(int fd);
    
    ~File();

    // Move-only semantics
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    
    File(File&& other) noexcept;
    
    File& operator=(File&& other) noexcept;

    /**
     * @brief Overloaded operator() to retrieve the raw file descriptor.
     */
    int operator()() const;

    int get() const;

private:
    int fd_;
};
