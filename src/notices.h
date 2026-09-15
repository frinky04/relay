#pragma once
#include <deque>
#include <functional>
#include <mutex>
#include <string>

// Bounded notification storage, independent of command resolution.
class NoticeStore {
public:
    struct Notice { std::string title, body; unsigned long long tick; };

    void push(Notice n);
    void clear();
    void dismiss(unsigned long long tick);
    void forEach(const std::function<void(const Notice&)>& f);

private:
    std::mutex m_mtx;
    std::deque<Notice> m_list;
};
