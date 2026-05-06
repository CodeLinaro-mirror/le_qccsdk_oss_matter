#include "WiFiManager.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(wifi_manager, LOG_LEVEL_INF);

WiFiManager & WiFiManager::Instance()
{
    static WiFiManager sInstance;
    return sInstance;
}

void WiFiManager::Init()
{
    LOG_INF("WiFi Manager initialized (stub)");
}

int WiFiManager::Connect(const char * ssid, const char * password)
{
    LOG_INF("WiFi Connect called (stub)");
    return 0;
}

int WiFiManager::Disconnect()
{
    return 0;
}

bool WiFiManager::IsConnected()
{
    return false;
}

int WiFiManager::GetSSID(char * ssid, size_t ssid_len)
{
    return -1;
}

void WiFiManager::WiFiEventHandler(struct net_mgmt_event_callback * cb, uint32_t mgmt_event, struct net_if * iface)
{
}
