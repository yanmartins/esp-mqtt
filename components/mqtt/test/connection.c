/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "unity.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_log.h"

#if SOC_EMAC_SUPPORTED
#define ETH_START_BIT BIT(0)
#define ETH_STOP_BIT BIT(1)
#define ETH_CONNECT_BIT BIT(2)
#define ETH_GOT_IP_BIT BIT(3)
#define ETH_STOP_TIMEOUT_MS (10000)
#define ETH_GET_IP_TIMEOUT_MS (60000)

static const char *TAG = "esp32_eth_test_fixture";

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    EventGroupHandle_t eth_event_group = (EventGroupHandle_t)arg;
    switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            xEventGroupSetBits(eth_event_group, ETH_CONNECT_BIT);
            ESP_LOGI(TAG, "Ethernet Link Up");
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Ethernet Link Down");
            break;
        case ETHERNET_EVENT_START:
            xEventGroupSetBits(eth_event_group, ETH_START_BIT);
            ESP_LOGI(TAG, "Ethernet Started");
            break;
        case ETHERNET_EVENT_STOP:
            xEventGroupSetBits(eth_event_group, ETH_STOP_BIT);
            ESP_LOGI(TAG, "Ethernet Stopped");
            break;
        default:
            break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    EventGroupHandle_t eth_event_group = (EventGroupHandle_t)arg;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;
    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    xEventGroupSetBits(eth_event_group, ETH_GOT_IP_BIT);
}

static esp_err_t test_uninstall_driver(esp_eth_handle_t eth_hdl, uint32_t ms_to_wait)
{
    int i = 0;
    ms_to_wait += 100;
    for (i = 0; i < ms_to_wait / 100; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (esp_eth_driver_uninstall(eth_hdl) == ESP_OK) {
            break;
        }
    }
    if (i < ms_to_wait / 10) {
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}
static EventGroupHandle_t eth_event_group;
static esp_netif_t *eth_netif;
static esp_eth_mac_t *mac;
static esp_eth_phy_t *phy;
static esp_eth_handle_t eth_handle = NULL;
static esp_eth_netif_glue_handle_t glue;

void eth_test_fixture_connect(void)
{
    EventBits_t bits;
    eth_event_group = xEventGroupCreate();
    TEST_ASSERT(eth_event_group != NULL);
    TEST_ESP_OK(esp_event_loop_create_default());
    // create TCP/IP netif
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&netif_cfg);

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac = esp_eth_mac_new_esp32(&mac_config);
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy = esp_eth_phy_new_ip101(&phy_config);
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);

    // install Ethernet driver
    TEST_ESP_OK(esp_eth_driver_install(&eth_config, &eth_handle));
    // combine driver with netif
    glue = esp_eth_new_netif_glue(eth_handle);
    TEST_ESP_OK(esp_netif_attach(eth_netif, glue));
    // register user defined event handers
    TEST_ESP_OK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, eth_event_group));
    TEST_ESP_OK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, eth_event_group));
    // start Ethernet driver
    TEST_ESP_OK(esp_eth_start(eth_handle));
    /* wait for IP lease */
    bits = xEventGroupWaitBits(eth_event_group, ETH_GOT_IP_BIT, true, true, pdMS_TO_TICKS(ETH_GET_IP_TIMEOUT_MS));
    TEST_ASSERT((bits & ETH_GOT_IP_BIT) == ETH_GOT_IP_BIT);
}

void eth_test_fixture_deinit(void)
{
    EventBits_t bits;
    // stop Ethernet driver
    TEST_ESP_OK(esp_eth_stop(eth_handle));
    /* wait for connection stop */
    bits = xEventGroupWaitBits(eth_event_group, ETH_STOP_BIT, true, true, pdMS_TO_TICKS(ETH_STOP_TIMEOUT_MS));
    TEST_ASSERT((bits & ETH_STOP_BIT) == ETH_STOP_BIT);
    TEST_ESP_OK(esp_eth_del_netif_glue(glue));
    /* driver should be uninstalled within 2 seconds */
    TEST_ESP_OK(test_uninstall_driver(eth_handle, 2000));
    TEST_ESP_OK(phy->del(phy));
    TEST_ESP_OK(mac->del(mac));
    TEST_ESP_OK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_event_handler));
    TEST_ESP_OK(esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler));
    esp_netif_destroy(eth_netif);
    TEST_ESP_OK(esp_event_loop_delete_default());
    vEventGroupDelete(eth_event_group);
}
#endif // SOC_EMAC_SUPPORTED
