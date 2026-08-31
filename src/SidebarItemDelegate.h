#ifndef MUDLET_SIDEBARITEMDELEGATE_H
#define MUDLET_SIDEBARITEMDELEGATE_H

/***************************************************************************
 *   Copyright (C) 2026 by Vadim Peretokin - vadim.peretokin@mudlet.org    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <QStyledItemDelegate>

class QListWidget;
class QModelIndex;
class QStyleOptionViewItem;

namespace uiDesign {

// Collapsed, the sidebar shows a category as its icon alone. Emptying the item's
// text would do that too, but the text is what a screen reader announces the row
// as - so it is the drawing that leaves it out rather than the data.
class SidebarItemDelegate : public QStyledItemDelegate
{
public:
    explicit SidebarItemDelegate(QListWidget* pList);

    void initStyleOption(QStyleOptionViewItem* pOption, const QModelIndex& index) const override;

private:
    QListWidget* mpList = nullptr;
};

} // namespace uiDesign

#endif // MUDLET_SIDEBARITEMDELEGATE_H
