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

#ifndef KLOGG_SSH_ASKPASS_H
#define KLOGG_SSH_ASKPASS_H

// When KLOGG_SSH_ASKPASS is set, this process was started by ssh as an
// SSH_ASKPASS helper. Print the password stored in Windows Credential Manager
// and return an exit code. Returns -1 if askpass mode was not requested.
int runSshAskPassIfRequested();

#endif
