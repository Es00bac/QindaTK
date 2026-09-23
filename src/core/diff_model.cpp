// SPDX-License-Identifier: LGPL-3.0-or-later
#include "diff_model.h"

#include <QCryptographicHash>

namespace QindaTK {
namespace {
constexpr int MaxLines = 512;
constexpr int MaxLineLength = 2048;
constexpr int MaxHunks = 128;

struct Step {
  char kind;
  int left;
  int right;
};

QString fingerprint(const QStringList &left, const QStringList &right,
                    int leftStart, int leftEnd, int rightStart, int rightEnd,
                    const QString &before, const QString &after) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  auto add = [&hash](const QString &value) {
    const QByteArray bytes = value.toUtf8();
    hash.addData(QByteArray::number(bytes.size()));
    hash.addData(QByteArrayLiteral(":"));
    hash.addData(bytes);
  };
  add(before);
  add(after);
  for (int i = leftStart; i < leftEnd; ++i)
    add(left[i]);
  add(QStringLiteral("->"));
  for (int i = rightStart; i < rightEnd; ++i)
    add(right[i]);
  return QString::fromLatin1(hash.result().toHex().left(20));
}
} // namespace

DiffModel::DiffModel(QObject *parent) : QAbstractListModel(parent) {}

int DiffModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0
         : m_provider     ? m_provider->regionCount()
                          : m_rows.size();
}

QVariant DiffModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
    return {};
  const DiffRegion row =
      m_provider ? m_provider->regionAt(index.row()) : m_rows[index.row()];
  switch (role) {
  case KindRole:
    return row.kind;
  case LeftTextRole:
    return row.leftText;
  case RightTextRole:
    return row.rightText;
  case LeftLineRole:
    return row.leftLine;
  case RightLineRole:
    return row.rightLine;
  case HunkIdRole:
    return row.hunkId;
  case ConflictIdRole:
    return row.conflictId.isEmpty() ? row.hunkId : row.conflictId;
  case LeftValueRole:
    return row.leftValue.isValid() ? row.leftValue : QVariant(row.leftText);
  case RightValueRole:
    return row.rightValue.isValid() ? row.rightValue : QVariant(row.rightText);
  default:
    return {};
  }
}

QHash<int, QByteArray> DiffModel::roleNames() const {
  return {{KindRole, "kind"},
          {LeftTextRole, "leftText"},
          {RightTextRole, "rightText"},
          {LeftLineRole, "leftLine"},
          {RightLineRole, "rightLine"},
          {HunkIdRole, "hunkId"},
          {ConflictIdRole, "conflictId"},
          {LeftValueRole, "leftValue"},
          {RightValueRole, "rightValue"}};
}

QVariantMap DiffModel::get(int row) const {
  if (row < 0 || row >= rowCount())
    return {};
  const DiffRegion item = m_provider ? m_provider->regionAt(row) : m_rows[row];
  return {{QStringLiteral("kind"), item.kind},
          {QStringLiteral("leftText"), item.leftText},
          {QStringLiteral("rightText"), item.rightText},
          {QStringLiteral("leftLine"), item.leftLine},
          {QStringLiteral("rightLine"), item.rightLine},
          {QStringLiteral("hunkId"), item.hunkId},
          {QStringLiteral("conflictId"),
           item.conflictId.isEmpty() ? item.hunkId : item.conflictId},
          {QStringLiteral("leftValue"),
           item.leftValue.isValid() ? item.leftValue : QVariant(item.leftText)},
          {QStringLiteral("rightValue"), item.rightValue.isValid()
                                             ? item.rightValue
                                             : QVariant(item.rightText)}};
}

QVariantMap DiffModel::hunkAt(int index) const {
  if (index < 0 || index >= hunkCount())
    return {};
  const DiffHunk hunk =
      m_provider ? m_provider->hunkAt(index) : m_hunkData[index];
  return {{QStringLiteral("id"), hunk.id},
          {QStringLiteral("conflictId"),
           hunk.conflictId.isEmpty() ? hunk.id : hunk.conflictId},
          {QStringLiteral("row"), hunk.row},
          {QStringLiteral("leftStart"), hunk.leftStart},
          {QStringLiteral("leftCount"), hunk.leftCount},
          {QStringLiteral("rightStart"), hunk.rightStart},
          {QStringLiteral("rightCount"), hunk.rightCount}};
}

bool DiffModel::setPageProvider(DiffPageProvider *provider,
                                const QString &leftRevision,
                                const QString &rightRevision) {
  if (!provider || leftRevision.isEmpty() || rightRevision.isEmpty())
    return fail(QStringLiteral("Provider and both revisions are required"));
  if (provider->regionCount() < 0 || provider->hunkCount() < 0)
    return fail(QStringLiteral("Invalid provider counts"));
  if (m_provider && m_provider != provider && m_leftRevision == leftRevision &&
      m_rightRevision == rightRevision)
    return fail(QStringLiteral("Provider changed without a revision change"));
  if (m_adapter) {
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
      m_adapter->cancelDecision(it.key());
  }
  m_pending.clear();
  disconnect(m_providerDestroyed);
  beginResetModel();
  m_provider = provider;
  m_rows.clear();
  m_hunkData.clear();
  m_hunks.clear();
  m_left.clear();
  m_right.clear();
  m_leftRevision = leftRevision;
  m_rightRevision = rightRevision;
  m_error.clear();
  endResetModel();
  m_providerDestroyed = connect(provider, &QObject::destroyed, this, [this] {
    if (m_adapter) {
      for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
        m_adapter->cancelDecision(it.key());
    }
    m_pending.clear();
    beginResetModel();
    m_provider = nullptr;
    m_leftRevision.clear();
    m_rightRevision.clear();
    endResetModel();
    emit stateChanged();
  });
  emit stateChanged();
  return true;
}

bool DiffModel::fail(const QString &message) {
  m_error = message;
  emit stateChanged();
  return false;
}

void DiffModel::setTransactionAdapter(DiffTransactionAdapter *adapter) {
  if (m_adapter == adapter)
    return;
  if (m_adapter) {
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
      m_adapter->cancelDecision(it.key());
  }
  m_pending.clear();
  m_adapter = adapter;
  emit stateChanged();
}

bool DiffModel::setDocuments(const QStringList &left, const QStringList &right,
                             const QString &leftRevision,
                             const QString &rightRevision) {
  if (leftRevision.isEmpty() || rightRevision.isEmpty())
    return fail(QStringLiteral("Both document revisions are required"));
  if (left.size() > MaxLines || right.size() > MaxLines)
    return fail(QStringLiteral("Diff exceeds 512 lines per side"));
  for (const QString &line : left)
    if (line.size() > MaxLineLength)
      return fail(QStringLiteral("Left line exceeds 2048 characters"));
  for (const QString &line : right)
    if (line.size() > MaxLineLength)
      return fail(QStringLiteral("Right line exceeds 2048 characters"));
  if (m_leftRevision == leftRevision && m_rightRevision == rightRevision &&
      (m_left != left || m_right != right))
    return fail(QStringLiteral("Content changed without a revision change"));

  // AGENT-NOTE (docs/diff-view.md): full LCS is deliberately bounded by
  // MaxLines squared; it gives deterministic alignment even for repeats.
  const int n = left.size(), m = right.size(), stride = m + 1;
  QVector<int> lcs((n + 1) * stride);
  for (int i = n - 1; i >= 0; --i) {
    for (int j = m - 1; j >= 0; --j) {
      const int at = i * stride + j;
      lcs[at] = left[i] == right[j]
                    ? 1 + lcs[(i + 1) * stride + j + 1]
                    : qMax(lcs[(i + 1) * stride + j], lcs[i * stride + j + 1]);
    }
  }
  QVector<Step> steps;
  int i = 0, j = 0;
  while (i < n || j < m) {
    if (i < n && j < m && left[i] == right[j]) {
      steps.append({'=', i++, j++});
    } else if (i < n && (j == m || lcs[(i + 1) * stride + j] >=
                                       lcs[i * stride + j + 1])) {
      steps.append({'-', i++, -1});
    } else {
      steps.append({'+', -1, j++});
    }
  }

  QVector<DiffRegion> rows;
  QVector<DiffHunk> hunks;
  QVariantList hunkMaps;
  QHash<QString, int> repeatedIds;
  int cursor = 0;
  while (cursor < steps.size()) {
    if (steps[cursor].kind == '=') {
      const Step &step = steps[cursor++];
      rows.append({QStringLiteral("equal"),
                   left[step.left],
                   right[step.right],
                   {},
                   step.left + 1,
                   step.right + 1,
                   {}, {}, {}});
      continue;
    }
    const int start = cursor;
    int leftStart = n, rightStart = m, leftCount = 0, rightCount = 0;
    while (cursor < steps.size() && steps[cursor].kind != '=') {
      const Step &step = steps[cursor++];
      if (step.left >= 0) {
        leftStart = qMin(leftStart, step.left);
        ++leftCount;
      }
      if (step.right >= 0) {
        rightStart = qMin(rightStart, step.right);
        ++rightCount;
      }
    }
    if (leftCount == 0)
      leftStart = start > 0 ? steps[start - 1].left + 1 : 0;
    if (rightCount == 0)
      rightStart = start > 0 ? steps[start - 1].right + 1 : 0;
    const QString before = start > 0 && steps[start - 1].kind == '='
                               ? left[steps[start - 1].left]
                               : QString();
    const QString after = cursor < steps.size() && steps[cursor].kind == '='
                              ? left[steps[cursor].left]
                              : QString();
    const QString key =
        fingerprint(left, right, leftStart, leftStart + leftCount, rightStart,
                    rightStart + rightCount, before, after);
    const int duplicate = repeatedIds[key]++;
    const QString id =
        duplicate ? key + QStringLiteral(":%1").arg(duplicate) : key;
    DiffHunk hunk{id, static_cast<int>(rows.size()), leftStart,
                  leftCount, rightStart, rightCount, {}};
    for (int k = 0; k < qMax(leftCount, rightCount); ++k) {
      rows.append({QStringLiteral("change"),
                   k < leftCount ? left[leftStart + k] : QString(),
                   k < rightCount ? right[rightStart + k] : QString(), id,
                   k < leftCount ? leftStart + k + 1 : 0,
                   k < rightCount ? rightStart + k + 1 : 0,
                   {}, {}, {}});
    }
    hunks.append(hunk);
    hunkMaps.append(QVariantMap{{QStringLiteral("id"), id},
                                {QStringLiteral("conflictId"), id},
                                {QStringLiteral("row"), hunk.row},
                                {QStringLiteral("leftStart"), leftStart},
                                {QStringLiteral("leftCount"), leftCount},
                                {QStringLiteral("rightStart"), rightStart},
                                {QStringLiteral("rightCount"), rightCount}});
    if (hunks.size() > MaxHunks)
      return fail(QStringLiteral("Diff exceeds 128 hunks"));
  }
  if (m_adapter) {
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it)
      m_adapter->cancelDecision(it.key());
  }
  m_pending.clear();
  disconnect(m_providerDestroyed);
  beginResetModel();
  m_provider = nullptr;
  m_rows = std::move(rows);
  m_hunkData = std::move(hunks);
  m_hunks = std::move(hunkMaps);
  m_left = left;
  m_right = right;
  m_leftRevision = leftRevision;
  m_rightRevision = rightRevision;
  m_error.clear();
  endResetModel();
  emit stateChanged();
  return true;
}

int DiffModel::hunkRow(const QString &hunkId) const {
  for (int index = 0; index < hunkCount(); ++index) {
    const DiffHunk hunk =
        m_provider ? m_provider->hunkAt(index) : m_hunkData[index];
    if (hunk.id == hunkId)
      return hunk.row;
  }
  return -1;
}

QString DiffModel::requestDecision(const QString &hunkId, const QString &action,
                                   const QString &expectedLeftRevision,
                                   const QString &expectedRightRevision) {
  if (!m_adapter) {
    fail(QStringLiteral("No transaction adapter"));
    return {};
  }
  if (action != QStringLiteral("accept") &&
      action != QStringLiteral("reject")) {
    fail(QStringLiteral("Unknown diff action"));
    return {};
  }
  if (expectedLeftRevision != m_leftRevision ||
      expectedRightRevision != m_rightRevision) {
    fail(QStringLiteral("Stale document revision"));
    return {};
  }
  for (int index = 0; index < hunkCount(); ++index) {
    const DiffHunk hunk =
        m_provider ? m_provider->hunkAt(index) : m_hunkData[index];
    if (hunk.id != hunkId)
      continue;
    DiffDecision decision;
    decision.requestId = QStringLiteral("diff-%1").arg(m_nextRequest++);
    decision.hunkId = hunk.id;
    decision.conflictId = hunk.conflictId.isEmpty() ? hunk.id : hunk.conflictId;
    decision.action = action;
    decision.leftRevision = m_leftRevision;
    decision.rightRevision = m_rightRevision;
    decision.leftStart = hunk.leftStart;
    decision.leftCount = hunk.leftCount;
    decision.replacement = m_provider ? m_provider->replacementFor(hunk, action)
                           : action == QStringLiteral("accept")
                               ? m_right.mid(hunk.rightStart, hunk.rightCount)
                               : m_left.mid(hunk.leftStart, hunk.leftCount);
    if (!m_adapter->submitDecision(decision)) {
      fail(QStringLiteral("Transaction adapter rejected request"));
      return {};
    }
    m_pending.insert(decision.requestId, decision);
    m_error.clear();
    emit stateChanged();
    emit decisionRequested(decision.requestId, hunkId, action);
    return decision.requestId;
  }
  fail(QStringLiteral("Hunk no longer exists"));
  return {};
}

bool DiffModel::cancelDecision(const QString &requestId) {
  if (!m_pending.contains(requestId))
    return false;
  if (m_adapter)
    m_adapter->cancelDecision(requestId);
  m_pending.remove(requestId);
  return true;
}
} // namespace QindaTK
