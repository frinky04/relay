#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>

// Calls run only on the update worker. Download returns the ready version,
// or an empty string when current; failures throw. Restart prepares the updater
// and requests normal UI shutdown. The SDK owns package verification/install.
struct UpdateBackend {
    std::function<std::string(std::stop_token)> download;
    std::function<void()> restart;
};

class Updates {
public:
    using Report = std::function<void(std::string, std::string)>;
    ~Updates();
    void start(UpdateBackend backend, Report report);
    void stop();
    std::string check();
    std::string restart();

private:
    enum class Request { None, Check, Restart };
    std::string request(Request request);
    std::jthread m_thread;
    std::mutex m_mutex;
    std::condition_variable_any m_cv;
    Request m_request = Request::None;
    bool m_available = false, m_busy = false, m_ready = false;
};

UpdateBackend velopackUpdates(std::function<void()> quit);
void runVelopack();
namespace Velopack { class IUpdateSource; }
std::unique_ptr<Velopack::IUpdateSource> releaseUpdateSource(std::stop_token stop);
