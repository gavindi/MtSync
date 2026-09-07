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

#include "daemon.hpp"
#include "application.hpp"
#include "i18n.hpp"
#include <glibmm/i18n.h>
#include <iostream>

int main(int argc, char* argv[]) {
    mtsync::init_i18n();
    bool daemon_mode = false;
    bool force_show  = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--daemon" || arg == "-d") {
            daemon_mode = true;
        } else if (arg == "--show") {
            force_show = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << _("Usage: mtsync [OPTIONS]\n")
                      << _("  --daemon, -d    Run as background daemon\n")
                      << _("  --show          Show the window even if 'start minimized' is enabled\n")
                      << _("  --help, -h      Show this help\n");
            return 0;
        }
    }

    if (daemon_mode) {
        mtsync::MtSyncDaemon daemon;
        daemon.run();
        return 0;
    }

    // Run as GUI application; pass only argv[0] so GApplication
    // doesn't reject options it doesn't know about
    auto app = mtsync::MtSyncApplication::create();
    app->set_force_show(force_show);
    return app->run(1, argv);
}
