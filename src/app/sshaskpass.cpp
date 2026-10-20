/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sshaskpass.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <cstdio>
#include <cstddef>

#include "windowscredentials.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
void writeError( const QString& message )
{
    const auto data = message.toLocal8Bit();
    std::fwrite( data.constData(), 1, static_cast<std::size_t>( data.size() ), stderr );
    std::fputc( '\n', stderr );
    std::fflush( stderr );
}

bool writePassword( const QByteArray& password )
{
#ifdef Q_OS_WIN
    const auto output = GetStdHandle( STD_OUTPUT_HANDLE );
    if ( output != nullptr && output != INVALID_HANDLE_VALUE ) {
        DWORD written = 0;
        const auto ok = WriteFile( output, password.constData(),
                                   static_cast<DWORD>( password.size() ), &written, nullptr );
        return ok && written == static_cast<DWORD>( password.size() );
    }
#endif

    const auto written
        = std::fwrite( password.constData(), 1, static_cast<std::size_t>( password.size() ),
                       stdout );
    std::fflush( stdout );
    return written == static_cast<std::size_t>( password.size() );
}
} // namespace

int runSshAskPassIfRequested()
{
    if ( qEnvironmentVariableIsEmpty( "KLOGG_SSH_ASKPASS" ) ) {
        return -1;
    }

    const auto target = qEnvironmentVariable( "KLOGG_SSH_CREDENTIAL_TARGET" );
    if ( target.isEmpty() ) {
        writeError( QStringLiteral( "Klogg SSH askpass: credential target is not set" ) );
        return 1;
    }

    QString errorString;
    const auto password = readWindowsCredentialPassword( target, &errorString );
    if ( password.isEmpty() ) {
        writeError( QStringLiteral( "Klogg SSH askpass: %1" )
                        .arg( errorString.isEmpty()
                                  ? QStringLiteral( "credential not found" )
                                  : errorString ) );
        return 1;
    }

    auto output = password.toUtf8();
    output.append( '\n' );

    if ( !writePassword( output ) ) {
        writeError( QStringLiteral( "Klogg SSH askpass: failed to write password" ) );
        return 1;
    }

    return 0;
}
