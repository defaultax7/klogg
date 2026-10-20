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

#include "windowscredentials.h"

#include <QByteArray>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>
#endif

namespace {

void setError( QString* errorString, const QString& message )
{
    if ( errorString != nullptr ) {
        *errorString = message;
    }
}

#ifdef Q_OS_WIN
QString windowsErrorString( DWORD errorCode )
{
    wchar_t* buffer = nullptr;
    const auto length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
        reinterpret_cast<LPWSTR>( &buffer ), 0, nullptr );

    QString message;
    if ( length > 0 && buffer != nullptr ) {
        message = QString::fromWCharArray( buffer, static_cast<int>( length ) ).trimmed();
    }
    if ( buffer != nullptr ) {
        LocalFree( buffer );
    }

    if ( message.isEmpty() ) {
        message = QStringLiteral( "Windows error %1" ).arg( errorCode );
    }

    return message;
}
#endif

} // namespace

bool windowsCredentialExists( const QString& target )
{
#ifdef Q_OS_WIN
    if ( target.isEmpty() ) {
        return false;
    }

    PCREDENTIALW credential = nullptr;
    if ( !CredReadW( reinterpret_cast<LPCWSTR>( target.utf16() ), CRED_TYPE_GENERIC, 0,
                     &credential ) ) {
        return false;
    }

    CredFree( credential );
    return true;
#else
    Q_UNUSED( target )
    return false;
#endif
}

QString readWindowsCredentialPassword( const QString& target, QString* errorString )
{
    setError( errorString, {} );

#ifdef Q_OS_WIN
    if ( target.isEmpty() ) {
        setError( errorString, QStringLiteral( "Credential target is empty" ) );
        return {};
    }

    PCREDENTIALW credential = nullptr;
    if ( !CredReadW( reinterpret_cast<LPCWSTR>( target.utf16() ), CRED_TYPE_GENERIC, 0,
                     &credential ) ) {
        setError( errorString, windowsErrorString( GetLastError() ) );
        return {};
    }

    const QByteArray blob( reinterpret_cast<const char*>( credential->CredentialBlob ),
                           static_cast<int>( credential->CredentialBlobSize ) );
    CredFree( credential );

    QString password;
    if ( blob.size() >= 2 && blob.size() % 2 == 0 ) {
        password = QString::fromUtf16( reinterpret_cast<const char16_t*>( blob.constData() ),
                                       blob.size() / 2 );
    }
    else {
        password = QString::fromUtf8( blob );
    }

    // cmdkey stores passwords as UTF-16 and may include a terminating NUL.
    while ( password.endsWith( QChar( 0 ) ) ) {
        password.chop( 1 );
    }

    return password;
#else
    Q_UNUSED( target )
    setError( errorString,
              QStringLiteral( "Windows Credential Manager is only available on Windows" ) );
    return {};
#endif
}
