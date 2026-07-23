#pragma once

#include <string>

#include "vemory/protocol/RequestContext.h"

// arg must be SnapshotManager*.
void PersistDispatcher(const RequestContext& ctx, std::string* reply, void* arg);
