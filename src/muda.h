#include "zBase.h"

INLINE_PROCEDURE void MudaReportV(const char *agent, const char *fmt, va_list args) {
	LogInfo("[%s] ", agent);
	LogInfoV(fmt, args);
}

INLINE_PROCEDURE void MudaReport(const char *agent, const char *fmt, ...) {
	va_list arg;
	va_start(arg, fmt);
	MudaReportV(agent, fmt, arg);
	va_end(arg);
}
