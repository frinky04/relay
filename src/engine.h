#pragma once
#include "command.h"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

class Engine {
public:
    struct Result { uint64_t generation; command::View view; };
    struct Completion { uint64_t generation; std::string error; };
    // Native declarations receive a worker-only request for plugin reload.
    using Loader = std::function<std::vector<command::Command>(std::function<std::string()>)>;
    using PluginLoader = std::function<void(std::vector<command::Command>&)>;
    ~Engine();
    void start(Loader loader, PluginLoader plugins, std::function<void()> wake);
    void stop();
    uint64_t submit(std::string text);
    uint64_t cancel();
    bool execute(uint64_t generation, size_t row, bool confirmed);
    std::optional<Result> takeResult();
    std::optional<Completion> takeCompletion();

private:
    struct Query { uint64_t generation; std::string text; };
    struct Action { uint64_t generation; size_t row; bool confirmed; };
    void work(Loader loader, PluginLoader plugins, const std::function<void()>& wake);
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false, m_executing = false;
    uint64_t m_generation = 0;
    std::optional<Query> m_query;
    std::optional<Action> m_action;
    std::optional<Result> m_result;
    std::optional<Completion> m_completion;
};
