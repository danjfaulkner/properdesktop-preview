// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "application.h"
#include "icons.h"
#include "previewsettings.h"

#include "base/dbus.h"
#include "base/event_loop.h"
#include "base/i18n.h"
#include "base/url.h"
#include "ui/application.h"
#include "ui/icon.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <csignal>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>

using namespace properdesktop;
using namespace std::chrono_literals;

// The files to open as absolute paths; false when the program is done.
static bool parseArguments(int argc, char** argv, std::vector<std::string>* paths, int* status)
{
    *status = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-h" || argument == "--help" || argument == "-?") {
            std::printf("Usage: %s [options] [files...]\nProperDesktop Preview\n\nOptions:\n"
                        "  -h, --help     Displays help on commandline options.\n"
                        "  -v, --version  Displays version information.\n\n"
                        "Arguments:\n"
                        "  files          Images to open.\n",
                        argv[0]);
            return false;
        }
        if (argument == "-v" || argument == "--version") {
            std::printf("properdesktop-preview %s\n", PREVIEW_VERSION);
            return false;
        }
        if (argument.size() > 1 && argument[0] == '-') {
            std::fprintf(stderr, "Unknown option '%s'.\n", argument.substr(argument.find_first_not_of('-')).c_str());
            *status = 1;
            return false;
        }
        std::error_code error;
        const std::string path = base::url::toLocalFile(argument).value_or(argument);
        paths->push_back(std::filesystem::absolute(path, error).lexically_normal().string());
    }
    return true;
}

int main(int argc, char** argv)
{
    std::vector<std::string> paths;
    int status = 0;
    if (!parseArguments(argc, argv, &paths, &status))
        return status;

    // Logging out ends the process with SIGTERM, which waits for the turns
    // still to be written. Blocked before any thread starts, so the signals
    // reach the fd.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGHUP);
    sigprocmask(SIG_BLOCK, &signals, nullptr);

    ui::Application app(argc, argv);
    app.setApplicationId("properdesktop-preview");
    i18n::setDomain("properdesktop-preview");
    for (const auto& [name, svg] : embeddedIcons())
        ui::Icon::registerGlyph(std::string(name), std::string(svg));

    PreviewSettings settings(PreviewSettings::defaultPath());
    Application application(settings);
    if (base::dbus::Connection* bus = app.sessionBus()) {
        if (!application.registerService(bus)) {
            base::dbus::Proxy running(bus, Application::kService, Application::kPath, Application::kService);
            running.setTimeout(3s);
            // A Preview that does not answer leaves the files to this one.
            if (!running.call("Open", paths, app.takeActivationToken()).isError())
                return 0;
        }
    }
    const int signalFd = signalfd(-1, &signals, SFD_NONBLOCK | SFD_CLOEXEC);
    app.loop().watch(signalFd, EPOLLIN, [&application, signalFd](uint32_t) {
        signalfd_siginfo info;
        while (::read(signalFd, &info, sizeof(info)) == ssize_t(sizeof(info))) {
        }
        application.quit();
    });
    application.open(paths, app.takeActivationToken());
    const int result = app.run();
    ::close(signalFd);
    return result;
}
