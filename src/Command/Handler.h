#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <iostream>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>

using Func = std::function<void(const std::vector<std::string>&)>;

namespace Command
{
    struct Function
    {
        std::string description;
        Func action;
        bool threaded;
    };
}

#endif // COMMAND_HANDLER_H
