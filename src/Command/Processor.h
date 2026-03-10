#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include <syncstream>

#include "Handler.h"
#include "Logs/Logs.h"

namespace Command
{
    class Processor
    {
        std::map<std::string, Function> _registry;
        std::queue<std::pair<Func, std::vector<std::string>>> _queue;
        std::mutex _mutex;
        std::condition_variable _cv;
        std::thread _worker;
        bool _running = true;

    public:
        Processor()
        {
            _worker = std::thread([this]() {
                while (_running)
                {
                    std::unique_lock<std::mutex> lock(_mutex);

                    _cv.wait(lock, [this] {
                        return !_queue.empty() || !_running;
                    });

                    if (!_running)
                    {
                        break;
                    }

                    auto [func, args] = _queue.front();
                    _queue.pop();
                    lock.unlock();
                    func(args);
                }
            });
        }

        void add(std::string name, std::string description, Func function, bool threaded = false)
        {
            _registry[name] =
            {
                description,
                function,
                threaded
            };
        }

        void execute(std::string& input)
        {
            std::stringstream stream(input);
            std::string name;

            stream >> name;

            if (_registry.find(name) == _registry.end())
            {
                std::osyncstream(std::cout) << RED << "Uknown Command: " << RESET << YELLOW << name << RESET << std::endl;
                std::osyncstream(std::cout) << "CMD] >>" << std::flush;
                
                return;
            }

            auto& cmd = _registry[name];
            std::vector<std::string> args;

            if (cmd.threaded)
            {
                std::lock_guard<std::mutex> lock(_mutex);

                _queue.push({
                    cmd.action,
                    args
                });

                _cv.notify_one();
            }
            else
            {
                cmd.action(args);
            }
        }

        ~Processor()
        {
            _running = false;
            _cv.notify_one();
            if (_worker.joinable())
            {
                _worker.join();
            }
        }
    };
}

#endif // COMMAND_PROCESSOR_H