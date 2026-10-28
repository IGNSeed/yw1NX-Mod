#pragma once
namespace yw1::render {
template<class F> struct ScopeExit {
    F function;
    ~ScopeExit() { function(); }
};
} // 名前空間 yw1::render
