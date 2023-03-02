#ifndef _THREADPOOL_HPP
#define _THREADPOOL_HPP

#include <boost/thread.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

namespace tp {
    enum class CommandType {
        Quit,
        Execute
    };

    enum class CommandStatus {
        Running,
        Success,
        Failure
    };

    class Command {
    private:
        friend class ThreadPool;
        boost::mutex mutex;
        boost::condition_variable condition;

    public:
        CommandType type;
        CommandStatus status = CommandStatus::Running;
        void* data = nullptr; // User data

        Command() = default;

        Command(CommandType type, void* data = nullptr):
            type(type),
            data(data) {}

        CommandStatus await() {
            boost::unique_lock<boost::mutex> lock(mutex);
            while (status == CommandStatus::Running) {
                condition.wait(lock);
            }
            return status;
        }
    };

    class CommandExecute: public Command {
    public:
        std::function<void(void*)> func;
        std::exception error;
        void* arg = nullptr;

        CommandExecute(std::function<void(void*)> func, void* arg = nullptr, void* data = nullptr):
            func(std::move(func)),
            arg(arg) {
            type = CommandType::Execute;
            this->data = data;
        }
    };

    using Task = CommandExecute;

    class ThreadPool {
    protected:
        struct CommandQueue {
            std::queue<std::shared_ptr<Command>> queue;
            boost::mutex mutex;
            boost::condition_variable condition;
        };

        void runner(CommandQueue* commands) {
            for (;;) {
                boost::unique_lock<boost::mutex> lock(commands->mutex);
                while (commands->queue.empty()) {
                    commands->condition.wait(lock);
                }
                std::shared_ptr<Command> command = std::move(commands->queue.front());
                commands->queue.pop();
                lock.unlock();

                switch (command->type) {
                    default: {
                        throw std::runtime_error("Invalid command type");
                    }

                    case CommandType::Execute: {
                        auto cmd = (CommandExecute*) command.get();
                        try {
                            cmd->func(cmd->arg);

                            boost::unique_lock<boost::mutex> lock(cmd->mutex);
                            cmd->status = CommandStatus::Success;
                        } catch (const std::exception& e) {
                            boost::unique_lock<boost::mutex> lock(cmd->mutex);
                            cmd->status = CommandStatus::Failure;
                            cmd->error = e;
                        }

                        cmd->condition.notify_all();
                        break;
                    }

                    case CommandType::Quit: {
                        return;
                    }
                }
            }
        }

        std::vector<std::pair<boost::thread, CommandQueue*>> threads;
        unsigned int sched_counter = 0;

    public:
        ThreadPool(unsigned int pool_size = boost::thread::hardware_concurrency()) {
            for (unsigned int i = 0; i < pool_size; i++) {
                auto new_queue = new CommandQueue;
                boost::thread new_thread(&ThreadPool::runner, this, new_queue);
                threads.push_back({std::move(new_thread), new_queue});
            }
        };

        ~ThreadPool() {
            for (auto& thread : threads) {
                auto cmd = std::make_shared<Command>(CommandType::Quit);

                {
                    boost::unique_lock<boost::mutex> lock(thread.second->mutex);
                    thread.second->queue.push(std::move(cmd));
                }
                thread.second->condition.notify_one();
            }

            for (auto& thread : threads) {
                thread.first.join();
                delete thread.second;
            }
        };

        std::shared_ptr<Task> schedule(std::function<void(void*)> func, void* arg = nullptr, void* data = nullptr) {
            CommandQueue* commands = threads[sched_counter].second;
            auto cmd = std::make_shared<CommandExecute>(std::move(func), arg, data);

            {
                boost::unique_lock<boost::mutex> lock(commands->mutex);
                commands->queue.push(cmd);
            }

            commands->condition.notify_one();
            sched_counter = (sched_counter + 1) % threads.size();

            return cmd;
        };

        void resize(unsigned int new_pool_size) {
            if (new_pool_size == threads.size()) {
                return;
            }

            if (new_pool_size < threads.size()) {
                for (unsigned int i = 0; i < new_pool_size; i++, sched_counter = (sched_counter + 1) % threads.size()) {
                    auto cmd = std::make_shared<Command>(CommandType::Quit);

                    {
                        boost::unique_lock<boost::mutex> lock(threads[sched_counter].second->mutex);
                        threads[sched_counter].second->queue.push(std::move(cmd));
                    }
                    threads[sched_counter].second->condition.notify_one();

                    threads[sched_counter].first.join();
                    delete threads[sched_counter].second;
                }

                threads.resize(new_pool_size);
            } else {
                for (unsigned int i = threads.size(); i < new_pool_size; i++) {
                    auto new_queue = new CommandQueue;
                    boost::thread new_thread(&ThreadPool::runner, this, new_queue);
                    threads.push_back({std::move(new_thread), new_queue});
                }
            }
        }

        inline decltype(threads)::size_type size() const {
            return threads.size();
        }
    };
} // namespace tp

#endif