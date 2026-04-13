#include "control_service.h"

#include "constants.h"

#include <QDBusError>
#include <stdexcept>

ControlService::ControlService(QObject *parent)
    : QObject(parent)
    , m_connection(QDBusConnection::sessionBus())
{
    if (!m_connection.registerService(kAppId)) {
        throw std::runtime_error(QStringLiteral("Failed to register control service: %1").arg(m_connection.lastError().message()).toStdString());
    }
    if (!m_connection.registerObject(kControlObjectPath, this, QDBusConnection::ExportAllSlots)) {
        throw std::runtime_error(QStringLiteral("Failed to register control object: %1").arg(m_connection.lastError().message()).toStdString());
    }
}

ControlService::~ControlService()
{
    m_connection.unregisterObject(kControlObjectPath);
    m_connection.unregisterService(kAppId);
}

bool ControlService::TogglePanel()
{
    emit commandReceived(QStringLiteral("toggle"));
    return true;
}

bool ControlService::ShowPanel()
{
    emit commandReceived(QStringLiteral("show"));
    return true;
}

bool ControlService::HidePanel()
{
    emit commandReceived(QStringLiteral("hide"));
    return true;
}

bool ControlService::Quit()
{
    emit commandReceived(QStringLiteral("quit"));
    return true;
}
