/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#include <QSettings>

#include <algorithm>

#include "favoritefiles.h"
#include "log.h"

namespace {
constexpr const int FavoriteFilesVersion = 2;
}

void FavoriteFiles::add( const QString& path )
{
    if ( std::any_of( files_.begin(), files_.end(), FullPathComparator( path ) ) ) {
        return;
    }

    files_.emplace_back( path );
    sortFavorites();
}

void FavoriteFiles::remove( const QString& path )
{
    const auto existingFile
        = std::find_if( files_.begin(), files_.end(), FullPathComparator( path ) );
    if ( existingFile != files_.end() ) {
        files_.erase( existingFile );
    }
}

void FavoriteFiles::rename( const QString& path, const QString& displayName )
{
    const auto existingFile
        = std::find_if( files_.begin(), files_.end(), FullPathComparator( path ) );
    if ( existingFile == files_.end() ) {
        return;
    }

    existingFile->setDisplayName( displayName.trimmed() );
    sortFavorites();
}

void FavoriteFiles::sortFavorites()
{
    std::sort( files_.begin(), files_.end(),
               []( const DisplayFilePath& lhs, const DisplayFilePath& rhs ) {
                   if ( lhs.displayName() == rhs.displayName() ) {
                       return lhs.fullPath() < rhs.fullPath();
                   }
                   return lhs.displayName() < rhs.displayName();
               } );
}

std::vector<DisplayFilePath> FavoriteFiles::favorites() const
{
    return files_;
}

void FavoriteFiles::saveToStorage( QSettings& settings ) const
{
    LOG_DEBUG << "FavoriteFiles::saveToStorage";

    settings.beginGroup( "FavoriteFiles" );
    settings.setValue( "version", FavoriteFilesVersion );
    settings.remove( "favorites" );
    settings.beginWriteArray( "favorites" );
    for ( auto i = 0u; i < files_.size(); ++i ) {
        settings.setArrayIndex( static_cast<int>( i ) );
        settings.setValue( "name", files_.at( i ).fullPath() );
        settings.setValue( "displayName", files_.at( i ).customDisplayName() );
    }
    settings.endArray();
    settings.endGroup();
}

void FavoriteFiles::retrieveFromStorage( QSettings& settings )
{
    LOG_DEBUG << "FavoriteFiles::retrieveFromStorage";

    files_.clear();

    if ( settings.contains( "FavoriteFiles/version" ) ) {
        settings.beginGroup( "FavoriteFiles" );
        const auto version = settings.value( "version" ).toInt();
        if ( version <= FavoriteFilesVersion ) {
            int size = settings.beginReadArray( "favorites" );
            for ( int i = 0; i < size; ++i ) {
                settings.setArrayIndex( static_cast<int>( i ) );
                const auto path = settings.value( "name" ).toString();
                const auto displayName = settings.value( "displayName" ).toString();
                if ( !path.isEmpty() ) {
                    files_.emplace_back( path, displayName );
                }
            }
            settings.endArray();
            sortFavorites();
        }
        else {
            LOG_ERROR << "Unknown version of favorite files, ignoring it...";
        }
        settings.endGroup();
    }
}
