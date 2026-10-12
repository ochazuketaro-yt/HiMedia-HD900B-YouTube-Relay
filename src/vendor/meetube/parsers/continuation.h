#ifndef YT_CONTINUATION_H
#define YT_CONTINUATION_H
#include <QString>
#include <string_view>
namespace yt {
// First continuation token found in a DFS of the (raw JSON) document — the
// modern continuationCommand.token plus the legacy next/reload shapes.
QString findContinuationToken(std::string_view json);
// Same, but scoped to the value of a top-level key ("" when the key is absent) —
// e.g. the comments-section token lives under "engagementPanels" only.
QString findContinuationTokenUnder(std::string_view json, const char *rootKey);
}
#endif
