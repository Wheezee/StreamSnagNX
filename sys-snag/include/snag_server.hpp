#pragma once
#include <switch.h>
#include <atomic>
#include "snag_ipc.hpp"
#include "ipc_server.h"

class SnagServer {
public:
    static SnagServer& Instance();

    bool Initialize();
    void Exit();
    void LoopProcess();

private:
    SnagServer();
    ~SnagServer();

    static Result RequestHandler(void* userdata, const IpcServerRequest* r, u8* out_data, size_t* out_dataSize);

    IpcServer server_{};
    std::atomic<bool> running_{false};
};
