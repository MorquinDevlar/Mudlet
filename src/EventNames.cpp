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

#include "EventNames.h"

#include "utils.h"

#include <QRegularExpression>

namespace eventNames {

bool fromGame(const QString& name)
{
    // The tables a protocol fills in, and the events raised as it is filled:
    // one per level of a GMCP key (gmcp.Char, then gmcp.Char.Vitals), msdp.ROOM,
    // mssp.PLAYERS, mxp.entity. ATCP is here for its table's name alone -
    // cTelnet::setATCPVariables() takes the dots out and raises the variable's
    // own name, so there is no atcp.* event to tell from a script's.
    static const QStringList protocols{qsl("gmcp"), qsl("msdp"), qsl("mssp"), qsl("mxp"), qsl("atcp")};
    for (const QString& protocol : protocols) {
        if (name == protocol) {
            return true;
        }
        if (name.size() > protocol.size() && name.at(protocol.size()) == QLatin1Char('.') && name.startsWith(protocol)) {
            return true;
        }
    }
    return false;
}

QStringList systemEvents()
{
    // By hand, and so out of date the moment a new event is raised without
    // being added here. To gather the candidates again, grep the tree for the
    // literals beside the two places an event is put together:
    //   grep -rEn '"sys[A-Za-z0-9]+"' src/*.cpp src/mudlet-lua/lua | \
    //     grep -E 'mArgumentList.append|raiseEvent\(|raiseProtocolEvent\('
    // The five speedwalk events are raised by Mudlet's own Lua rather than by
    // its C++; the rest come from src/*.cpp. Not every name starts with sys:
    // the map, the speech and the channel 102 events are Mudlet's too. Sorted without regard to case,
    // which is the order they are offered in.
    static const QStringList names{qsl("channel102Message"),
                                   qsl("mapModeChangeEvent"),
                                   qsl("mapOpenEvent"),
                                   qsl("sysApplicationFocusChangeEvent"),
                                   qsl("sysAppStyleSheetChange"),
                                   qsl("sysBufferShrinkEvent"),
                                   qsl("sysCommandClicked"),
                                   qsl("sysCommandLineDeleted"),
                                   qsl("sysConnectionEvent"),
                                   qsl("sysConsoleSizeChanged"),
                                   qsl("sysCustomHttpDone"),
                                   qsl("sysCustomHttpError"),
                                   qsl("sysDataSendRequest"),
                                   qsl("sysDeleteHttpDone"),
                                   qsl("sysDeleteHttpError"),
                                   qsl("sysDisconnectionEvent"),
                                   qsl("sysDownloadDone"),
                                   qsl("sysDownloadError"),
                                   qsl("sysDownloadFileProgress"),
                                   qsl("sysDropEvent"),
                                   qsl("sysDropUrlEvent"),
                                   qsl("sysExitEvent"),
                                   qsl("sysFontChangeEvent"),
                                   qsl("sysGetHttpDone"),
                                   qsl("sysGetHttpError"),
                                   qsl("sysInstall"),
                                   qsl("sysInstallModule"),
                                   qsl("sysInstallPackage"),
                                   qsl("sysIrcMessage"),
                                   qsl("sysLabelDeleted"),
                                   qsl("sysLoadEvent"),
                                   qsl("sysLuaInstallModule"),
                                   qsl("sysLuaUninstallModule"),
                                   qsl("sysManualLocationSetEvent"),
                                   qsl("sysMapAreaChanged"),
                                   qsl("sysMapDownloadEvent"),
                                   qsl("sysMapperButtonAction"),
                                   qsl("sysMapWindowMousePressEvent"),
                                   qsl("sysMediaFinished"),
                                   qsl("sysMediaPaused"),
                                   qsl("sysMediaStarted"),
                                   qsl("sysMiniConsoleDeleted"),
                                   qsl("sysPathChanged"),
                                   qsl("sysPostHttpDone"),
                                   qsl("sysPostHttpError"),
                                   qsl("sysProfileFocusChangeEvent"),
                                   qsl("sysProtocolDisabled"),
                                   qsl("sysProtocolEnabled"),
                                   qsl("sysPutHttpDone"),
                                   qsl("sysPutHttpError"),
                                   qsl("sysScrollBoxDeleted"),
                                   qsl("sysServerGuiInstalled"),
                                   qsl("sysSettingChanged"),
                                   qsl("sysSpeedwalkFinished"),
                                   qsl("sysSpeedwalkPaused"),
                                   qsl("sysSpeedwalkResumed"),
                                   qsl("sysSpeedwalkStarted"),
                                   qsl("sysSpeedwalkStopped"),
                                   qsl("sysSTTCapabilitiesChanged"),
                                   qsl("sysSTTError"),
                                   qsl("sysSTTPartialResult"),
                                   qsl("sysSTTResult"),
                                   qsl("sysSTTStateChanged"),
                                   qsl("sysSTTWords"),
                                   qsl("sysSyncInstallModule"),
                                   qsl("sysSyncUninstallModule"),
                                   qsl("sysTelnetEvent"),
                                   qsl("sysTextEditDeleted"),
                                   qsl("sysUiTourFinished"),
                                   qsl("sysUninstall"),
                                   qsl("sysUninstallModule"),
                                   qsl("sysUninstallPackage"),
                                   qsl("sysUnzipDone"),
                                   qsl("sysUnzipError"),
                                   qsl("sysUserWindowResizeEvent"),
                                   qsl("sysWindowMousePressEvent"),
                                   qsl("sysWindowMouseReleaseEvent"),
                                   qsl("sysWindowOverflowEvent"),
                                   qsl("sysWindowResizeEvent"),
                                   qsl("ttsPitchChanged"),
                                   qsl("ttsRateChanged"),
                                   qsl("ttsSpeechError"),
                                   qsl("ttsSpeechPaused"),
                                   qsl("ttsSpeechQueued"),
                                   qsl("ttsSpeechReady"),
                                   qsl("ttsSpeechStarted"),
                                   qsl("ttsSpeechSynthesizing"),
                                   qsl("ttsVoiceChanged"),
                                   qsl("ttsVolumeChanged")};
    return names;
}

QStringList namesInCode(const QString& code)
{
    // Two shapes in one pass, so that the names come out in the order they are
    // written rather than one call's before the other's. The closing quote is a
    // back-reference to the opening one, which is what keeps an apostrophe
    // inside a double-quoted name from ending it, and the newline the class
    // bars is what stops a run-on match swallowing half the file when a quote
    // is left unclosed. registerNamedEventHandler names the event third, and
    // the two arguments skipped over are matched loosely - anything but a
    // comma, a bracket or a line end - since either may be a literal or a
    // variable.
    static const QRegularExpression regex{qsl(R"((?:\b(?:raiseEvent|raiseGlobalEvent|registerAnonymousEventHandler)\s*\(\s*(["'])([^\n]*?)\1)"
                                              R"()|(?:\bregisterNamedEventHandler\s*\(\s*[^,()\n]+,\s*[^,()\n]+,\s*(["'])([^\n]*?)\3))")};

    QStringList names;
    QRegularExpressionMatchIterator matches = regex.globalMatch(code);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString name = match.captured(2).isEmpty() ? match.captured(4) : match.captured(2);
        if (name.isEmpty() || names.contains(name)) {
            continue;
        }
        names << name;
    }
    return names;
}

} // namespace eventNames
