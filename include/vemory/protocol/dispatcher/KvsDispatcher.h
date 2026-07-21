#pragma once

#include <string>

#include "vemory/protocol/RequestContext.h"

// Domain dispatcher for SET / DEL / GET.
// arg must be KvStore*.
void KvsDispatcher(const RequestContext& ctx, std::string* reply, void* arg);
