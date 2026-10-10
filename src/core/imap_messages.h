// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "imap_session.h"

#include <QMap>
#include <QStringList>

// Message-level IMAP commands for ImapMailProvider's worker jobs (D-340):
// FLAGS read and STORE of flags and keywords, header blocks
// (BODY.PEEK[HEADER] / [HEADER.FIELDS (...)]), UID COPY and UID MOVE of UID
// sets with the RFC 4315 COPYUID mapping, and the \Deleted + (UID) EXPUNGE
// fallback of a move without RFC 6851 MOVE. Every function runs inside an
// open, selected Imap::Session on the job's thread; a false return means the
// session's channel recorded the failure. Internal to the library.
namespace QindaTK::ImapMessages {

constexpr int MaximumUids = 5000;      // messages in one request
constexpr int ChunkUids = 500;         // UIDs in one command line
constexpr int MaximumHeaderFields = 50;

// "1:3,7" from {1, 2, 3, 7} (sorted, duplicates dropped).
QByteArray uidSet(const QList<qint64> &uids);
// {1, 2, 3, 7} from "1:3,7" or "3:1,7" (each range ascending), at most
// `limit` UIDs; empty when malformed, open ("*") or over the limit.
QList<qint64> expandUidSet(const QString &set, int limit = MaximumUids);
// The flag atoms of a FLAGS list value.
QStringList flagsOf(const Imap::Value *flags);

// UID FETCH (UID FLAGS) of `uids`, in chunks; `flags` gets every reported UID.
bool fetchFlags(Imap::Session &session, const QList<qint64> &uids, QMap<qint64, QStringList> *flags);
// UID STORE <set> +FLAGS.SILENT (add) and -FLAGS.SILENT (remove); the flags
// were validated (ImapKeywords::validFlag) before the job started.
bool storeFlags(Imap::Session &session, const QList<qint64> &uids, const QStringList &add, const QStringList &remove);

// An RFC 5322 field name the IMAP command line can carry as an atom.
bool validHeaderField(const QString &name);
// UID FETCH of each message's header block: the whole header when `fields`
// is empty, else BODY.PEEK[HEADER.FIELDS (<fields>)]. The blocks are the
// server's bytes (unfolded by nobody); the message stays unread.
bool fetchHeaders(Imap::Session &session, const QList<qint64> &uids, const QStringList &fields,
                  QMap<qint64, QByteArray> *headers);

// UID COPY or, with `move`, UID MOVE (RFC 6851) of `uids` to the wire name
// `target`; without MOVE: COPY, then \Deleted and UID EXPUNGE (EXPUNGE
// without UIDPLUS, which also removes other \Deleted messages). `targets`
// maps source to destination UIDs and `targetValidity` is set when the
// server answers with COPYUID (RFC 4315).
bool transfer(Imap::Session &session, const QList<qint64> &uids, const QByteArray &target, bool move,
              QMap<qint64, qint64> *targets, QString *targetValidity);
// \Deleted on `uids`, then UID EXPUNGE of them (EXPUNGE without UIDPLUS).
bool expunge(Imap::Session &session, const QList<qint64> &uids);
// The COPYUID of a COPY/MOVE exchange (tagged status or untagged OK).
void readCopyUids(const QList<QByteArray> &untagged, const QString &status, QMap<qint64, qint64> *targets,
                  QString *targetValidity);

} // namespace QindaTK::ImapMessages
