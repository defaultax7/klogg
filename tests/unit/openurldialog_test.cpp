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

#include <catch2/catch.hpp>

#include <QCheckBox>
#include <QGroupBox>
#include <QUrl>
#include <QUrlQuery>

#include "openurldialog.h"

SCENARIO( "Open URL dialog SSH options", "[ui]" )
{
    GIVEN( "an empty URL" )
    {
        OpenUrlDialog dialog;

        auto* sshOptionsGroup
            = dialog.findChild<QGroupBox*>( QStringLiteral( "sshOptionsGroup" ) );
        auto* disableHostKeyCheckBox
            = dialog.findChild<QCheckBox*>( QStringLiteral( "disableHostKeyCheckBox" ) );

        REQUIRE( sshOptionsGroup != nullptr );
        REQUIRE( disableHostKeyCheckBox != nullptr );

        THEN( "the SSH options are clickable before a URL is entered" )
        {
            REQUIRE( sshOptionsGroup->isEnabled() );
            REQUIRE( disableHostKeyCheckBox->isEnabled() );
        }

    }

    GIVEN( "a fresh SSH URL" )
    {
        OpenUrlDialog dialog;
        dialog.setUrl( QStringLiteral( "ssh://user@host/var/log/app.log" ) );

        auto* sshOptionsGroup
            = dialog.findChild<QGroupBox*>( QStringLiteral( "sshOptionsGroup" ) );
        auto* credentialCheckBox
            = dialog.findChild<QCheckBox*>( QStringLiteral( "useCredentialCheckBox" ) );
        auto* disableHostKeyCheckBox
            = dialog.findChild<QCheckBox*>( QStringLiteral( "disableHostKeyCheckBox" ) );
        auto* strictHostKeyCheckBox
            = dialog.findChild<QCheckBox*>( QStringLiteral( "strictHostKeyCheckBox" ) );

        REQUIRE( sshOptionsGroup != nullptr );
        REQUIRE( sshOptionsGroup->isEnabled() );
        REQUIRE( credentialCheckBox != nullptr );
        REQUIRE( disableHostKeyCheckBox != nullptr );
        REQUIRE( strictHostKeyCheckBox != nullptr );

        THEN( "all options are unchecked by default" )
        {
            REQUIRE_FALSE( credentialCheckBox->isChecked() );
            REQUIRE_FALSE( disableHostKeyCheckBox->isChecked() );
            REQUIRE_FALSE( strictHostKeyCheckBox->isChecked() );
        }

        WHEN( "accepted with defaults" )
        {
            dialog.accept();

            THEN( "keys are used and host-key checking keeps the default" )
            {
                REQUIRE( dialog.result() == QDialog::Accepted );

                const QUrlQuery query{ QUrl{ dialog.url() } };
#ifdef Q_OS_WIN
                REQUIRE( query.queryItemValue( QStringLiteral( "credential" ) )
                         == QStringLiteral( "none" ) );
#endif
                REQUIRE_FALSE( query.hasQueryItem( QStringLiteral( "hostKeyChecking" ) ) );
            }
        }
    }

    GIVEN( "host-key options" )
    {
        WHEN( "host-key checking is disabled" )
        {
            OpenUrlDialog dialog;
            dialog.setUrl( QStringLiteral( "ssh://user@host/var/log/app.log" ) );
            auto* disableHostKeyCheckBox
                = dialog.findChild<QCheckBox*>( QStringLiteral( "disableHostKeyCheckBox" ) );
            REQUIRE( disableHostKeyCheckBox != nullptr );
            disableHostKeyCheckBox->setChecked( true );
            dialog.accept();

            THEN( "the URL contains hostKeyChecking=no" )
            {
                const QUrlQuery query{ QUrl{ dialog.url() } };
                REQUIRE( query.queryItemValue( QStringLiteral( "hostKeyChecking" ) )
                         == QStringLiteral( "no" ) );
            }
        }

        WHEN( "strict host-key checking is requested" )
        {
            OpenUrlDialog dialog;
            dialog.setUrl( QStringLiteral( "ssh://user@host/var/log/app.log" ) );
            auto* strictHostKeyCheckBox
                = dialog.findChild<QCheckBox*>( QStringLiteral( "strictHostKeyCheckBox" ) );
            REQUIRE( strictHostKeyCheckBox != nullptr );
            strictHostKeyCheckBox->setChecked( true );
            dialog.accept();

            THEN( "the URL contains hostKeyChecking=yes" )
            {
                const QUrlQuery query{ QUrl{ dialog.url() } };
                REQUIRE( query.queryItemValue( QStringLiteral( "hostKeyChecking" ) )
                         == QStringLiteral( "yes" ) );
            }
        }
    }

    GIVEN( "an SSH URL with existing options" )
    {
        OpenUrlDialog dialog;
        dialog.setUrl( QStringLiteral(
            "ssh://user@host/var/log/app.log?credential=none&hostKeyChecking=no" ) );

        auto* disableHostKeyCheckBox
            = dialog.findChild<QCheckBox*>( QStringLiteral( "disableHostKeyCheckBox" ) );
        REQUIRE( disableHostKeyCheckBox != nullptr );

        THEN( "the options are reflected in the dialog" )
        {
            REQUIRE( disableHostKeyCheckBox->isChecked() );
        }

        WHEN( "accepted without changes" )
        {
            dialog.accept();

            THEN( "the options are preserved" )
            {
                const QUrlQuery query{ QUrl{ dialog.url() } };
                REQUIRE( query.queryItemValue( QStringLiteral( "hostKeyChecking" ) )
                         == QStringLiteral( "no" ) );
#ifdef Q_OS_WIN
                REQUIRE( query.queryItemValue( QStringLiteral( "credential" ) )
                         == QStringLiteral( "none" ) );
#endif
            }
        }
    }

    GIVEN( "a non-SSH URL" )
    {
        OpenUrlDialog dialog;
        dialog.setUrl( QStringLiteral( "https://example.com/file.log" ) );
        dialog.accept();

        THEN( "the URL is returned unchanged" )
        {
            REQUIRE( dialog.result() == QDialog::Accepted );
            REQUIRE( dialog.url() == QStringLiteral( "https://example.com/file.log" ) );
        }
    }
}
