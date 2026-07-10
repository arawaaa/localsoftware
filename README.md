# All in one server

An all in one C++ server to serve my personal webpage, monitor bridge state, participate in home automation. Very specific to my home automation setup. Also includes scripts and systemd service configurations to enable the 802.11<->802.3 bridge. Contains a authenticated reverse proxy implementation for OpenClaw access (instead of a tailscale configuration as suggested).

The initial architecture was centered around the IoEvent class, through overriding its virtual on_new_data function on io_uring events. With implementing more complex classes that would require multiple levels of inheritance for a clean implementation, this needed an additional virtual method that ran before on_new_data, and would modify the input to it. A class would inherit from another, which wrapped io_uring calls. This allowed for a structure of IoEvent -> TLS -> HTTP class. However, this was not clean, and did not allow event-emitting chains of more than 2, and had no possibility of calling distinct event-emitting functions.

A separate IoUringManager class was responsible for submitting SQEs and listening for CQEs, on which it would use the associated data pointer to associate the completion to a particular class, and call its on_new_data function.

The HTTP class, which contained http parsing logic, as well as accept, and which inherited for a TLS wrapper class, which also implemented read-all and write-all behavior for a socket, was split up into multiple classes, and IoUringManager was given the ability to also queue function calls across separate subclasses of IoEvent.

The initial design of IoUringManager did not allow multithreading, since it was centered around a single ring, used synchronous operations across classes and had synchronous logic for marking completion and deleting child objects.

IoUringManager is renamed AsyncManager, and per-thread event queueing and ring maintenance are now within ThreadData, and IoEvent to Event. Event now queues function calls, deletions and constructions within itself, and these queued commands are processed by ThreadData after the construct or on_yield (replacing on_new_data) is finished.

In the short term, the use of TLS will be made more efficient through custom BIOs, which will reduce a spurious copy that currently occurs with the usage of BIO_mem. Following that, kTLS will be implemented, reducing the number of copies further, especially for static serving.

Progress on events is currently tracked by proccess IDs associated to object IDs. Eventually, process IDs will be an internal only handle, and consumers will attach to yields/completions/errors through a Promise-like object. Furthermore, object IDs / child vector indices will be replaced with an RAII object which will queue constructions on initializations, and queue remote deletions on object destruction, as well as have methods reflected from the actual object, that return Promises.

An interesting cache, Tiny-LFU will also be implemented to optimize serving static files. This is a type of two-tier cache, utilizing a data structure called a Count-min-sketch, which is similar to a bloom filter.

All code written after f5a178b is entirely hand-written, including all of the OpenSSL-based support for TLS.

## Compilation
Compilation and transfer commands are in redeploy.sh. It's currently a unity build, the cmake doesn't actually work :(. I wish I could use c++ modules, but cmake doesn't support header units yet and compilation catastrophically breaks with including beast in the global module fragment. 

Dependencies required:
- Boost.{Beast, Json}
- oneTBB
- openSSL

Requires Boost, C++26 and liburing, and {npm, mui, react} for the frontend.

PDF copies of my blog posts are in the frontend/public directory.
