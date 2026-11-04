#pragma once

// Small controls of the panel: segmented buttons for choices of two to four options (visible at
// a glance, one click, touch friendly) and collapsible sections for what is rarely needed.

#include <QColor>
#include <QIcon>
#include <QList>
#include <QStringList>
#include <QWidget>

class QButtonGroup;
class QToolButton;

namespace Gui
{

class Segmented : public QWidget
{
    Q_OBJECT

public:
    explicit Segmented(const QStringList& labels, QWidget* parent = nullptr);

    int  Index() const;
    void SetIndex(int index);                             // does not emit IndexChanged()
    void SetAccent(const QColor& color);                  // colour of the selected segment
    void SetToolTips(const QStringList& tips);            // one per segment
    void SetIcons(const QList<QIcon>& icons);             // one per segment, instead of the labels
    void SetSegmentColor(int index, const QColor& color); // for a segment named after a channel

    // Independent toggles instead of one choice: use IsChecked() and SetChecked().
    void SetIndependent();
    bool IsChecked(int index) const;
    void SetChecked(int index, bool checked); // does not emit IndexChanged()

signals:
    void IndexChanged(int index); // by the user only

private:
    QButtonGroup* m_group = nullptr;
};

class Section : public QWidget
{
    Q_OBJECT

public:
    explicit Section(const QString& title, bool expanded, QWidget* parent = nullptr);

    QWidget* Body() const { return m_body; } // give it a layout with the section's controls

private:
    QToolButton* m_header = nullptr;
    QWidget*     m_body   = nullptr;
};

}
