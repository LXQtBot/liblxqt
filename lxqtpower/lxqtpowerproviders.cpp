/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * LXQt - a lightweight, Qt based, desktop toolset
 * https://lxqt.org
 *
 * Copyright: 2010-2011 Razor team
 * Authors:
 *   Alexander Sokoloff <sokoloff.a@gmail.com>
 *   Petr Vanek <petr@scribus.info>
 *
 * This program or library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * END_COMMON_COPYRIGHT_HEADER */


#include "lxqtpowerproviders.h"
#include <QDBusInterface>
#include <QProcess>
#include <QProcessEnvironment>
#include <QGuiApplication>
#include <QDebug>
#include "lxqtnotification.h"
#include <csignal> // for kill()


using namespace Qt::Literals::StringLiterals;
#define UPOWER_SERVICE          "org.freedesktop.UPower"
#define UPOWER_PATH             "/org/freedesktop/UPower"
#define UPOWER_INTERFACE        UPOWER_SERVICE

#define CONSOLEKIT_SERVICE      "org.freedesktop.ConsoleKit"
#define CONSOLEKIT_PATH         "/org/freedesktop/ConsoleKit/Manager"
#define CONSOLEKIT_INTERFACE    "org.freedesktop.ConsoleKit.Manager"

#define SYSTEMD_SERVICE         "org.freedesktop.login1"
#define SYSTEMD_PATH            "/org/freedesktop/login1"
#define SYSTEMD_INTERFACE       "org.freedesktop.login1.Manager"

#define LXQT_SERVICE      "org.lxqt.session"
#define LXQT_PATH         "/LXQtSession"
#define LXQT_INTERFACE    "org.lxqt.session"

#define PROPERTIES_INTERFACE    "org.freedesktop.DBus.Properties"

using namespace LXQt;

/************************************************
 Helper func
 ************************************************/
void printDBusMsg(const QDBusMessage &msg)
{
    qWarning() << "** Dbus error **************************";
    qWarning() << "Error name " << msg.errorName();
    qWarning() << "Error msg  " << msg.errorMessage();
    qWarning() << "****************************************";
}


/************************************************
 Helper func
 ************************************************/
static bool dbusCall(const QString &service,
              const QString &path,
              const QString &interface,
              const QDBusConnection &connection,
              const QString & method,
              PowerProvider::DbusErrorCheck errorCheck = PowerProvider::CheckDBUS,
              const QVariantList &args = QVariantList()
              )
{
    QDBusInterface dbus(service, path, interface, connection);
    if (!dbus.isValid())
    {
        qWarning() << "dbusCall: QDBusInterface is invalid" << service << path << interface << method;
        if (errorCheck == PowerProvider::CheckDBUS)
        {
            Notification::notify(
                                    QObject::tr("Power Manager Error"),
                                    QObject::tr("QDBusInterface is invalid") + "\n\n"_L1 + service + u' ' + path + u' ' + interface + u' ' + method,
                                    "lxqt-logo.png"_L1);
        }
        return false;
    }

    QDBusMessage msg = !args.isEmpty() ? dbus.callWithArgumentList(QDBus::Block, method, args) : dbus.call(method);

    if (!msg.errorName().isEmpty())
    {
        printDBusMsg(msg);
        if (errorCheck == PowerProvider::CheckDBUS)
        {
            Notification::notify(
                                    QObject::tr("Power Manager Error (D-BUS call)"),
                                    msg.errorName() + "\n\n"_L1 + msg.errorMessage(),
                                    "lxqt-logo.png"_L1);
        }
    }

    // If the method no returns value, we believe that it was successful.
    return msg.arguments().isEmpty() ||
           msg.arguments().constFirst().isNull() ||
           msg.arguments().constFirst().toBool();
}

/************************************************
 Helper func

 Just like dbusCall(), except that systemd
 returns a string instead of a bool, and it takes
 an "interactivity boolean" as an argument.
 ************************************************/
static bool dbusCallSystemd(const QString &service,
                     const QString &path,
                     const QString &interface,
                     const QDBusConnection &connection,
                     const QString &method,
                     bool needBoolArg,
                     PowerProvider::DbusErrorCheck errorCheck = PowerProvider::CheckDBUS
                     )
{
    QDBusInterface dbus(service, path, interface, connection);
    if (!dbus.isValid())
    {
        qWarning() << "dbusCall: QDBusInterface is invalid" << service << path << interface << method;
        if (errorCheck == PowerProvider::CheckDBUS)
        {
            Notification::notify(
                                    QObject::tr("Power Manager Error"),
                                    QObject::tr("QDBusInterface is invalid") + "\n\n"_L1 + service + u' ' + path + u' '+ interface + u' ' + method,
                                    "lxqt-logo.png"_L1);
        }
        return false;
    }

    QDBusMessage msg = needBoolArg ? dbus.call(method, QVariant(true)) : dbus.call(method);

    if (!msg.errorName().isEmpty())
    {
        printDBusMsg(msg);
        if (errorCheck == PowerProvider::CheckDBUS)
        {
            Notification::notify(
                                    QObject::tr("Power Manager Error (D-BUS call)"),
                                    msg.errorName() + "\n\n"_L1 + msg.errorMessage(),
                                    "lxqt-logo.png"_L1);
        }
    }

    // If the method no returns value, we believe that it was successful.
    if (msg.arguments().isEmpty() || msg.arguments().constFirst().isNull())
        return true;

    QString response = msg.arguments().constFirst().toString();
    qDebug() << "systemd:" << method << "=" << response;
    return response == "yes"_L1 || response == "challenge"_L1;
}


/************************************************
 Helper func
 ************************************************/
bool dbusGetProperty(const QString &service,
                     const QString &path,
                     const QString &interface,
                     const QDBusConnection &connection,
                     const QString & property
                    )
{
    QDBusInterface dbus(service, path, interface, connection);
    if (!dbus.isValid())
    {
        qWarning() << "dbusGetProperty: QDBusInterface is invalid" << service << path << interface << property;
//        Notification::notify(QObject::tr("LXQt Power Manager"),
//                                  "lxqt-logo.png",
//                                  QObject::tr("Power Manager Error"),
//                                  QObject::tr("QDBusInterface is invalid")+ "\n\n" + service +" " + path +" " + interface +" " + property);

        return false;
    }

    QDBusMessage msg = dbus.call("Get"_L1, dbus.interface(), property);

    if (!msg.errorName().isEmpty())
    {
        printDBusMsg(msg);
//        Notification::notify(QObject::tr("LXQt Power Manager"),
//                                  "lxqt-logo.png",
//                                  QObject::tr("Power Manager Error (Get Property)"),
//                                  msg.errorName() + "\n\n" + msg.errorMessage());
    }

    return !msg.arguments().isEmpty() &&
            msg.arguments().constFirst().value<QDBusVariant>().variant().toBool();
}




/************************************************
 PowerProvider
 ************************************************/
PowerProvider::PowerProvider(QObject *parent):
    QObject(parent)
{
}


PowerProvider::~PowerProvider() = default;



/************************************************
 UPowerProvider
 ************************************************/
UPowerProvider::UPowerProvider(QObject *parent):
    PowerProvider(parent)
{
}


UPowerProvider::~UPowerProvider() = default;


bool UPowerProvider::canAction(Power::Action action) const
{
    QString command;
    QString property;
    switch (action)
    {
    case Power::PowerHibernate:
        property = "CanHibernate"_L1;
        command  = "HibernateAllowed"_L1;
        break;

    case Power::PowerSuspend:
        property = "CanSuspend"_L1;
        command  = "SuspendAllowed"_L1;
        break;

    default:
        return false;
    }

    return  dbusGetProperty(  // Whether the system is able to hibernate.
                QL1SV(UPOWER_SERVICE),
                QL1SV(UPOWER_PATH),
                QL1SV(PROPERTIES_INTERFACE),
                QDBusConnection::systemBus(),
                property
            )
            &&
            dbusCall( // Check if the caller has (or can get) the PolicyKit privilege to call command.
                QL1SV(UPOWER_SERVICE),
                QL1SV(UPOWER_PATH),
                QL1SV(UPOWER_INTERFACE),
                QDBusConnection::systemBus(),
                command,
                // canAction should be always silent because it can freeze
                // g_main_context_iteration Qt event loop in QMessageBox
                // on panel startup if there is no DBUS running.
                PowerProvider::DontCheckDBUS
            );
}


bool UPowerProvider::doAction(Power::Action action)
{
    QString command;

    switch (action)
    {
    case Power::PowerHibernate:
        command = "Hibernate"_L1;
        break;

    case Power::PowerSuspend:
        command = "Suspend"_L1;
        break;

    default:
        return false;
    }


    return dbusCall(QL1SV(UPOWER_SERVICE),
             QL1SV(UPOWER_PATH),
             QL1SV(UPOWER_INTERFACE),
             QDBusConnection::systemBus(),
             command );
}



/************************************************
 ConsoleKitProvider
 ************************************************/
ConsoleKitProvider::ConsoleKitProvider(QObject *parent):
    PowerProvider(parent)
{
}


ConsoleKitProvider::~ConsoleKitProvider() = default;


bool ConsoleKitProvider::canAction(Power::Action action) const
{
    QString command;
    switch (action)
    {
    case Power::PowerReboot:
        command = "CanReboot"_L1;
        break;

    case Power::PowerShutdown:
        command = "CanPowerOff"_L1;
        break;

    case Power::PowerHibernate:
        command  = "CanHibernate"_L1;
        break;

    case Power::PowerSuspend:
        command  = "CanSuspend"_L1;
        break;

    default:
        return false;
    }

    return dbusCallSystemd(QL1SV(CONSOLEKIT_SERVICE),
                    QL1SV(CONSOLEKIT_PATH),
                    QL1SV(CONSOLEKIT_INTERFACE),
                    QDBusConnection::systemBus(),
                    command,
                    false,
                    // canAction should be always silent because it can freeze
                    // g_main_context_iteration Qt event loop in QMessageBox
                    // on panel startup if there is no DBUS running.
                    PowerProvider::DontCheckDBUS
                   );
}


bool ConsoleKitProvider::doAction(Power::Action action)
{
    QString command;
    switch (action)
    {
    case Power::PowerReboot:
        command = "Reboot"_L1;
        break;

    case Power::PowerShutdown:
        command = "PowerOff"_L1;
        break;

    case Power::PowerHibernate:
        command = "Hibernate"_L1;
        break;

    case Power::PowerSuspend:
        command = "Suspend"_L1;
        break;

    default:
        return false;
    }

    return dbusCallSystemd(QL1SV(CONSOLEKIT_SERVICE),
                QL1SV(CONSOLEKIT_PATH),
                QL1SV(CONSOLEKIT_INTERFACE),
                QDBusConnection::systemBus(),
                command,
                true
               );
}

/************************************************
  SystemdProvider

  http://www.freedesktop.org/wiki/Software/systemd/logind
 ************************************************/

SystemdProvider::SystemdProvider(QObject *parent):
    PowerProvider(parent)
{
}


SystemdProvider::~SystemdProvider() = default;


bool SystemdProvider::canAction(Power::Action action) const
{
    QString command;

    switch (action)
    {
    case Power::PowerLogout:
    {
        const QByteArray sessionId = qgetenv("XDG_SESSION_ID");
        if (sessionId.isEmpty())
            return false;

        QDBusInterface dbus(QL1SV(SYSTEMD_SERVICE),
                            QL1SV(SYSTEMD_PATH),
                            QL1SV(SYSTEMD_INTERFACE),
                            QDBusConnection::systemBus());
        return dbus.isValid();
    }

    case Power::PowerReboot:
        command = "CanReboot"_L1;
        break;

    case Power::PowerShutdown:
        command = "CanPowerOff"_L1;
        break;

    case Power::PowerSuspend:
        command = "CanSuspend"_L1;
        break;

    case Power::PowerHibernate:
        command = "CanHibernate"_L1;
        break;

    default:
        return false;
    }

    return dbusCallSystemd(QL1SV(SYSTEMD_SERVICE),
                    QL1SV(SYSTEMD_PATH),
                    QL1SV(SYSTEMD_INTERFACE),
                    QDBusConnection::systemBus(),
                    command,
                    false,
                    // canAction should be always silent because it can freeze
                    // g_main_context_iteration Qt event loop in QMessageBox
                    // on panel startup if there is no DBUS running.
                    PowerProvider::DontCheckDBUS
                   );
}


bool SystemdProvider::doAction(Power::Action action)
{
    QString command;

    switch (action)
    {
    case Power::PowerLogout:
    {
        const QByteArray sessionId = qgetenv("XDG_SESSION_ID");
        if (sessionId.isEmpty())
            return false;

        return dbusCall(QL1SV(SYSTEMD_SERVICE),
                        QL1SV(SYSTEMD_PATH),
                        QL1SV(SYSTEMD_INTERFACE),
                        QDBusConnection::systemBus(),
                        "TerminateSession"_L1,
                        PowerProvider::CheckDBUS,
                        QVariantList() << QString::fromLocal8Bit(sessionId));
    }

    case Power::PowerReboot:
        command = "Reboot"_L1;
        break;

    case Power::PowerShutdown:
        command = "PowerOff"_L1;
        break;

    case Power::PowerSuspend:
        command = "Suspend"_L1;
        break;

    case Power::PowerHibernate:
        command = "Hibernate"_L1;
        break;

    default:
        return false;
    }

    return dbusCallSystemd(QL1SV(SYSTEMD_SERVICE),
             QL1SV(SYSTEMD_PATH),
             QL1SV(SYSTEMD_INTERFACE),
             QDBusConnection::systemBus(),
             command,
             true
            );
}


/************************************************
  LXQtProvider
 ************************************************/
LXQtProvider::LXQtProvider(QObject *parent):
    PowerProvider(parent)
{
}


LXQtProvider::~LXQtProvider() = default;


bool LXQtProvider::canAction(Power::Action action) const
{
    QString command;
    switch (action)
    {
        case Power::PowerLogout:
            command = "canLogout"_L1;
            break;
        case Power::PowerReboot:
            command = "canReboot"_L1;
            break;
        case Power::PowerShutdown:
            command = "canPowerOff"_L1;
            break;
        default:
            return false;
    }

    // there can be case when lxqtsession-session does not run
    return dbusCall(QL1SV(LXQT_SERVICE), QL1SV(LXQT_PATH), QL1SV(LXQT_INTERFACE),
            QDBusConnection::sessionBus(), command,
            PowerProvider::DontCheckDBUS);
}


bool LXQtProvider::doAction(Power::Action action)
{
    QString command;
    switch (action)
    {
        case Power::PowerLogout:
            command = "logout"_L1;
            break;
        case Power::PowerReboot:
            command = "reboot"_L1;
            break;
        case Power::PowerShutdown:
            command = "powerOff"_L1;
            break;
        default:
            return false;
    }

    return dbusCall(QL1SV(LXQT_SERVICE),
             QL1SV(LXQT_PATH),
             QL1SV(LXQT_INTERFACE),
             QDBusConnection::sessionBus(),
             command
            );
}

/************************************************
  LxSessionProvider
 ************************************************/
LxSessionProvider::LxSessionProvider(QObject *parent):
    PowerProvider(parent)
{
    pid = (qint64)qgetenv("_LXSESSION_PID").toLong();
}


LxSessionProvider::~LxSessionProvider() = default;


bool LxSessionProvider::canAction(Power::Action action) const
{
    switch (action)
    {
        case Power::PowerLogout:
            return pid != 0;
        default:
            return false;
    }
}


bool LxSessionProvider::doAction(Power::Action action)
{
    switch (action)
    {
    case Power::PowerLogout:
        if(pid)
            ::kill(pid, SIGTERM);
        break;
    default:
        return false;
    }

    return true;
}


/************************************************
  HalProvider
 ************************************************/
HalProvider::HalProvider(QObject *parent):
    PowerProvider(parent)
{
}


HalProvider::~HalProvider() = default;


bool HalProvider::canAction(Power::Action action) const
{
    Q_UNUSED(action)
    return false;
}


bool HalProvider::doAction(Power::Action action)
{
    Q_UNUSED(action)
    return false;
}


/************************************************
  CustomProvider
 ************************************************/
CustomProvider::CustomProvider(QObject *parent):
    PowerProvider(parent),
    mSettings("power"_L1)
{
}

CustomProvider::~CustomProvider() = default;

bool CustomProvider::canAction(Power::Action action) const
{
    switch (action)
    {
    case Power::PowerShutdown:
        return mSettings.contains("shutdownCommand"_L1);

    case Power::PowerReboot:
        return mSettings.contains("rebootCommand"_L1);

    case Power::PowerHibernate:
        return mSettings.contains("hibernateCommand"_L1);

    case Power::PowerSuspend:
        return mSettings.contains("suspendCommand"_L1);

    case Power::PowerLogout:
        return mSettings.contains("logoutCommand"_L1);

    case Power::PowerMonitorOff:
        if (QGuiApplication::platformName() == QStringLiteral("xcb"))
            return mSettings.contains("monitorOffCommand"_L1);
        else if (QGuiApplication::platformName() == QStringLiteral("wayland"))
            return mSettings.contains("monitorOffCommand_wayland"_L1);
        else
            return false;

    case Power::PowerShowLeaveDialog:
        return mSettings.contains("showLeaveDialogCommand"_L1);

    default:
        return false;
    }
}

bool CustomProvider::doAction(Power::Action action)
{
    QString command;

    switch(action)
    {
    case Power::PowerShutdown:
        command = mSettings.value("shutdownCommand"_L1).toString();
        break;

    case Power::PowerReboot:
        command = mSettings.value("rebootCommand"_L1).toString();
        break;

    case Power::PowerHibernate:
        command = mSettings.value("hibernateCommand"_L1).toString();
        break;

    case Power::PowerSuspend:
        command = mSettings.value("suspendCommand"_L1).toString();
        break;

    case Power::PowerLogout:
        command = mSettings.value("logoutCommand"_L1).toString();
        break;

    case Power::PowerMonitorOff:
        if (QGuiApplication::platformName() == QStringLiteral("xcb"))
            command = mSettings.value("monitorOffCommand"_L1).toString();
        else if (QGuiApplication::platformName() == QStringLiteral("wayland"))
            command = mSettings.value("monitorOffCommand_wayland"_L1).toString();
        break;

    case Power::PowerShowLeaveDialog:
        command = mSettings.value("showLeaveDialogCommand"_L1).toString();
        break;

    default:
        return false;
    }

    QStringList args = QProcess::splitCommand(command);
    if (args.isEmpty())
        return false;

    QProcess process;
    process.setProgram(args.takeFirst());
    process.setArguments(args);
    return process.startDetached();
}
