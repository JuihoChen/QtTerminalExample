/*
    Copyright 2013 Christian Surlykke

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301  USA.
*/
#include <QMenu>
#include <QAction>
#include <QRegExp>
#include <QDebug>

#include "SearchBar.h"

SearchBar::SearchBar(QWidget *parent) : QWidget(parent)
{
    widget.setupUi(this);
    setAutoFillBackground(true); // make it always opaque, especially inside translucent windows
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "#SearchBar { background: palette(window); border-top: 1px solid rgba(0, 0, 0, 0.15); }"
        "#SearchBar QToolButton { border: 1px solid transparent; border-radius: 4px;"
        "  min-width: 22px; min-height: 22px; padding: 0px 4px; }"
        "#SearchBar QToolButton:hover { background: rgba(0, 0, 0, 0.08); }"
        "#SearchBar QToolButton:pressed { background: rgba(0, 0, 0, 0.14); }"
        "#SearchBar QToolButton:checked { background: rgba(0, 102, 204, 0.14);"
        "  border-color: rgba(0, 102, 204, 0.55); color: #0066cc; }"
        "#SearchBar QToolButton:disabled { color: rgba(0, 0, 0, 0.3); }"
        "#statusLabel { color: #666; padding-left: 4px; }"));

    connect(widget.closeButton, SIGNAL(clicked()), this, SLOT(hide()));
    connect(widget.searchTextEdit, SIGNAL(textChanged(QString)), this, SIGNAL(searchCriteriaChanged()));
    connect(widget.findPreviousButton, SIGNAL(clicked()), this, SIGNAL(findPrevious()));
    connect(widget.findNextButton, SIGNAL(clicked()), this, SIGNAL(findNext()));

    connect(this, SIGNAL(searchCriteriaChanged()), this, SLOT(clearBackgroundColor()));
    connect(widget.searchTextEdit, &QLineEdit::textChanged, this, &SearchBar::updateButtons);

    m_matchCaseMenuEntry = new QAction(tr("Aa"), this);
    m_matchCaseMenuEntry->setToolTip(tr("Match case"));
    m_matchCaseMenuEntry->setCheckable(true);
    m_matchCaseMenuEntry->setChecked(false);
    widget.matchCaseButton->setDefaultAction(m_matchCaseMenuEntry);
    connect(m_matchCaseMenuEntry, SIGNAL(toggled(bool)), this, SIGNAL(searchCriteriaChanged()));

    m_useRegularExpressionMenuEntry = new QAction(tr(".*"), this);
    m_useRegularExpressionMenuEntry->setToolTip(tr("Regular expression"));
    m_useRegularExpressionMenuEntry->setCheckable(true);
    widget.regExpButton->setDefaultAction(m_useRegularExpressionMenuEntry);
    connect(m_useRegularExpressionMenuEntry, SIGNAL(toggled(bool)), this, SIGNAL(searchCriteriaChanged()));

    // Kept for the API; highlighting every match isn't implemented, so it has no button
    m_highlightMatchesMenuEntry = new QAction(tr("Highlight all matches"), this);
    m_highlightMatchesMenuEntry->setCheckable(true);
    m_highlightMatchesMenuEntry->setChecked(true);
    connect(m_highlightMatchesMenuEntry, SIGNAL(toggled(bool)), this, SIGNAL(highlightMatchesChanged(bool)));

    updateButtons();
}

SearchBar::~SearchBar() {
}

QString SearchBar::searchText()
{
    return widget.searchTextEdit->text();
}


bool SearchBar::useRegularExpression()
{
    return m_useRegularExpressionMenuEntry->isChecked();
}

bool SearchBar::matchCase()
{
    return m_matchCaseMenuEntry->isChecked();
}

bool SearchBar::highlightAllMatches()
{
    return m_highlightMatchesMenuEntry->isChecked();
}

void SearchBar::show()
{
    QWidget::show();
    widget.searchTextEdit->setFocus();
    widget.searchTextEdit->selectAll();
}

void SearchBar::noMatchFound()
{
    widget.searchTextEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: #fdecea; border: 1px solid #e0a39c; border-radius: 3px; padding: 1px 2px; }"));
    widget.statusLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
    widget.statusLabel->setText(tr("No matches"));
}

void SearchBar::updateButtons()
{
    const bool hasText = !widget.searchTextEdit->text().isEmpty();
    widget.findPreviousButton->setEnabled(hasText);
    widget.findNextButton->setEnabled(hasText);
}

void SearchBar::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    Q_EMIT closed();
}


void SearchBar::keyReleaseEvent(QKeyEvent* keyEvent)
{
    if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
    {
        // A terminal's newest output is at the bottom, so Enter keeps searching
        // up into older output and Shift+Enter comes back down
        if (keyEvent->modifiers() == Qt::ShiftModifier)
        {
            Q_EMIT findNext();
        }
        else
        {
            Q_EMIT findPrevious();
        }
    }
    else if (keyEvent->key() == Qt::Key_Escape)
    {
        hide();
    }
}

void SearchBar::clearBackgroundColor()
{
    widget.searchTextEdit->setStyleSheet(QString());
    widget.statusLabel->clear();

}
