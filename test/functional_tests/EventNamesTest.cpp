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

/*
 * Where the event names the script editor offers come from: the hand-kept list
 * of the ones Mudlet raises, and the reading of a piece of Lua for the ones it
 * names itself.
 *
 * Run with: ctest -R EventNamesTest -V
 */

#include <QSet>
#include <QtTest/QtTest>

#include "EventNames.h"
#include "utils.h"

#include "GroupedTest.h"

class EventNamesTest : public QObject
{
    Q_OBJECT

private slots:
    // Both calls that raise one and both that register a handler for one, in
    // either quote; a name that is worked out at run time is not there to be
    // read, and one written twice is one name
    void test_theNamesInAPieceOfLuaAreRead()
    {
        const QString code = qsl("raiseEvent(\"a\", 1)\n"
                                 "raiseEvent('b')\n"
                                 "raiseGlobalEvent(\"g\")\n"
                                 "registerAnonymousEventHandler(\"c\", myHandler)\n"
                                 "registerNamedEventHandler(\"u\", \"h\", \"d\", myHandler)\n"
                                 "raiseEvent(someVariable)\n"
                                 "raiseEvent(\"a\", 2)\n");

        const QStringList found = eventNames::namesInCode(code);

        QCOMPARE(found, QStringList({qsl("a"), qsl("b"), qsl("g"), qsl("c"), qsl("d")}));
        QVERIFY2(!found.contains(qsl("someVariable")), "a name the code works out at run time was read as a literal");
    }

    // A quote only ends the name it opened, and neither runs past the end of
    // its line - so an unclosed one costs that call rather than the rest of the
    // file
    void test_aQuoteOnlyEndsItsOwnKind()
    {
        QCOMPARE(eventNames::namesInCode(qsl("raiseEvent(\"it's here\")")), QStringList{qsl("it's here")});
        QCOMPARE(eventNames::namesInCode(qsl("raiseEvent(\"unclosed\nraiseEvent(\"after\")")), QStringList{qsl("after")});
    }

    // A name the game sent is told by the protocol table it is named after,
    // which nothing else in a profile is named for
    void test_theGamesNamesAreKnownByTheirShape()
    {
        QVERIFY2(eventNames::fromGame(qsl("gmcp.Char.Vitals")), "a GMCP key was not read as the game's");
        QVERIFY2(eventNames::fromGame(qsl("msdp.ROOM")), "an MSDP variable was not read as the game's");
        QVERIFY2(eventNames::fromGame(qsl("mssp")), "the MSSP table's own name was not read as the game's");
        QVERIFY2(eventNames::fromGame(qsl("mxp.entity")), "an MXP tag was not read as the game's");
        QVERIFY2(!eventNames::fromGame(qsl("sysLoadEvent")), "an event Mudlet raises was read as the game's");
        QVERIFY2(!eventNames::fromGame(qsl("gmcpish")), "a name that only starts with a protocol's letters was read as the game's");
        QVERIFY2(!eventNames::fromGame(qsl("MyEvent")), "a script's own name was read as the game's");
        QVERIFY2(!eventNames::fromGame(QString()), "a name with nothing in it was read as the game's");
    }

    void test_theSystemEventsAreListedOnceEach()
    {
        const QStringList system = eventNames::systemEvents();

        QVERIFY2(system.contains(qsl("sysConnectionEvent")), "the list is missing an event Mudlet raises from C++");
        QVERIFY2(system.contains(qsl("sysSpeedwalkStarted")), "the list is missing an event Mudlet raises from its own Lua");
        QCOMPARE(QSet<QString>(system.constBegin(), system.constEnd()).size(), system.size());
    }
};

#include "EventNamesTest.moc"
MUDLET_GROUPED_TEST_MAIN(EventNamesTest)
