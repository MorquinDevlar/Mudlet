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

#include "EditorPackageActiveCommand.h"

#include "Host.h"

EditorPackageActiveCommand::EditorPackageActiveCommand(EditorViewType viewType, const QString& packageName, const bool newState, Host* host)
: EditorCommand(generateText(packageName, newState), host)
, mViewType(viewType)
, mPackageName(packageName)
, mNewState(newState)
{
}

// No first-redo skip, unlike EditorToggleActiveCommand: the switch is thrown
// here rather than by the site that pushes this, so the push is what does it
void EditorPackageActiveCommand::redo()
{
    if (mpHost) {
        mpHost->setPackageEnabled(mPackageName, mNewState);
    }
}

void EditorPackageActiveCommand::undo()
{
    if (mpHost) {
        mpHost->setPackageEnabled(mPackageName, !mNewState);
    }
}

QString EditorPackageActiveCommand::generateText(const QString& packageName, const bool newState)
{
    if (newState) {
        //: Undo/redo menu text for switching a whole package back on. %1 is the package's name
        return QObject::tr("Turn on package %1").arg(packageName);
    }
    //: Undo/redo menu text for switching a whole package off. %1 is the package's name
    return QObject::tr("Turn off package %1").arg(packageName);
}
