#pragma once

#include "ipc_fs.hpp"

namespace yw1::logging {

struct SaveOutcome {
    Result result = 0;
    Result writeResult = kAlreadyRun;
    Result flushResult = kAlreadyRun;
    Result receiptWriteResult = kAlreadyRun;
    Result receiptFlushResult = kAlreadyRun;
    Result closeResult = kAlreadyRun;
    bool payloadPersisted = false;
    bool receiptPersisted = false;
    bool quarantined = false;
};

// 起動ごとに1個だけ所有する。失敗時も再送して既存ファイルを重複追記しない。
class SdWriter {
  public:
    SaveOutcome save(MemoryLog& log, IpcFs& fs, IpcStorage& storage);

  private:
    std::atomic<bool> entered_{false};
};

void startSdLogger();

} // 名前空間 yw1::logging
