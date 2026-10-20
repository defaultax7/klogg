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

#ifndef KLOGG_WINDOWS_CREDENTIALS_H
#define KLOGG_WINDOWS_CREDENTIALS_H

#include <QString>

// Returns true if a generic credential with this target exists in the Windows
// Credential Manager. Always false on non-Windows platforms.
bool windowsCredentialExists( const QString& target );

// Reads the password stored in a generic credential from the Windows
// Credential Manager. Returns an empty string and fills errorString on failure.
QString readWindowsCredentialPassword( const QString& target,
                                       QString* errorString = nullptr );

#endif
