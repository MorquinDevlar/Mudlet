#ifndef MUDLET_EVENTNAMES_H
#define MUDLET_EVENTNAMES_H

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

#include <QStringList>

// Where the names a script can listen for are come by, so that typing one into
// the editor can be a search rather than a recollection.
namespace eventNames {

// Where a name is from, which is the one word set beside it in the editor's list
enum class Source { Mudlet, Game, Script };

// One name the editor can offer, and where it is from
struct Entry
{
    QString name;
    Source source;
};

// Whether a name is one the game sent: the protocol tables' own naming,
// gmcp.Char.Vitals and the like, which nothing else in a profile uses
bool fromGame(const QString& name);

// Every event Mudlet raises itself, sorted, for the editor to offer. Does not
// include the protocol events (gmcp.*, msdp.*, mssp.*, mxp.*) since those are
// named after whatever the game sends; see fromGame().
QStringList systemEvents();

// The event names a piece of Lua raises or registers a handler for, read off
// its string literals: raiseEvent("x"), raiseGlobalEvent('x'),
// registerAnonymousEventHandler("x", ...), registerNamedEventHandler(user, handler, "x", ...).
// Unique, in order of first appearance. A name built at runtime is not found.
QStringList namesInCode(const QString& code);

} // namespace eventNames

#endif // MUDLET_EVENTNAMES_H
