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

#ifndef KLOGG_REMOTETAIL_H
#define KLOGG_REMOTETAIL_H

#include <memory>
#include <unordered_map>
#include <vector>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>

class QProcess;

// True for ssh:// URLs handled by RemoteTail.
bool isSshUrl( const QString& fileName );

// Default Windows Credential Manager target for an ssh URL:
// klogg-ssh:user@host or klogg-ssh:user@host:port
QString defaultSshCredentialTarget( const QUrl& url );

enum class SshHostKeyChecking {
    Strict,
    AcceptNew,
    Disabled,
};

// Reads the optional hostKeyChecking URL query parameter.
// Default: AcceptNew. Only disable this for disposable test environments.
SshHostKeyChecking sshHostKeyChecking( const QUrl& url );

// Runs "ssh ... tail -F" and writes its output to a local spool file that
// klogg can open as a normal growing log file.
class RemoteTail : public QObject {
    Q_OBJECT
public:
    explicit RemoteTail( QObject* parent = nullptr );
    ~RemoteTail() override;

    RemoteTail( const RemoteTail& ) = delete;
    RemoteTail& operator=( const RemoteTail& ) = delete;

    bool start( const QUrl& url, QString* errorString = nullptr );

    QString localFilePath() const
    {
        return spoolPath_;
    }

    bool isRunning() const;

Q_SIGNALS:
    void failed( const QString& message );

private:
    void stop();
    void reportFailure( const QString& message );
    QString takeErrorOutput();

    QTemporaryDir tempDir_;
    QString spoolPath_;
    QString sshUrl_;
    QProcess* process_ = nullptr;
    QByteArray errorOutput_;
    bool stopping_ = false;
    bool failureReported_ = false;
};

// Keeps one RemoteTail per ssh URL and exposes the local spool file paths.
class RemoteTailManager : public QObject {
    Q_OBJECT
public:
    static RemoteTailManager& instance();

    // Starts the tail if needed and returns the local file path to open.
    QString ensureTail( const QString& sshUrl, QString* errorString = nullptr );

Q_SIGNALS:
    void remoteTailFailed( const QString& sshUrl, const QString& message );

private:
    explicit RemoteTailManager( QObject* parent );

    std::unordered_map<QString, std::unique_ptr<RemoteTail>> tails_;
    std::vector<std::unique_ptr<RemoteTail>> retiredTails_;
};

#endif
