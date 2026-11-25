// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "application.h"
#include "imagesequence.h"
#include "previewsettings.h"
#include "turnsaver.h"
#include "views/previewwindow.h"

#include "base/dbus.h"
#include "base/event_loop.h"
#include "base/mime_apps.h"
#include "base/url.h"
#include "ui/application.h"
#include "ui/window.h"

#include <algorithm>

using namespace properdesktop;

Application::Application(PreviewSettings& settings)
    : m_settings(settings)
    , m_turnSaver(std::make_unique<TurnSaver>())
{
}

Application::~Application()
{
    m_service.reset();
    m_windows.clear();
}

void Application::open(const std::vector<std::string>& paths, const std::string& activationToken)
{
    std::vector<std::string> images;
    std::vector<std::vector<std::string>> requests;
    for (const std::string& path : paths) {
        if (ImageSequence::isImage(path))
            images.push_back(path);
        else
            requests.push_back({path});
    }
    if (!images.empty())
        requests.insert(requests.begin(), images);
    if (requests.empty())
        requests.push_back({});

    for (size_t i = 0; i < requests.size(); ++i) {
        const std::vector<std::string>& request = requests[i];
        PreviewWindow* window = request.empty() ? nullptr : windowShowing(request.front());
        if (!window) {
            window = freeWindow();
            if (!request.empty())
                window->show(request);
        }
        window->raise(i + 1 == requests.size() ? activationToken : std::string());
    }
}

PreviewWindow* Application::windowShowing(const std::string& path) const
{
    for (const auto& window : m_windows) {
        if (window->currentPath() == path)
            return window.get();
    }
    return nullptr;
}

PreviewWindow* Application::freeWindow()
{
    for (const auto& window : m_windows) {
        if (window->isWelcome())
            return window.get();
    }
    m_windows.push_back(std::make_unique<PreviewWindow>(*this));
    return m_windows.back().get();
}

void Application::releaseWindow(PreviewWindow* window)
{
    const auto it = std::find_if(m_windows.begin(), m_windows.end(),
                                 [window](const std::unique_ptr<PreviewWindow>& w) { return w.get() == window; });
    if (it == m_windows.end())
        return;
    // Destroyed after the event it closed in, which may be one of its own.
    std::shared_ptr<PreviewWindow> doomed(std::move(*it));
    m_windows.erase(it);
    base::EventLoop::current()->post([doomed] {});
    if (m_windows.empty())
        quit();
}

void Application::quit()
{
    m_turnSaver->finish();
    // Later: this is called from handlers of the windows quitting closes.
    base::EventLoop::current()->post([] { ui::Application::instance()->quit(); });
}

void Application::showInFiles(const std::string& path) const
{
    base::dbus::Connection* bus = ui::Application::instance()->sessionBus();
    if (!bus)
        return;
    base::dbus::Proxy proxy(bus, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                            "org.freedesktop.FileManager1");
    proxy.send("ShowItems", std::vector<std::string> {base::url::fromLocalFile(path)}, std::string());
}

void Application::openUri(const std::string& uri, ui::Window* window) const
{
    if (uriOpener) {
        uriOpener(uri);
        return;
    }
    window->requestActivationToken([uri](const std::string& token) { base::mimeapps::openUri(uri, token); });
}

bool Application::registerService(base::dbus::Connection* bus)
{
    // The object goes up before the name, so a second launch that finds the
    // name always finds someone to hand its files to.
    m_service = std::make_unique<base::dbus::Object>(bus, kPath, kService);
    m_service->addMethod(
            "Open",
            [this](const std::vector<std::string>& paths, const std::string& activationToken) {
                open(paths, activationToken);
            },
            base::dbus::ArgumentNames {{"paths", "activationToken"}, {}});
    m_service->publish();
    return bus->requestName(kService);
}
