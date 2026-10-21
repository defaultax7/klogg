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

#include "openurldialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "remotetail.h"
#include "windowscredentials.h"

OpenUrlDialog::OpenUrlDialog( QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Open URL as log file" ) );

    auto* mainLayout = new QVBoxLayout( this );

    mainLayout->addWidget( new QLabel( tr( "URL to download or open over SSH:" ), this ) );

    urlEdit_ = new QLineEdit( this );
    urlEdit_->setObjectName( QStringLiteral( "openUrlEdit" ) );
    urlEdit_->setPlaceholderText(
        tr( "https://example.com/file.log or ssh://user@host/path/to/file.log" ) );
    urlEdit_->setClearButtonEnabled( true );
    mainLayout->addWidget( urlEdit_ );

    sshOptionsGroup_ = new QGroupBox( tr( "SSH options" ), this );
    sshOptionsGroup_->setObjectName( QStringLiteral( "sshOptionsGroup" ) );
    auto* sshLayout = new QVBoxLayout( sshOptionsGroup_ );

    credentialCheckBox_ = new QCheckBox( tr( "Use Windows Credential Manager" ), sshOptionsGroup_ );
    credentialCheckBox_->setObjectName( QStringLiteral( "useCredentialCheckBox" ) );
    credentialCheckBox_->setToolTip(
        tr( "When unchecked, SSH keys or ssh-agent are used instead" ) );
    sshLayout->addWidget( credentialCheckBox_ );

    credentialRow_ = new QWidget( sshOptionsGroup_ );
    auto* credentialLayout = new QHBoxLayout( credentialRow_ );
    credentialLayout->setContentsMargins( 20, 0, 0, 0 );
    credentialLayout->addWidget( new QLabel( tr( "Credential target:" ), credentialRow_ ) );
    credentialTargetEdit_ = new QLineEdit( credentialRow_ );
    credentialTargetEdit_->setObjectName( QStringLiteral( "credentialTargetEdit" ) );
    credentialTargetEdit_->setPlaceholderText( tr( "klogg-ssh:user@host" ) );
    credentialLayout->addWidget( credentialTargetEdit_, 1 );
    sshLayout->addWidget( credentialRow_ );

    disableHostKeyCheckBox_
        = new QCheckBox( tr( "Disable SSH host key checking (insecure)" ), sshOptionsGroup_ );
    disableHostKeyCheckBox_->setObjectName( QStringLiteral( "disableHostKeyCheckBox" ) );
    disableHostKeyCheckBox_->setToolTip(
        tr( "Only use this for disposable local test servers. "
            "The real known_hosts file is not modified." ) );
    sshLayout->addWidget( disableHostKeyCheckBox_ );

    strictHostKeyCheckBox_
        = new QCheckBox( tr( "Require strict SSH host key checking" ), sshOptionsGroup_ );
    strictHostKeyCheckBox_->setObjectName( QStringLiteral( "strictHostKeyCheckBox" ) );
    sshLayout->addWidget( strictHostKeyCheckBox_ );

    auto* note = new QLabel(
        tr( "With both host-key options unchecked, new host keys are accepted and changed "
            "host keys are rejected." ),
        sshOptionsGroup_ );
    note->setWordWrap( true );
    sshLayout->addWidget( note );

    mainLayout->addWidget( sshOptionsGroup_ );

    auto* buttonBox
        = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    connect( buttonBox, &QDialogButtonBox::accepted, this, &OpenUrlDialog::accept );
    connect( buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject );
    mainLayout->addWidget( buttonBox );

#ifndef Q_OS_WIN
    credentialCheckBox_->setVisible( false );
    credentialRow_->setVisible( false );
#endif

    connect( urlEdit_, &QLineEdit::textChanged, this, &OpenUrlDialog::updateForUrl );
    connect( credentialCheckBox_, &QCheckBox::toggled, this,
             &OpenUrlDialog::updateCredentialState );
    connect( disableHostKeyCheckBox_, &QCheckBox::toggled, this, [ this ]( bool checked ) {
        if ( checked ) {
            strictHostKeyCheckBox_->setChecked( false );
        }
    } );
    connect( strictHostKeyCheckBox_, &QCheckBox::toggled, this, [ this ]( bool checked ) {
        if ( checked ) {
            disableHostKeyCheckBox_->setChecked( false );
        }
    } );

    resize( 620, sizeHint().height() );
    updateForUrl();
    urlEdit_->setFocus();
}

void OpenUrlDialog::setUrl( const QString& url )
{
    urlEdit_->setText( url );

    const QUrl parsedUrl{ url };
    const QUrlQuery query{ parsedUrl };

    QString credentialTarget = defaultSshCredentialTarget( parsedUrl );
    auto useCredential = false;

    if ( query.hasQueryItem( QStringLiteral( "credential" ) ) ) {
        const auto queryTarget
            = query.queryItemValue( QStringLiteral( "credential" ), QUrl::FullyDecoded ).trimmed();
        useCredential = !queryTarget.isEmpty()
                        && queryTarget.compare( QStringLiteral( "none" ),
                                                Qt::CaseInsensitive )
                               != 0;
        if ( useCredential ) {
            credentialTarget = queryTarget;
        }
    }

#ifdef Q_OS_WIN
    {
        const QSignalBlocker blocker( credentialCheckBox_ );
        credentialCheckBox_->setChecked( useCredential );
    }
#else
    Q_UNUSED( useCredential )
#endif

    credentialTargetEdit_->setText( credentialTarget );

    const auto hostKeyMode = sshHostKeyChecking( parsedUrl );
    {
        const QSignalBlocker disableBlocker( disableHostKeyCheckBox_ );
        const QSignalBlocker strictBlocker( strictHostKeyCheckBox_ );
        disableHostKeyCheckBox_->setChecked( hostKeyMode == SshHostKeyChecking::Disabled );
        strictHostKeyCheckBox_->setChecked( hostKeyMode == SshHostKeyChecking::Strict );
    }

    updateForUrl();
}

QString OpenUrlDialog::url() const
{
    return resultUrl_.isEmpty() ? urlEdit_->text().trimmed() : resultUrl_;
}

QString OpenUrlDialog::defaultCredentialTarget() const
{
    return defaultSshCredentialTarget( QUrl{ urlEdit_->text().trimmed() } );
}

void OpenUrlDialog::updateForUrl()
{
    const auto isRemote = isSshUrl( urlEdit_->text().trimmed() );

    // Keep the options visible and clickable at all times. They are only
    // applied when the entered URL is an ssh:// URL.
    sshOptionsGroup_->setEnabled( true );
    sshOptionsGroup_->setTitle( isRemote
                                    ? tr( "SSH options" )
                                    : tr( "SSH options (applied to ssh:// URLs)" ) );

    if ( isRemote && !wasSshUrl_ && credentialTargetEdit_->text().trimmed().isEmpty() ) {
        credentialTargetEdit_->setText( defaultCredentialTarget() );
    }

    wasSshUrl_ = isRemote;
    updateCredentialState();
    adjustSize();
}

void OpenUrlDialog::updateCredentialState()
{
    const auto useCredential = credentialCheckBox_->isChecked();

    credentialRow_->setVisible( useCredential );
    credentialTargetEdit_->setEnabled( useCredential );
}

QString OpenUrlDialog::buildResultUrl() const
{
    const auto enteredUrl = urlEdit_->text().trimmed();
    if ( !isSshUrl( enteredUrl ) ) {
        return enteredUrl;
    }

    QUrl url{ enteredUrl };
    QUrlQuery query{ url };

    query.removeAllQueryItems( QStringLiteral( "credential" ) );
    query.removeAllQueryItems( QStringLiteral( "hostKeyChecking" ) );

#ifdef Q_OS_WIN
    if ( credentialCheckBox_->isChecked() ) {
        query.addQueryItem( QStringLiteral( "credential" ),
                            credentialTargetEdit_->text().trimmed() );
    }
    else {
        query.addQueryItem( QStringLiteral( "credential" ), QStringLiteral( "none" ) );
    }
#endif

    if ( disableHostKeyCheckBox_->isChecked() ) {
        query.addQueryItem( QStringLiteral( "hostKeyChecking" ), QStringLiteral( "no" ) );
    }
    else if ( strictHostKeyCheckBox_->isChecked() ) {
        query.addQueryItem( QStringLiteral( "hostKeyChecking" ), QStringLiteral( "yes" ) );
    }

    url.setQuery( query );
    return url.toString( QUrl::FullyEncoded );
}

void OpenUrlDialog::accept()
{
    const auto enteredUrl = urlEdit_->text().trimmed();
    if ( enteredUrl.isEmpty() ) {
        QMessageBox::warning( this, tr( "Open URL" ), tr( "Enter a URL first." ) );
        return;
    }

    if ( isSshUrl( enteredUrl ) ) {
        const QUrl url{ enteredUrl };
        if ( url.host().isEmpty() || url.path().isEmpty() ) {
            QMessageBox::warning( this, tr( "Open SSH log" ),
                                  tr( "SSH URL must have the form ssh://user@host/path" ) );
            return;
        }

#ifdef Q_OS_WIN
        if ( credentialCheckBox_->isChecked() ) {
            const auto credentialTarget = credentialTargetEdit_->text().trimmed();
            if ( credentialTarget.isEmpty() ) {
                QMessageBox::warning(
                    this, tr( "Windows Credential Manager" ),
                    tr( "Enter a credential target or uncheck Windows Credential Manager." ) );
                return;
            }

            if ( !windowsCredentialExists( credentialTarget ) ) {
                QMessageBox::warning(
                    this, tr( "Windows Credential Manager" ),
                    tr( "No credential found for target \"%1\".\n\n"
                        "Create one with:\n"
                        "cmdkey /generic:\"%1\" /user:<user> /pass:<password>" )
                        .arg( credentialTarget ) );
                return;
            }
        }
#endif
    }

    resultUrl_ = buildResultUrl();
    QDialog::accept();
}
