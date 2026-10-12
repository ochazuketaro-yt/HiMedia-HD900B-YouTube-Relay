#ifndef YT_RENDERERPARSER_H
#define YT_RENDERERPARSER_H
#include <QString>
#include <QList>
#include <string>
#include <string_view>
#include "servicedatatypes.h"
namespace yt {
// Every function takes raw JSON bytes: either a whole InnerTube response
// (parse*List / parseChannel / parseAccountsList / parseWatchPage) or the inner
// renderer object itself (parseVideoRenderer & co, parseText). The buffer only
// needs to stay alive for the duration of the call.
QString parseText(std::string_view field);
CT::Video parseVideoRenderer(std::string_view r);
// lockupViewModel (2024+ YouTube surfaces — watch-page related, some feeds/search —
// replaced compactVideoRenderer/gridVideoRenderer). Caller passes the inner object.
CT::Video parseLockupViewModel(std::string_view lm);
// tileRenderer (TVHTML5 surfaces): the authed feeds (history/subscriptions/library)
// arrive TV-shaped because the OAuth bearer is only honored by the TV client.
CT::Video parseTileRenderer(std::string_view t);
QList<CT::Video> parseVideoList(std::string_view response, QString *nextToken);
// createCommentParams (out, nullable): the create-comment box's submit token, scraped
// best-effort from the comments-section create-comment command (R4 — exact source
// unverified; left empty when absent). Threaded into CommentPage so the model can post.
QList<CT::Comment> parseComments(std::string_view response, QString *nextToken,
                                 QString *createCommentParams = 0);
CT::Playlist parsePlaylistRenderer(std::string_view r);
QList<CT::Playlist> parsePlaylistList(std::string_view response, QString *nextToken);
// Channel header (c4TabbedHeaderRenderer / pageHeaderRenderer) → CT::User.
// The response is a whole document; the const std::string & overload reads via
// the NUL-terminated sentinel path (kInDoc) — production passes *r.body.
CT::User parseChannel(std::string_view response);
CT::User parseChannel(const std::string &response);
// The viewer's subscribe state on a channel — the first subscribeButtonRenderer.subscribed
// anywhere (works for the WEB c4 header AND the TVHTML5 channelHeaderRenderer). Only an
// AUTHED (bearer/TV) browse carries it; anonymous responses omit it → false.
bool parseChannelSubscribed(std::string_view response);
CT::User parseUserRenderer(std::string_view r);
QList<CT::User> parseUserList(std::string_view response, QString *nextToken);
// account/accounts_list (authed) → the active account's identity. The channel id is
// reconstructed as "UC" + offlineCacheKeyToken.clientCacheKey (the youtubei.js trick —
// the response carries no plain channelId).
CT::Account parseAccountsList(std::string_view response);
// Watch page (/next): the primary video's details (title/description/views/likes +
// channel name/avatar/id) and the related-videos list, in one response.
void parseWatchPage(std::string_view response, CT::Video *primary, QList<CT::Video> *related);
// navigation/resolve_url response → endpoint.browseEndpoint.browseId ("" when absent).
QString parseResolvedBrowseId(std::string_view response);
}
#endif
