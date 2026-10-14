/*
 * Copyright (C) 2021 CutefishOS Team.
 *
 * Author:     revenmartin <revenmartin@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "processmanager.h"
#include "application.h"
#include "applicationlauncher.h"
#include "applicationruntime.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QSettings>
#include <QDebug>
#include <QProcessEnvironment>
#include <QTimer>
#include <QDir>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>

ProcessManager::ProcessManager(Application *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

ProcessManager::~ProcessManager()
{
    QMapIterator<QString, QProcess *> i(m_coreProcesses);
    while (i.hasNext()) {
        i.next();
        delete i.value();
        m_coreProcesses[i.key()] = nullptr;
    }
}

void ProcessManager::start()
{
    if (m_kwinReady)
        return;

    m_kwinWatcher = new QDBusServiceWatcher(this);
    m_kwinWatcher->setConnection(QDBusConnection::sessionBus());
    m_kwinWatcher->setWatchMode(QDBusServiceWatcher::WatchForRegistration);
    m_kwinWatcher->addWatchedService(QStringLiteral("org.kde.KWinWrapper"));
    m_kwinWatcher->addWatchedService(QStringLiteral("org.kde.KWin"));
    connect(m_kwinWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, [this](const QString &) {
        startAfterKWinReady();
    });

    if (QDBusConnectionInterface *interface = QDBusConnection::sessionBus().interface()) {
        const QDBusReply<bool> wrapperReply = interface->isServiceRegistered(QStringLiteral("org.kde.KWinWrapper"));
        const QDBusReply<bool> kwinReply = interface->isServiceRegistered(QStringLiteral("org.kde.KWin"));
        if ((wrapperReply.isValid() && wrapperReply.value()) ||
            (kwinReply.isValid() && kwinReply.value())) {
            startAfterKWinReady();
        }
    }
}

void ProcessManager::startAfterKWinReady()
{
    if (m_kwinReady)
        return;

    m_kwinReady = true;

    // Explicitly unload KWin zoom and scale effects so desktop is never zoomed in on login
    QDBusInterface kwinEffects(QStringLiteral("org.kde.KWin"),
                               QStringLiteral("/Effects"),
                               QStringLiteral("org.kde.kwin.Effects"),
                               QDBusConnection::sessionBus());
    if (kwinEffects.isValid()) {
        kwinEffects.call(QStringLiteral("unloadEffect"), QStringLiteral("zoom"));
        kwinEffects.call(QStringLiteral("unloadEffect"), QStringLiteral("scale"));
        kwinEffects.call(QStringLiteral("unloadEffect"), QStringLiteral("cutefish_scale"));
    }

    if (m_kwinWatcher) {
        m_kwinWatcher->deleteLater();
        m_kwinWatcher = nullptr;
    }

    // Start the desktop after the services daemon has registered its D-Bus
    // service so its theme and input backends are available to the shell.
    const QString serviceName = QStringLiteral("com.cutefish.Services");
    const auto servicesReady = [this]() {
        if (m_servicesWatcher) {
            m_servicesWatcher->deleteLater();
            m_servicesWatcher = nullptr;
        }
        startDesktopProcess();
    };

    m_servicesWatcher = new QDBusServiceWatcher(serviceName,
                                                QDBusConnection::sessionBus(),
                                                QDBusServiceWatcher::WatchForRegistration,
                                                this);
    connect(m_servicesWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, [servicesReady](const QString &) {
        servicesReady();
    });

    startDaemonProcess();

    if (QDBusConnectionInterface *interface = QDBusConnection::sessionBus().interface()) {
        const QDBusReply<bool> reply = interface->isServiceRegistered(serviceName);
        if (reply.isValid() && reply.value())
            servicesReady();
    }
}

void ProcessManager::logout()
{
    // Applications belong to the runtime, so ask it to quit them, then close
    // what we started ourselves.
    ApplicationRuntime *runtime = ApplicationRuntime::instance();
    if (!runtime->quitAll()) {
        for (const QString &appId : runtime->runningApplications())
            runtime->quitApplication(appId);
    }

    stopProcesses(m_coreProcesses);

    // KWin is started with --exit-with-session, so returning from this
    // process also ends the compositor session and returns to the greeter.

    QCoreApplication::exit(0);
}

void ProcessManager::stopProcesses(QMap<QString, QProcess *> &processes)
{
    QMapIterator<QString, QProcess *> i(processes);
    while (i.hasNext()) {
        i.next();
        QProcess *p = i.value();

        if (!p || p->state() == QProcess::NotRunning)
            continue;

        p->terminate();

        if (!p->waitForFinished(2000))
            p->kill();
    }
}

void ProcessManager::startDesktopProcess()
{
    if (!m_kwinReady) {
        qWarning() << "Ignoring desktop startup before KWin is ready";
        return;
    }

    if (m_desktopStarted)
        return;

    m_desktopStarted = true;

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("wayland"));
    launchManagedProcess(QStringLiteral("cutefish-shell"), QStringList(), environment);

    // For CutefishOS. An ordinary application, so the runtime starts it.
    if (QFile("/usr/bin/cutefish-welcome").exists() &&
            !QFile("/run/live/medium/live/filesystem.squashfs").exists()) {
        QSettings settings("cutefishos", "login");
        QStringList command{QStringLiteral("/usr/bin/cutefish-welcome")};

        if (settings.value("Finished", false).toBool())
            command << QStringLiteral("-d");

        ApplicationLauncher::startDetached(command, QString(),
                                           QStringLiteral("cutefish-welcome"));
    }

    // Auto start
    QTimer::singleShot(100, this, &ProcessManager::loadAutoStartProcess);
}

void ProcessManager::startDaemonProcess()
{
    launchManagedProcess(QStringLiteral("cutefish-appruntime"), QStringList());
    launchManagedProcess(QStringLiteral("cutefish-services"), QStringList());
}

void ProcessManager::launchManagedProcess(const QString &progName, const QStringList &progArgs, const QProcessEnvironment &env)
{
    if (m_coreProcesses.contains(progName) || QCoreApplication::closingDown())
        return;

    QProcess *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    process->setProgram(progName);
    process->setArguments(progArgs);
    if (!env.isEmpty()) {
        process->setProcessEnvironment(env);
    }
    process->start();
    process->waitForStarted();

    if (process->state() != QProcess::NotRunning) {
        m_coreProcesses.insert(progName, process);
        qDebug() << "Started managed process:" << progName << progArgs;

        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, progName, progArgs, env](int exitCode, QProcess::ExitStatus status) {
            qWarning() << "Managed process" << progName << "finished with code" << exitCode << "status" << status;
            m_coreProcesses.remove(progName);
            if (QCoreApplication::closingDown())
                return;
            QTimer::singleShot(1000, this, [this, progName, progArgs, env]() {
                launchManagedProcess(progName, progArgs, env);
            });
        });
    } else {
        qWarning() << "Failed to start managed process:" << progName
                   << process->errorString();
        process->deleteLater();
    }
}

void ProcessManager::loadAutoStartProcess()
{
    QStringList execList;
    const QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericConfigLocation,
                                                       QStringLiteral("autostart"),
                                                       QStandardPaths::LocateDirectory);
    for (const QString &dir : dirs) {
        const QDir d(dir);
        const QStringList fileNames = d.entryList(QStringList() << QStringLiteral("*.desktop"));
        for (const QString &file : fileNames) {
            QSettings desktop(d.absoluteFilePath(file), QSettings::IniFormat);
            desktop.beginGroup("Desktop Entry");

            if (desktop.value("Hidden", false).toBool() || desktop.value("NoDisplay", false).toBool())
                continue;

            const QString onlyShowIn = desktop.value("OnlyShowIn").toString();
            if (!onlyShowIn.isEmpty()) {
                const QStringList onlyList = onlyShowIn.split(';', Qt::SkipEmptyParts);
                bool matches = false;
                for (const QString &item : onlyList) {
                    const QString trimmed = item.trimmed();
                    if (trimmed.compare(QStringLiteral("mimo"), Qt::CaseInsensitive) == 0 ||
                        trimmed.compare(QStringLiteral("Cutefish"), Qt::CaseInsensitive) == 0) {
                        matches = true;
                        break;
                    }
                }
                if (!matches)
                    continue;
            }

            const QString notShowIn = desktop.value("NotShowIn").toString();
            if (!notShowIn.isEmpty()) {
                const QStringList notList = notShowIn.split(';', Qt::SkipEmptyParts);
                bool blocked = false;
                for (const QString &item : notList) {
                    const QString trimmed = item.trimmed();
                    if (trimmed.compare(QStringLiteral("mimo"), Qt::CaseInsensitive) == 0 ||
                        trimmed.compare(QStringLiteral("Cutefish"), Qt::CaseInsensitive) == 0) {
                        blocked = true;
                        break;
                    }
                }
                if (blocked)
                    continue;
            }

            const QString execValue = desktop.value("Exec").toString();

            if (execValue.contains("cutefish-services") ||
                    execValue.contains("cutefish-appruntime"))
                continue;

            if (!execValue.isEmpty()) {
                execList << execValue;
            }
        }
    }

    // Autostart entries are applications: the runtime starts them, and keeps
    // them quittable like any other application.
    for (const QString &exec : execList) {
        QStringList command = QProcess::splitCommand(exec);

        // Autostart entries take no files, so their field codes expand to
        // nothing rather than to a literal "%U" argument.
        command.removeIf([](const QString &argument) {
            return argument.size() == 2 && argument.startsWith(QLatin1Char('%'));
        });

        if (command.isEmpty())
            continue;

        ApplicationLauncher::startDetached(command);
    }
}
