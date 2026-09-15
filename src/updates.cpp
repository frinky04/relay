#include "updates.h"
#include <exception>

Updates::~Updates() { stop(); }

void Updates::start(UpdateBackend backend, Report report) {
    stop();
    {
        std::lock_guard lock(m_mutex);
        m_available = bool(backend.download);
        m_ready = false;
        m_busy = m_available;
        m_request = m_available ? Request::Check : Request::None;
    }
    if (!m_available) return;
    m_thread = std::jthread([this, backend = std::move(backend), report = std::move(report)](std::stop_token stop) {
        bool automatic = true;
        for (;;) {
            Request task;
            {
                std::unique_lock lock(m_mutex);
                if (!m_cv.wait(lock, stop, [this] { return m_request != Request::None; })) return;
                if (stop.stop_requested()) return;
                task = m_request;
                m_request = Request::None;
            }
            std::string title, body;
            bool ready = false;
            try {
                if (task == Request::Restart) {
                    backend.restart();
                    return;
                }
                const auto version = backend.download(stop);
                ready = !version.empty();
                if (ready) {
                    title = "Update ready";
                    body = "Relay " + version + " is ready; run /relay Restart to Update or restart Relay later";
                } else if (!automatic) {
                    title = "Relay is up to date";
                    body = "No newer release is available";
                }
            } catch (const std::exception&) {
                title = "Update failed";
                body = task == Request::Restart
                    ? "Cannot apply the update; run /relay Restart to Update again"
                    : "Cannot download updates; check your connection and run /relay Check for Updates";
            }
            {
                std::lock_guard lock(m_mutex);
                // A failed recheck must not discard an already downloaded update.
                m_ready = m_ready || ready;
                m_busy = false;
            }
            if (stop.stop_requested()) return;
            if (!title.empty()) report(std::move(title), std::move(body));
            automatic = false;
        }
    });
}

void Updates::stop() {
    {
        std::lock_guard lock(m_mutex);
        m_available = false;
    }
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_cv.notify_all();
        m_thread.join();
    }
}

std::string Updates::request(Request request) {
    std::lock_guard lock(m_mutex);
    if (!m_available) return "Updates are unavailable in this build; download Relay from GitHub Releases";
    if (m_busy) return "An update operation is running; wait for it to finish and try again";
    if (request == Request::Restart && !m_ready)
        return "No update is ready; run /relay Check for Updates first";
    m_busy = true;
    m_request = request;
    m_cv.notify_one();
    return {};
}

std::string Updates::check() { return request(Request::Check); }
std::string Updates::restart() { return request(Request::Restart); }
