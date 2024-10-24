#pragma once

#include <functional>
#include <deque>

class nuDeletionQueue {
    public:
        void push_function(std::function<void()>&& function); // inefficient at scale...
        void flush();
        
    private:
        std::deque<std::function<void()>> _deletionQueue;

};
