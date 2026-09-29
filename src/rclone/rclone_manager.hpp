/*
 * Mt. Sync — GTK4 frontend to rclone
 * Copyright (C) 2026  Mt. Sync contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "rclone_cli.hpp"
#include "rclone_rc.hpp"
#include "sandbox.hpp"

namespace mtsync::rclone {

class RcloneManager {
public:
    RcloneManager() = default;

    RcloneCli& cli() { return m_cli; }
    RcloneRc& rc() { return m_rc; }

private:
    RcloneCli m_cli{sandbox::rclone_program()};
    RcloneRc m_rc;
};

} // namespace mtsync::rclone
