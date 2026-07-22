#pragma once

#include <string>

#include "vemory/protocol/RequestContext.h"

// Domain dispatcher for VSET / VGET / VDEL.
// arg must be VNodeIndex*.
void VNodeDispatcher(const RequestContext& ctx, std::string* reply, void* arg);
