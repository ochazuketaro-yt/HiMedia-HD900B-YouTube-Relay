/*
 * Copyright (C) 2026 IarChep
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CT_SERVICEDATATYPES_H
#define CT_SERVICEDATATYPES_H

#include <QString>
#include <QList>
#include <QMetaType>

namespace CT {

struct Video {
    QString id, title, description, thumbnailUrl, largeThumbnailUrl, date, duration, url,
            streamUrl, userId, username, avatarUrl, likeText, viewText;
    qint64 viewCount = 0;
    // Account-tied engagement state (populated only from authed /next; see WS1).
    int    likeStatus  = 0;    // 0 Indifferent, 1 Liked, 2 Disliked
    qint64 likeCount   = -1;   // numeric; -1 = unknown
    qint64 dislikeCount = -1;  // RYD-filled; -1 = unknown
    // Viewer's subscribe state for this video's channel — populated only from the
    // authed /next owner (videoOwnerRenderer.subscribeButton); false otherwise.
    bool subscribed = false;
    bool downloadable = false;
};
struct Playlist {
    QString id, title, description, thumbnailUrl, date, userId, username;
    int videoCount = 0;
};
struct User {
    QString id, username, description, thumbnailUrl, subscriberCount, bannerUrl,
            handle, videoCount;
    bool subscribed = false;
};
struct Comment { QString id, body, date, userId, username, thumbnailUrl, videoId; };
// One playable track. id = itag ("18","140",…) or "hls". A muxed format has
// hasAudio && width>0; a video-only adaptive has width>0 && !hasAudio; an
// audio-only adaptive has width==0 && hasAudio. bitrate is bits/s (0 if unknown).
struct Stream  { QString id, url, description, mimeType; int width = 0, height = 0, bitrate = 0; bool hasAudio = false; };
struct Subtitle{ QString id, url, title, language; };
struct Category{ QString id, title; };
struct Account { QString id, username, thumbnailUrl, handle, channelId; };

} // namespace CT

Q_DECLARE_METATYPE(CT::Video)
Q_DECLARE_METATYPE(CT::Playlist)
Q_DECLARE_METATYPE(CT::User)
Q_DECLARE_METATYPE(CT::Comment)
Q_DECLARE_METATYPE(CT::Stream)
Q_DECLARE_METATYPE(CT::Subtitle)
Q_DECLARE_METATYPE(CT::Category)
Q_DECLARE_METATYPE(CT::Account)
Q_DECLARE_METATYPE(QList<CT::Video>)
Q_DECLARE_METATYPE(QList<CT::Playlist>)
Q_DECLARE_METATYPE(QList<CT::User>)
Q_DECLARE_METATYPE(QList<CT::Comment>)
Q_DECLARE_METATYPE(QList<CT::Stream>)
Q_DECLARE_METATYPE(QList<CT::Subtitle>)
Q_DECLARE_METATYPE(QList<CT::Category>)
Q_DECLARE_METATYPE(QList<CT::Account>)

#endif // CT_SERVICEDATATYPES_H
