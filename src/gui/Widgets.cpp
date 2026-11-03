// Segmented buttons and collapsible sections. Their look is in the application stylesheet
// (gui/Theme.cpp); the properties set here select the rules there.

#include "gui/Widgets.hpp"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>

namespace Gui
{

Segmented::Segmented(const QStringList& labels, QWidget* parent)
    : QWidget(parent), m_group(new QButtonGroup(this))
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = new QToolButton;
        b->setText(labels.at(i));
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus); // Space stays Run/Stop
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const char* pos = i == 0 ? "first" : i == labels.size() - 1 ? "last" : "middle";
        b->setProperty("segment", QString::fromLatin1(pos));
        m_group->addButton(b, i);
        row->addWidget(b);
    }
    connect(m_group, &QButtonGroup::idClicked, this, &Segmented::IndexChanged);
}

int Segmented::Index() const
{
    return m_group->checkedId();
}

void Segmented::SetIndex(int index)
{
    if (QAbstractButton* b = m_group->button(index))
    {
        b->setChecked(true);
    }
}

void Segmented::SetIndependent()
{
    m_group->setExclusive(false);
}

bool Segmented::IsChecked(int index) const
{
    const QAbstractButton* b = m_group->button(index);
    return b != nullptr && b->isChecked();
}

void Segmented::SetChecked(int index, bool checked)
{
    if (QAbstractButton* b = m_group->button(index))
    {
        b->setChecked(checked);
    }
}

void Segmented::SetAccent(const QColor& color)
{
    setStyleSheet(QStringLiteral("QToolButton:checked { background: %1; border-color: %1;"
                                 " color: black; }")
                      .arg(color.name()));
}

void Segmented::SetToolTips(const QStringList& tips)
{
    for (int i = 0; i < tips.size(); ++i)
    {
        if (QAbstractButton* b = m_group->button(i))
        {
            b->setToolTip(tips.at(i));
        }
    }
}

Section::Section(const QString& title, bool expanded, QWidget* parent)
    : QWidget(parent), m_header(new QToolButton), m_body(new QWidget)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    m_header->setText(title);
    m_header->setCheckable(true);
    m_header->setChecked(expanded);
    m_header->setFocusPolicy(Qt::NoFocus);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_header->setProperty("section", true);
    m_body->setVisible(expanded);
    v->addWidget(m_header);
    v->addWidget(m_body);
    connect(m_header, &QToolButton::toggled, this,
            [this](bool on)
            {
                m_header->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
                m_body->setVisible(on);
            });
}

}
