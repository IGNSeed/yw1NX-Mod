#include "native_snapshot.hpp"
#include <string_view>

namespace yw1::logging {
namespace {
using namespace std::literals;
constexpr auto kHeader = "YW1_NATIVE_SNAPSHOT_V1\nsequence=0x"sv;
constexpr auto kCommit = "YW1_COMMIT_V1\nsequence=0x"sv;
constexpr auto kHash = "\npayload_hash=0x"sv;
constexpr auto kSuccess = "\npayload_write_result=0x00000000\npayload_flush_result=0x00000000\n"sv;
constexpr std::array kKeys{
    "runtime_mode"sv, "hook_status"sv, "callback_count"sv, "original_call_count"sv,
    "original_return_count"sv, "original_result"sv, "hook_error"sv, "reentry_count"sv,
    "in_flight"sv, "gl_resolver_verified"sv, "gl_resolver_rejected"sv, "current_context"sv,
    "context_changes"sv, "imgui_initialized"sv, "backend_initialized"sv, "newframe_count"sv,
    "render_count"sv, "draw_count"sv, "gl_state_save"sv, "gl_state_restore"sv,
    "gl_state_failures"sv, "arena_capacity"sv, "arena_used"sv, "arena_peak"sv,
    "arena_largest_free"sv, "allocation_failures"sv, "invalid_frees"sv, "worker_state"sv,
    "worker_create_result"sv, "worker_start_result"sv, "worker_handle_close_result"sv,
    "sm_connect_result"sv, "fs_initialize_result"sv, "sd_open_result"sv, "file_open_result"sv,
    "file_write_result"sv, "file_flush_result"sv, "file_read_result"sv, "last_error"sv,
    "dropped_snapshots"sv, "last_verified_sequence"sv, "system_tick"sv, "memory_log_dropped"sv,
    "sm_borrow_result"sv, "fsp_get_service_result"sv, "transport_result"sv,
    "hook_install_result"sv, "last_render_result"sv, "renderer_disabled"sv
};
static_assert(kKeys.size() == static_cast<unsigned>(SnapshotField::Count));

class Text {
  public:
    explicit Text(std::span<char> bytes) : bytes_(bytes) {}
    void add(std::string_view value) {
        if (value.size() > bytes_.size() - used_) { valid_ = false; return; }
        for (char byte : value) { bytes_[used_++] = byte; }
    }
    void hex(std::uint64_t value) {
        constexpr char digits[] = "0123456789ABCDEF";
        for (unsigned i = 0; i < 16; ++i) {
            const char byte = digits[(value >> ((15 - i) * 4)) & 15];
            add(std::string_view(&byte, 1));
        }
    }
    bool valid() const { return valid_; }
  private:
    std::span<char> bytes_;
    std::size_t used_ = 0;
    bool valid_ = true;
};

bool starts(std::span<const char> bytes, std::string_view value) {
    if (bytes.size() < value.size()) { return false; }
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (bytes[i] != value[i]) { return false; }
    }
    return true;
}

bool readHex(std::span<const char> bytes, std::size_t position, std::uint64_t& value) {
    if (position > bytes.size() || bytes.size() - position < 16) { return false; }
    value = 0;
    for (unsigned i = 0; i < 16; ++i) {
        const char byte = bytes[position + i];
        unsigned digit = byte >= '0' && byte <= '9' ? byte - '0' :
            byte >= 'A' && byte <= 'F' ? byte - 'A' + 10 : 16;
        if (digit > 15) { return false; }
        value = (value << 4) | digit;
    }
    return true;
}

std::uint64_t hash(std::span<const char> bytes) {
    std::uint64_t result = 14695981039346656037ULL;
    for (char byte : bytes) { result = (result ^ static_cast<unsigned char>(byte)) * 1099511628211ULL; }
    return result;
}
} // 無名名前空間

bool formatSnapshot(std::span<char, kSnapshotSlotSize> bytes, const NativeSnapshot& snapshot,
                    std::uint64_t sequence) {
    for (auto& byte : bytes) { byte = ' '; }
    bytes[kSnapshotPayloadSize - 1] = '\n';
    bytes.back() = '\n';
    Text text(bytes.first(kSnapshotPayloadSize - 1));
    text.add(kHeader); text.hex(sequence);
    text.add("\nmod_version=B0.2.15.1\nbuild_tag=0x000000000B002151\n"
             "game_version=1.4.0\ngame_build_id="
             "7F35BDFC5DE46CF1EDE9C0618B47047BAFF49E12000000000000000000000000\nboot_id=0x");
    text.hex(snapshot.bootId); text.add("\n");
    for (unsigned i = 0; i < kKeys.size(); ++i) {
        text.add(kKeys[i]); text.add("=0x"); text.hex(snapshot.values[i]); text.add("\n");
    }
    Text footer(bytes.last(kSnapshotFooterSize));
    footer.add("YW1_PENDING_V1\n");
    return text.valid() && footer.valid();
}

void commitSnapshot(std::span<char, kSnapshotSlotSize> bytes) {
    std::uint64_t sequence = 0;
    if (!readHex(bytes, kHeader.size(), sequence)) { return; }
    const auto checksum = hash(bytes.first(kSnapshotPayloadSize));
    auto footerBytes = bytes.last(kSnapshotFooterSize);
    for (auto& byte : footerBytes) { byte = ' '; }
    footerBytes.back() = '\n';
    Text footer(footerBytes);
    footer.add(kCommit); footer.hex(sequence); footer.add(kHash); footer.hex(checksum);
    // このreceiptは、先に成功したpayloadのWrite/Flushだけを証言する。
    footer.add(kSuccess);
}

SnapshotRecord inspectSnapshot(std::span<const char, kSnapshotSlotSize> bytes) {
    SnapshotRecord result;
    result.recognized = starts(bytes, kHeader);
    if (!result.recognized || !readHex(bytes, kHeader.size(), result.sequence)) { return result; }
    const auto footer = bytes.last(kSnapshotFooterSize);
    std::uint64_t sequence = 0;
    const auto hashPosition = kCommit.size() + 16;
    result.committed = starts(footer, kCommit) && readHex(footer, kCommit.size(), sequence) &&
        starts(footer.subspan(hashPosition), kHash) &&
        readHex(footer, hashPosition + kHash.size(), result.checksum) &&
        starts(footer.subspan(hashPosition + kHash.size() + 16), kSuccess) &&
        sequence == result.sequence && sequence != 0 &&
        result.checksum == hash(bytes.first(kSnapshotPayloadSize));
    return result;
}
} // 名前空間 yw1::logging
