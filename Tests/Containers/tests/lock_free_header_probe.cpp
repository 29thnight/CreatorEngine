#include <ce/lock_free_queue.h>
struct value {
  int x = 0;
  explicit value(int v = 0) : x(v) {}
  value(const value &) = delete;
  value(value &&v) noexcept : x(v.x) {}
  value &operator=(value &&v) noexcept {
    x = v.x;
    return *this;
  }
  ~value() noexcept = default;
};
int main() {
  ce::lock_free_queue<value> q;
  auto prepared = decltype(q)::prepare(42);
  if (!q.publish(static_cast<decltype(prepared) &&>(prepared)))
    return 1;
  value out;
  if (!q.try_pop(out) || out.x != 42)
    return 2;
  q.reclaim();
  return q.empty() ? 0 : 3;
}
