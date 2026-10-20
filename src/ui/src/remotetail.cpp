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

#include "remotetail.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QUrlQuery>

#include "log.h"
#include "windowscredentials.h"

namespace {

constexpr int MaxErrorOutputSize = 64 * 1024;

QString shellQuote( const QString& value )
{
    auto quoted = value;
    quoted.replace( '\'', QStringLiteral( "'\\''" ) );
    return QStringLiteral( "'%1'" ).arg( quoted );
}

QString findSshProgram()
{
    const auto configured = qEnvironmentVariable( "KLOGG_SSH" );
    if ( !configured.isEmpty() ) {
        return configured;
    }

    auto program = QStandardPaths::findExecutable( QStringLiteral( "ssh" ) );
#ifdef Q_OS_WIN
    if ( program.isEmpty() ) {
        const QFileInfo systemSsh(
            QStringLiteral( "C:/Windows/System32/OpenSSH/ssh.exe" ) );
        if ( systemSsh.exists() ) {
            program = systemSsh.absoluteFilePath();
        }
    }
#endif

    return program.isEmpty() ? QStringLiteral( "ssh" ) : program;
}

QString sanitizedFileName( const QString& remotePath )
{
    auto name = QFileInfo( remotePath ).fileName();
    name.replace( QRegularExpression( QStringLiteral( "[^A-Za-z0-9._-]" ) ),
                  QStringLiteral( "_" ) );
    if ( name.isEmpty() ) {
        name = QStringLiteral( "remote.log" );
    }
    return name;
}

} // namespace

bool isSshUrl( const QString& fileName )
{
    return QUrl( fileName ).scheme().compare( QStringLiteral( "ssh" ), Qt::CaseInsensitive ) == 0;
}

QString defaultSshCredentialTarget( const QUrl& url )
{
    if ( !url.isValid() || url.host().isEmpty() ) {
        return {};
    }

    auto target = QStringLiteral( "klogg-ssh:" );
    const auto user = url.userName( QUrl::FullyDecoded );
    if ( !user.isEmpty() ) {
        target += user;
        target += QLatin1Char( '@' );
    }
    target += url.host();

    const auto port = url.port( 22 );
    if ( port > 0 && port != 22 ) {
        target += QLatin1Char( ':' );
        target += QString::number( port );
    }

    return target;
}

RemoteTail::RemoteTail( QObject* parent )
    : QObject( parent )
    , tempDir_( QDir::temp().filePath( QStringLiteral( "klogg-ssh-XXXXXX" ) ) )
    , process_( new QProcess( this ) )
{
}

RemoteTail::~RemoteTail()
{
    stop();
}

bool RemoteTail::isRunning() const
{
    return process_ != nullptr && process_->state() != QProcess::NotRunning;
}

bool RemoteTail::start( const QUrl& url, QString* errorString )
{
    if ( errorString != nullptr ) {
        errorString->clear();
    }

    if ( url.scheme().compare( QStringLiteral( "ssh" ), Qt::CaseInsensitive ) != 0 ) {
        if ( errorString != nullptr ) {
            *errorString = tr( "Not an ssh:// URL" );
        }
        return false;
    }

    const auto host = url.host( QUrl::FullyDecoded );
    const auto remotePath = url.path( QUrl::FullyDecoded );
    if ( host.isEmpty() || remotePath.isEmpty() ) {
        if ( errorString != nullptr ) {
            *errorString = tr( "SSH URL must contain a host and a remote path" );
        }
        return false;
    }

    if ( remotePath.contains( QLatin1Char( '\n' ) )
         || remotePath.contains( QLatin1Char( '\r' ) ) ) {
        if ( errorString != nullptr ) {
            *errorString = tr( "Remote path contains invalid characters" );
        }
        return false;
    }

    const auto user = url.userName( QUrl::FullyDecoded );
    if ( user.startsWith( QLatin1Char( '-' ) ) || host.startsWith( QLatin1Char( '-' ) ) ) {
        if ( errorString != nullptr ) {
            *errorString = tr( "Invalid SSH user or host" );
        }
        return false;
    }

    if ( !tempDir_.isValid() ) {
        if ( errorString != nullptr ) {
            *errorString = tr( "Failed to create a temporary directory for the remote tail" );
        }
        return false;
    }

    sshUrl_ = url.toString( QUrl::FullyEncoded );
    spoolPath_ = tempDir_.filePath( QStringLiteral( "tail-" )
                                    + sanitizedFileName( remotePath ) );

    QFile spoolFile{ spoolPath_ };
    if ( !spoolFile.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        if ( errorString != nullptr ) {
            *errorString = tr( "Failed to create remote tail file: %1" )
                               .arg( spoolFile.errorString() );
        }
        return false;
    }
    spoolFile.close();

    auto hostForSsh = host;
    if ( hostForSsh.contains( QLatin1Char( ':' ) )
         && !hostForSsh.startsWith( QLatin1Char( '[' ) ) ) {
        hostForSsh = QStringLiteral( "[%1]" ).arg( hostForSsh );
    }

    const auto destination
        = user.isEmpty() ? hostForSsh : QStringLiteral( "%1@%2" ).arg( user, hostForSsh );

    QUrlQuery urlQuery{ url };
    QString credentialTarget;
    if ( urlQuery.hasQueryItem( QStringLiteral( "credential" ) ) ) {
        credentialTarget
            = urlQuery.queryItemValue( QStringLiteral( "credential" ), QUrl::FullyDecoded );
        if ( credentialTarget.compare( QStringLiteral( "none" ), Qt::CaseInsensitive ) == 0 ) {
            credentialTarget.clear();
        }
    }
    else {
        credentialTarget = defaultSshCredentialTarget( url );
    }

    const auto useAskPass
        = !credentialTarget.isEmpty() && windowsCredentialExists( credentialTarget );

    QStringList arguments;
    arguments << QStringLiteral( "-T" )
              << QStringLiteral( "-o" )
              << ( useAskPass ? QStringLiteral( "BatchMode=no" )
                              : QStringLiteral( "BatchMode=yes" ) )
              << QStringLiteral( "-o" ) << QStringLiteral( "StrictHostKeyChecking=accept-new" )
              << QStringLiteral( "-o" ) << QStringLiteral( "ConnectTimeout=20" )
              << QStringLiteral( "-o" ) << QStringLiteral( "ServerAliveInterval=30" )
              << QStringLiteral( "-o" ) << QStringLiteral( "ServerAliveCountMax=3" )
              << QStringLiteral( "-o" ) << QStringLiteral( "NumberOfPasswordPrompts=1" );

    const auto port = url.port( 22 );
    if ( port > 0 && port != 22 ) {
        arguments << QStringLiteral( "-p" ) << QString::number( port );
    }

    arguments << QStringLiteral( "--" ) << destination;
    arguments << QStringLiteral( "exec tail -F -n +1 -- %1" ).arg( shellQuote( remotePath ) );

    auto environment = QProcessEnvironment::systemEnvironment();
    if ( useAskPass ) {
        auto askPassProgram = QCoreApplication::applicationFilePath();
#ifdef Q_OS_WIN
        askPassProgram = QDir::toNativeSeparators( askPassProgram );
#endif
        environment.insert( QStringLiteral( "SSH_ASKPASS" ), askPassProgram );
        environment.insert( QStringLiteral( "SSH_ASKPASS_REQUIRE" ), QStringLiteral( "force" ) );
        environment.insert( QStringLiteral( "DISPLAY" ), QStringLiteral( ":0" ) );
        environment.insert( QStringLiteral( "KLOGG_SSH_ASKPASS" ), QStringLiteral( "1" ) );
        environment.insert( QStringLiteral( "KLOGG_SSH_CREDENTIAL_TARGET" ), credentialTarget );
    }
    else if ( !credentialTarget.isEmpty() ) {
        LOG_INFO << "No Windows Credential Manager entry for " << credentialTarget
                 << ", falling back to SSH keys or agent";
    }

    process_->setProcessEnvironment( environment );
    process_->setProcessChannelMode( QProcess::SeparateChannels );
    process_->setStandardOutputFile( spoolPath_, QIODevice::WriteOnly | QIODevice::Truncate );

    connect( process_, &QProcess::readyReadStandardError, this, [ this ]() {
        errorOutput_.append( process_->readAllStandardError() );
        if ( errorOutput_.size() > MaxErrorOutputSize ) {
            errorOutput_ = errorOutput_.right( MaxErrorOutputSize );
        }
    } );

    connect( process_, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), this,
             [ this ]( int exitCode, QProcess::ExitStatus exitStatus ) {
                 if ( stopping_ ) {
                     return;
                 }

                 errorOutput_.append( process_->readAllStandardError() );

                 if ( exitStatus == QProcess::CrashExit ) {
                     reportFailure( tr( "ssh tail process crashed" ) );
                 }
                 else if ( exitCode != 0 ) {
                     reportFailure( tr( "ssh tail exited with code %1" ).arg( exitCode ) );
                 }
                 else {
                     LOG_INFO << "SSH tail for " << sshUrl_ << " finished";
                 }
             } );

    connect( process_, &QProcess::errorOccurred, this, [ this ]( QProcess::ProcessError error ) {
        if ( error == QProcess::FailedToStart ) {
            reportFailure( tr( "Failed to start ssh: %1" ).arg( process_->errorString() ) );
        }
        else if ( error == QProcess::Crashed ) {
            reportFailure( tr( "ssh tail process crashed" ) );
        }
    } );

    LOG_INFO << "Starting remote tail for " << sshUrl_ << " using " << findSshProgram();
    process_->start( findSshProgram(), arguments );

    if ( !process_->waitForStarted( 15000 ) ) {
        reportFailure( tr( "Failed to start ssh: %1" ).arg( process_->errorString() ) );
        if ( errorString != nullptr ) {
            *errorString = tr( "Failed to start ssh: %1" ).arg( process_->errorString() );
        }
        return false;
    }

    LOG_INFO << "Remote tail started, local spool file " << spoolPath_;
    return true;
}

void RemoteTail::stop()
{
    if ( process_ == nullptr || process_->state() == QProcess::NotRunning ) {
        return;
    }

    stopping_ = true;
    process_->terminate();
    if ( !process_->waitForFinished( 2000 ) ) {
        process_->kill();
        process_->waitForFinished( 2000 );
    }
}

QString RemoteTail::takeErrorOutput()
{
    errorOutput_.append( process_->readAllStandardError() );
    return QString::fromLocal8Bit( errorOutput_ ).trimmed();
}

void RemoteTail::reportFailure( const QString& message )
{
    if ( failureReported_ || stopping_ ) {
        return;
    }

    failureReported_ = true;

    auto fullMessage = message;
    const auto details = takeErrorOutput();
    if ( !details.isEmpty() ) {
        fullMessage += QStringLiteral( "\n" );
        fullMessage += details;
    }

    LOG_ERROR << "Remote tail for " << sshUrl_ << " failed: " << fullMessage;
    Q_EMIT failed( fullMessage );
}

RemoteTailManager::RemoteTailManager( QObject* parent )
    : QObject( parent )
{
}

RemoteTailManager& RemoteTailManager::instance()
{
    static auto* manager = new RemoteTailManager( QCoreApplication::instance() );
    return *manager;
}

QString RemoteTailManager::ensureTail( const QString& sshUrl, QString* errorString )
{
    if ( errorString != nullptr ) {
        errorString->clear();
    }

    const QUrl url{ sshUrl };
    if ( !url.isValid() || url.scheme().compare( QStringLiteral( "ssh" ), Qt::CaseInsensitive )
                               != 0 ) {
        if ( errorString != nullptr ) {
            *errorString = tr( "Invalid ssh:// URL" );
        }
        return {};
    }

    const auto key = url.toString( QUrl::FullyEncoded );
    const auto existing = tails_.find( key );
    if ( existing != tails_.end() ) {
        if ( existing->second->isRunning() ) {
            return existing->second->localFilePath();
        }

        // Keep the old spool file available for tabs that still display it,
        // but allow reconnecting when the URL is opened again.
        retiredTails_.push_back( std::move( existing->second ) );
        tails_.erase( existing );
    }

    auto tail = std::make_unique<RemoteTail>( this );
    connect( tail.get(), &RemoteTail::failed, this,
             [ this, key ]( const QString& message ) { Q_EMIT remoteTailFailed( key, message ); } );

    const auto started = tail->start( url, errorString );
    const auto localFile = tail->localFilePath();

    if ( localFile.isEmpty() ) {
        const auto message = ( errorString != nullptr && !errorString->isEmpty() )
                                 ? *errorString
                                 : tr( "Failed to start remote tail" );
        Q_EMIT remoteTailFailed( key, message );
        return {};
    }

    if ( !started ) {
        LOG_ERROR << "Remote tail failed to start for " << key;
        if ( errorString != nullptr && errorString->isEmpty() ) {
            *errorString = tr( "Failed to start remote tail" );
        }
    }

    tails_.emplace( key, std::move( tail ) );
    return localFile;
}
