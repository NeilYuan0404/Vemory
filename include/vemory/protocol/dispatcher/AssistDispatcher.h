#pragma once

#include <string>

#include "vemory/protocol/RequestContext.h"

// Domain dispatcher for PING / ECHO. arg is unused.
void AssistDispatcher(const RequestContext& ctx, std::string* reply, void* arg);
