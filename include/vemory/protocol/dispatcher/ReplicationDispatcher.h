#pragma once

#include <string>

#include "vemory/protocol/RequestContext.h"

void ReplicationDispatcher(const RequestContext& ctx, std::string* reply,
                           void* arg);
