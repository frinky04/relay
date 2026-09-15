#include "notices.h"
#include <algorithm>

void NoticeStore::push(Notice n) {
    std::lock_guard lk(m_mtx);
    if (!n.key.empty()) std::erase_if(m_list, [&](const Notice& old) { return old.key == n.key; });
    m_list.push_front(std::move(n));
    if (m_list.size() > 20) m_list.pop_back();
}

void NoticeStore::clear() { std::lock_guard lk(m_mtx); m_list.clear(); }

void NoticeStore::dismiss(unsigned long long tick) {
    std::lock_guard lk(m_mtx);
    std::erase_if(m_list, [tick](const Notice& n) { return n.tick == tick; });
}

void NoticeStore::forEach(const std::function<void(const Notice&)>& f) {
    std::lock_guard lk(m_mtx);
    for (auto& n : m_list) f(n);
}
