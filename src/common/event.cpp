#include "event.h"

#include <memory>
#include <vector>
#include <liburing.h>

#include "file.h"
#include "defs.h"

using namespace std;

Event::Event(vector<shared_ptr<File>> file) : files_(file) {
}

Event::Event() {
}
