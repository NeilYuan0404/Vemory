#pragma once

#include <string>

#include "vemory/protocol/RequestContext.h"

// Domain dispatcher for VSET / VGET / VDEL.
// arg must be VNodeDispatchArg*.
void VNodeDispatcher(const RequestContext& ctx, std::string* reply, void* arg);
