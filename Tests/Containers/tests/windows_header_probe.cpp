#include <Windows.h>
#include <ce/bounded_mpsc_queue.h>
#include <ce/containers.h>
int main() {
  ce::utf8_string a(ce::utf8_string_view("sdk"));
  auto b = a;
  ce::string wide(u"\uD55C\uAE00");
  ce::utf8_string bytes;
  if(!ce::to_utf8(wide.view(),bytes) || bytes.size()!=6) return 2;
  ce::dynamic_array<int> values;
  values.push_back(42);
  values.push_back(values[0]);
  ce::bounded_mpsc_queue<int, 7> queue;
  int value = 0;
  return queue.try_push(42) && queue.try_pop(value) && value == 42 && b == a &&
                 values[1] == 42
             ? 0
             : 1;
}
