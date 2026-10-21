#pragma once

// 初期化順が未確認のゲーム側snprintfやallocatorへ依存しない。
struct BootstrapUpstreamLogger {
    constexpr bool IsEnabled() const {
        return false;
    }

    template <typename... Args> constexpr void Log(Args&&...) const {}

    template <typename... Args> constexpr void VLog(Args&&...) const {}
};

inline constexpr BootstrapUpstreamLogger Logging;
