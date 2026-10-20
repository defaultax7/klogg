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

#include <QUrl>

#include "remotetail.h"

SCENARIO( "SSH remote tail URL helpers", "[ui]" )
{
    GIVEN( "an ssh URL" )
    {
        const auto url = QUrl( QStringLiteral( "ssh://alice@example.com/var/log/app.log" ) );

        THEN( "it is recognized as an ssh URL" )
        {
            REQUIRE( isSshUrl( url.toString() ) );
            REQUIRE_FALSE( isSshUrl( QStringLiteral( "C:/logs/app.log" ) ) );
            REQUIRE_FALSE( isSshUrl( QStringLiteral( "https://example.com/app.log" ) ) );
        }

        THEN( "the default credential target uses user, host and optional port" )
        {
            REQUIRE( defaultSshCredentialTarget( url )
                     == QStringLiteral( "klogg-ssh:alice@example.com" ) );

            auto portUrl = url;
            portUrl.setPort( 2222 );
            REQUIRE( defaultSshCredentialTarget( portUrl )
                     == QStringLiteral( "klogg-ssh:alice@example.com:2222" ) );

            auto noUserUrl = url;
            noUserUrl.setUserName( {} );
            REQUIRE( defaultSshCredentialTarget( noUserUrl )
                     == QStringLiteral( "klogg-ssh:example.com" ) );
        }
    }

    GIVEN( "a non-ssh URL" )
    {
        THEN( "no default credential target is generated" )
        {
            REQUIRE( defaultSshCredentialTarget( QUrl{} ).isEmpty() );
        }
    }
}
