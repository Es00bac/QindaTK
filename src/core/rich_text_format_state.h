// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class RichTextArea;

// Effective formatting of the active selection for FormatBar. Values are
// resolved (heading and document defaults applied, a pending typing format
// included); toggles report Qt.CheckState values: 0 off, 1 mixed, 2 on.
// Refreshing is O(covered runs); a collapsed selection reads one format.
class RichTextFormatState : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY changed)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)
    Q_PROPERTY(bool fontFamilyMixed READ fontFamilyMixed NOTIFY changed)
    Q_PROPERTY(bool fontMissing READ fontMissing NOTIFY changed)
    Q_PROPERTY(double fontSize READ fontSize NOTIFY changed)
    Q_PROPERTY(bool fontSizeMixed READ fontSizeMixed NOTIFY changed)
    Q_PROPERTY(int bold READ bold NOTIFY changed)
    Q_PROPERTY(int italic READ italic NOTIFY changed)
    Q_PROPERTY(int underline READ underline NOTIFY changed)
    Q_PROPERTY(int strike READ strike NOTIFY changed)
    Q_PROPERTY(int superscript READ superscript NOTIFY changed)
    Q_PROPERTY(int subscript READ subscript NOTIFY changed)
    Q_PROPERTY(QColor color READ color NOTIFY changed)
    Q_PROPERTY(bool colorMixed READ colorMixed NOTIFY changed)
    Q_PROPERTY(QString link READ link NOTIFY changed)
    Q_PROPERTY(bool linkMixed READ linkMixed NOTIFY changed)
    Q_PROPERTY(QString alignment READ alignment NOTIFY changed)
    Q_PROPERTY(bool alignmentMixed READ alignmentMixed NOTIFY changed)
    Q_PROPERTY(int heading READ heading NOTIFY changed)
    Q_PROPERTY(bool headingMixed READ headingMixed NOTIFY changed)
    Q_PROPERTY(QString list READ list NOTIFY changed)          // "none", "bullet", "number" or "mixed"
public:
    explicit RichTextFormatState(RichTextArea *area);
    void refresh();

    bool editable() const { return m_editable; }
    bool hasSelection() const { return m_hasSelection; }
    QString fontFamily() const { return m_family; }
    bool fontFamilyMixed() const { return m_familyMixed; }
    bool fontMissing() const { return m_fontMissing; }
    double fontSize() const { return m_size; }
    bool fontSizeMixed() const { return m_sizeMixed; }
    int bold() const { return m_bold; }
    int italic() const { return m_italic; }
    int underline() const { return m_underline; }
    int strike() const { return m_strike; }
    int superscript() const { return m_superscript; }
    int subscript() const { return m_subscript; }
    QColor color() const { return m_color; }
    bool colorMixed() const { return m_colorMixed; }
    QString link() const { return m_link; }
    bool linkMixed() const { return m_linkMixed; }
    QString alignment() const { return m_alignment; }
    bool alignmentMixed() const { return m_alignmentMixed; }
    int heading() const { return m_heading; }
    bool headingMixed() const { return m_headingMixed; }
    QString list() const { return m_list; }

signals:
    void changed();

private:
    RichTextArea *m_area = nullptr;
    bool m_editable = false;
    bool m_hasSelection = false;
    QString m_family;
    bool m_familyMixed = false;
    bool m_fontMissing = false;
    double m_size = 0;
    bool m_sizeMixed = false;
    int m_bold = 0, m_italic = 0, m_underline = 0, m_strike = 0, m_superscript = 0, m_subscript = 0;
    QColor m_color;
    bool m_colorMixed = false;
    QString m_link;
    bool m_linkMixed = false;
    QString m_alignment = QStringLiteral("start");
    bool m_alignmentMixed = false;
    int m_heading = 0;
    bool m_headingMixed = false;
    QString m_list = QStringLiteral("none");
};

} // namespace QindaTK
