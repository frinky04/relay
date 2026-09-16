#include "engine.h"
#include <exception>
#include <utility>
#include <algorithm>
#include <stdexcept>

Engine::~Engine() { stop(); }
void Engine::start(Loader loader, PluginLoader plugins, std::function<void()> wake, AppLoader apps) {
    m_thread = std::thread([this, loader = std::move(loader), plugins = std::move(plugins), wake = std::move(wake), apps = std::move(apps)] {
        work(loader, plugins, wake, apps);
    });
}
void Engine::stop() {
    { std::lock_guard lock(m_mutex); m_stop = true; }
    m_cv.notify_one();
    if (m_thread.joinable()) m_thread.join();
}
uint64_t Engine::submit(std::string text) {
    std::lock_guard lock(m_mutex);
    m_query = Query{++m_generation, std::move(text)};
    m_result.reset();
    m_cv.notify_one();
    return m_generation;
}
uint64_t Engine::cancel() {
    std::lock_guard lock(m_mutex);
    ++m_generation;
    m_query.reset(); m_result.reset();
    return m_generation;
}
bool Engine::execute(uint64_t generation, size_t row, bool confirmed) {
    std::lock_guard lock(m_mutex);
    if (m_stop || m_executing || m_action || m_completion || generation != m_generation) return false;
    m_action = Action{generation, row, confirmed};
    m_cv.notify_one();
    return true;
}
std::optional<Engine::Result> Engine::takeResult() {
    std::lock_guard lock(m_mutex);
    return std::exchange(m_result, std::nullopt);
}
std::optional<Engine::Completion> Engine::takeCompletion() {
    std::lock_guard lock(m_mutex);
    return std::exchange(m_completion, std::nullopt);
}

void Engine::work(Loader loader, PluginLoader plugins, const std::function<void()>& wake, AppLoader apps) {
    // The catalog, evaluations and every Lua reference live and die here.
    bool reloadRequested = false;
    bool rescanRequested = false;
    std::vector<command::Command> native;
    std::vector<command::Command> commands;
    std::string loadError;
    auto load = [&] {
        auto next = native;
        if (plugins) plugins(next);
        return next;
    };
    try {
        native = loader([&]() -> std::string { reloadRequested = true; return {}; },
            [&]() -> std::string { rescanRequested = true; return {}; });
        commands = native;
        commands = load();
    }
    catch (const std::exception& e) { loadError = e.what(); }
    command::Evaluation current;
    uint64_t evaluated = 0;
    for (;;) {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [&] { return m_stop || m_action || m_query; });
        if (m_stop) return;
        if (m_action) {
            const auto action = *m_action; m_action.reset();
            if (action.generation != m_generation || action.generation != evaluated ||
                action.row >= current.actions.size() || !current.actions[action.row] ||
                (current.view.rows[action.row].danger && !action.confirmed)) {
                // Every accepted dispatch finishes, including one cancelled by
                // a newer edit before the worker could start it.
                m_completion = Completion{action.generation, "Request changed; select a current row and try again"};
                lock.unlock();
                wake();
                continue;
            }
            m_executing = true;
            lock.unlock();
            std::string error;
            try { error = current.actions[action.row](); }
            catch (const std::exception& e) { error = e.what(); }
            catch (...) { error = "Command failed; edit the request and try again"; }
            bool reloaded = false;
            if (std::exchange(rescanRequested, false) && error.empty()) {
                try {
                    auto app = apps();
                    auto nativeCopy = app;
                    const auto nativeApp = std::find_if(native.begin(), native.end(), [](auto& cmd) { return cmd.name == "app"; });
                    const auto activeApp = std::find_if(commands.begin(), commands.end(), [](auto& cmd) { return cmd.name == "app"; });
                    if (nativeApp == native.end() || activeApp == commands.end())
                        throw std::runtime_error("App command unavailable");
                    // Displayed actions own their callbacks. Keep this evaluation
                    // usable while future queries see the new app catalog.
                    *nativeApp = std::move(nativeCopy);
                    *activeApp = std::move(app);
                } catch (...) {
                    error = "Cannot rescan apps; try /relay Rescan Apps again";
                }
            }
            if (std::exchange(reloadRequested, false) && error.empty()) {
                try {
                    auto next = load();
                    // The executing callback has returned. Release all actions
                    // pointing into the old catalog before replacing it.
                    current = {};
                    evaluated = 0;
                    commands = std::move(next);
                    loadError.clear();
                    reloaded = true;
                } catch (const std::exception& e) {
                    error = std::string(e.what()) + "; check the plugins and run /relay Reload Plugins again";
                } catch (...) {
                    error = "Cannot reload plugins; check the plugins and run /relay Reload Plugins again";
                }
            }
            lock.lock();
            if (reloaded) m_result.reset();
            m_executing = false;
            m_completion = Completion{action.generation, std::move(error)};
            lock.unlock();
            wake();
        } else {
            auto query = std::move(*m_query); m_query.reset();
            lock.unlock();
            current = command::evaluate(commands, query.text);
            if (!loadError.empty()) {
                MenuRow row; row.title = "Commands unavailable"; row.subtitle = loadError; row.kind = "Error";
                current.view.rows.push_back(std::move(row)); current.actions.push_back({});
            }
            evaluated = query.generation;
            lock.lock();
            if (query.generation != m_generation) continue;
            m_result = Result{query.generation, current.view};
            lock.unlock();
            wake();
        }
    }
}
