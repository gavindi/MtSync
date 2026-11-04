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

#include "error_hints.hpp"
#include <glibmm/i18n.h>

namespace mtsync::rclone {

namespace {

// Google Cloud project number of rclone's built-in Google Drive OAuth client
// (202264815644.apps.googleusercontent.com). Its per-minute quota is shared by
// every rclone user who hasn't configured their own client_id.
constexpr const char* kRcloneDriveProject = "202264815644";

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

} // namespace

std::string error_hint(const std::string& error) {
    bool rate_limited = contains(error, "rateLimitExceeded") ||
                        contains(error, "RATE_LIMIT_EXCEEDED");
    if (rate_limited && contains(error, kRcloneDriveProject)) {
        return _("Google Drive is limiting requests from rclone's built-in client ID, "
                 "which is shared by all rclone users. Try again later, or use your own "
                 "client ID: create one as described at "
                 "https://rclone.org/drive/#making-your-own-client-id, then open this "
                 "remote under Remotes, enter the client_id and client_secret, click "
                 "Authorize, and click Update.");
    }
    return {};
}

} // namespace mtsync::rclone
