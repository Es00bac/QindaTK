// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_equation_editor.h"
#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// The visual view of Tk.EquationEditor (WP-052, OFF-09): paints the laid-out
// equation with a caret and selection in its expression tree and edits the
// tree from keys, clicks and palette requests (RichText::Equation::Editor).
// AGENT-CONTRACT: `source` is the canonical TeX-subset source both ways.
// Setting it re-parses (a source that does not parse is kept as `error`
// and the tree stays); every visual edit writes the new canonical source
// and emits edited(source). docs/references-review.md#visual-editing.
class EquationCanvas : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString linearText READ linearText NOTIFY sourceChanged)
    Q_PROPERTY(QString error READ error NOTIFY sourceChanged)
    Q_PROPERTY(QString caretPath READ caretPath NOTIFY caretChanged)
    Q_PROPERTY(QString selectedSource READ selectedSource NOTIFY caretChanged)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY appearanceChanged)
    Q_PROPERTY(double fontSizePt READ fontSizePt WRITE setFontSizePt NOTIFY appearanceChanged)
    Q_PROPERTY(double padding READ padding WRITE setPadding NOTIFY appearanceChanged)
    Q_PROPERTY(QColor inkColor READ inkColor WRITE setInkColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor caretColor READ caretColor WRITE setCaretColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor placeholderColor READ placeholderColor WRITE setPlaceholderColor NOTIFY appearanceChanged)
    Q_PROPERTY(double contentWidth READ contentWidth NOTIFY sourceChanged)
    Q_PROPERTY(double contentHeight READ contentHeight NOTIFY sourceChanged)
public:
    explicit EquationCanvas(QQuickItem *parent = nullptr);

    QString source() const { return m_source; }
    void setSource(const QString &source);
    QString linearText() const;
    QString error() const { return m_error; }
    QString caretPath() const { return m_editor.caretPath(); }
    QString selectedSource() const { return m_editor.selectedSource(); }
    bool readOnly() const { return m_readOnly; }
    void setReadOnly(bool readOnly);
    double fontSizePt() const { return m_sizePt; }
    void setFontSizePt(double size);
    double padding() const { return m_padding; }
    void setPadding(double padding);
    QColor inkColor() const { return m_ink; }
    void setInkColor(const QColor &color);
    QColor caretColor() const { return m_caret; }
    void setCaretColor(const QColor &color);
    QColor selectionColor() const { return m_selection; }
    void setSelectionColor(const QColor &color);
    QColor placeholderColor() const { return m_placeholder; }
    void setPlaceholderColor(const QColor &color);
    double contentWidth() const; // px, the laid-out equation with padding
    double contentHeight() const;

    // Caret movement: "left", "right", "up", "down", "home", "end".
    Q_INVOKABLE bool moveCaret(const QString &direction, bool extend = false);
    Q_INVOKABLE bool insertText(const QString &text);
    Q_INVOKABLE bool insertSymbol(const QString &symbolId);
    Q_INVOKABLE bool backspace();
    Q_INVOKABLE bool deleteForward();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE bool setCaretAt(qreal x, qreal y, bool extend = false); // item coordinates
    Q_INVOKABLE QRectF caretRectangle() const;                            // item coordinates

    void paint(QPainter *painter) override;

signals:
    void sourceChanged();
    void caretChanged();
    void appearanceChanged();
    void edited(const QString &source); // a visual edit, with the new canonical source

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    bool apply(bool changed);
    void relayout();
    double scale() const;      // px per point
    QPointF origin() const;    // item position of the box's baseline start

    RichText::Equation::Editor m_editor;
    RichText::Equation::Box m_box;
    QString m_source;
    QString m_error;
    bool m_readOnly = false;
    double m_sizePt = 16;
    double m_padding = 8;
    QColor m_ink = Qt::black;
    QColor m_caret = Qt::black;
    QColor m_selection = QColor(0, 120, 215, 60);
    QColor m_placeholder = Qt::gray;
};

} // namespace QindaTK
