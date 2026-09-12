#ifndef MUDLET_EDITORPACKAGEACTIVECOMMAND_H
#define MUDLET_EDITORPACKAGEACTIVECOMMAND_H

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

#include "EditorCommand.h"

#include <QString>

// Switching a whole package on or off from one of the editor's trees. Unlike
// EditorToggleActiveCommand, which holds the id of the one item it switched,
// this holds only the package's name: Host::setPackageEnabled() switches every
// trigger, alias, timer, script, key and button the package installed, and
// which items those are is a question the Host answers again at undo time
// rather than one that can be settled when the command is pushed.
class EditorPackageActiveCommand : public EditorCommand
{
public:
    EditorPackageActiveCommand(EditorViewType viewType, const QString& packageName, const bool newState, Host* host);

    void undo() override;
    void redo() override;

    EditorViewType viewType() const override { return mViewType; }

    // Nothing, and nothing is needed. The stack hands these ids to
    // dlgTriggerEditor::slot_itemsChanged() so that rows which were rebuilt can
    // be found again under their new items - but a package switch rebuilds no
    // row at all: Host::setPackageEnabled() asks the editor only for
    // refreshPackageState(), which repaints the trees and rewrites their
    // accessible descriptions in place. Every item, and every id, is still the
    // one it was.
    QList<int> affectedItemIDs() const override { return {}; }

    // Ditto: what is held here is a package's name, and no delete and re-add of
    // an item changes that
    void remapItemID(int, int) override {}

private:
    static QString generateText(const QString& packageName, const bool newState);

    EditorViewType mViewType;
    QString mPackageName;
    bool mNewState;
};

#endif // MUDLET_EDITORPACKAGEACTIVECOMMAND_H
