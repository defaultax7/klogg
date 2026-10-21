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

#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>

#include "favoritefiles.h"

SCENARIO( "favorite files custom names", "[ui]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );
    const auto settingsFile = tempDir.filePath( QStringLiteral( "favorites.ini" ) );

    GIVEN( "favorites with custom display names" )
    {
        FavoriteFiles favorites;
        favorites.add( QStringLiteral( "/logs/z-first.log" ) );
        favorites.add( QStringLiteral( "/logs/a-second.log" ) );
        favorites.rename( QStringLiteral( "/logs/z-first.log" ),
                          QStringLiteral( "Production log" ) );
        favorites.rename( QStringLiteral( "/logs/a-second.log" ),
                          QStringLiteral( "Development log" ) );

        THEN( "names are used for display and entries are re-sorted" )
        {
            const auto list = favorites.favorites();
            REQUIRE( list.size() == 2 );
            REQUIRE( list[ 0 ].displayName() == QStringLiteral( "Development log" ) );
            REQUIRE( list[ 0 ].customDisplayName() == QStringLiteral( "Development log" ) );
            REQUIRE( list[ 1 ].displayName() == QStringLiteral( "Production log" ) );
        }

        THEN( "names are persisted and restored" )
        {
            {
                QSettings settings{ settingsFile, QSettings::IniFormat };
                favorites.saveToStorage( settings );
                settings.sync();
            }

            QSettings settings{ settingsFile, QSettings::IniFormat };
            FavoriteFiles loaded;
            loaded.retrieveFromStorage( settings );

            const auto list = loaded.favorites();
            REQUIRE( list.size() == 2 );
            REQUIRE( list[ 0 ].customDisplayName() == QStringLiteral( "Development log" ) );
            REQUIRE( list[ 1 ].customDisplayName() == QStringLiteral( "Production log" ) );
        }

        THEN( "an empty name restores the default display name" )
        {
            favorites.rename( QStringLiteral( "/logs/z-first.log" ), QStringLiteral( "   " ) );

            const auto list = favorites.favorites();
            const auto entry = std::find_if(
                list.begin(), list.end(),
                []( const DisplayFilePath& file ) {
                    return file.fullPath() == QStringLiteral( "/logs/z-first.log" );
                } );
            REQUIRE( entry != list.end() );
            REQUIRE( entry->customDisplayName().isEmpty() );
            REQUIRE( entry->displayName() == DisplayFilePath( entry->fullPath() ).displayName() );
        }
    }

    GIVEN( "a version 1 favorites file" )
    {
        {
            QSettings settings{ settingsFile, QSettings::IniFormat };
            settings.beginGroup( QStringLiteral( "FavoriteFiles" ) );
            settings.setValue( QStringLiteral( "version" ), 1 );
            settings.beginWriteArray( QStringLiteral( "favorites" ) );
            settings.setArrayIndex( 0 );
            settings.setValue( QStringLiteral( "name" ), QStringLiteral( "/logs/old.log" ) );
            settings.endArray();
            settings.endGroup();
            settings.sync();
        }

        QSettings settings{ settingsFile, QSettings::IniFormat };
        FavoriteFiles loaded;
        loaded.retrieveFromStorage( settings );

        THEN( "the old path-only format is still loaded" )
        {
            const auto list = loaded.favorites();
            REQUIRE( list.size() == 1 );
            REQUIRE( list[ 0 ].fullPath() == QStringLiteral( "/logs/old.log" ) );
            REQUIRE( list[ 0 ].customDisplayName().isEmpty() );
        }
    }
}
