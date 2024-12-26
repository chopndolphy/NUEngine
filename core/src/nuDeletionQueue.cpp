#include "nuDeletionQueue.h"

void nuDeletionQueue::push_function(std::function<void()>&& function) {
    _deletionQueue.push_back(function);
}

void nuDeletionQueue::flush() {
    for (auto it = _deletionQueue.rbegin() ; it != _deletionQueue.rend() ; it++) {
        (*it)();
    }

    _deletionQueue.clear();
}
