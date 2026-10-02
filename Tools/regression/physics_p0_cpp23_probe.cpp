#include <expected>
#include <flat_map>
#include <flat_set>
#include <mdspan>
#include <ranges>
#include <format>
#include <source_location>
#include <memory_resource>
#include <variant>
#include <array>
#include <vector>
#include <iostream>
#include <mathematics/views.hpp>

struct view_test {
    int value = 4;
    template<class Self> decltype(auto) get(this Self&& self) { return (self.value); }
};
template<class T> requires std::integral<T>
constexpr T twice(T v) { return v + v; }
int main() {
    auto e = std::expected<int, int>{2}.and_then([](int n) -> std::expected<int,int> { return n+1; })
        .transform([](int n) { return twice(n); }).or_else([](int) -> std::expected<int,int> { return 0; });
    std::flat_map<int,int> map{{1,2}};
    std::flat_set<int> set{1,2};
    std::array<int,4> data{1,2,3,4};
    std::mdspan<int,std::extents<std::size_t,2,2>> matrix(data.data());
    int sum=0;
    for (auto [i,v] : data | std::views::enumerate) sum+=int(i)+v;
    for (auto [a,b] : std::views::zip(data,data)) sum+=a+b;
    for (auto chunk : data | std::views::chunk(2)) sum+=*chunk.begin();
    for (int v : data | std::views::filter([](int x){return x%2==0;}) |
                 std::views::transform([](int x){return x*2;})) sum+=v;
    view_test v; const view_test cv;
    std::variant<int,float> command{3};
    sum+=std::visit([](auto x){return int(x);},command);
    std::pmr::monotonic_buffer_resource arena;
    std::pmr::vector<int> owned(&arena); owned.push_back(1);
    math::vector3 vector{1,2,3};
    float math_sum=0;
    for (auto x : math::components(vector) | math::views::transform_fixed([](float x){return x*2;})) math_sum+=x;
    auto m=math::matrix4x4::identity();
    std::size_t rows=0; for(auto row : math::rows(m)) { if(row.size()!=4) return 2; ++rows; }
    if(*e!=6 || matrix[1,1]!=4 || map.at(1)!=2 || !set.contains(2) || v.get()!=4 || cv.get()!=4 ||
       math_sum!=12 || rows!=4 || sum!=55 || owned[0]!=1) return 1;
    std::cout << std::format("{{\"result\":\"CPP23_OK\",\"msvc\":{},\"cplusplus\":{},\"source_line\":{}}}\n",
        _MSC_FULL_VER,__cplusplus,std::source_location::current().line());
}
