#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t ssid[32];           /**< SSID of ESP8266 soft-AP */
    uint8_t password[64];       /**< Password of ESP8266 soft-AP */
    uint8_t ssid_len;           /**< Length of SSID. If softap_config.ssid_len==0, check the SSID until there is a termination character; otherwise, set the SSID length according to softap_config.ssid_len. */
    uint8_t channel;            /**< Channel of ESP8266 soft-AP */
    uint8_t ssid_hidden;        /**< Broadcast SSID or not, default 0, broadcast the SSID */
    uint8_t max_connection;     /**< Max number of stations allowed to connect in, default 4, max 4 */
    uint16_t beacon_interval;   /**< Beacon interval, 100 ~ 60000 ms, default 100 ms */
} wifi_ap_config_t;

typedef struct {
    uint8_t ssid[32];      /**< SSID of target AP*/
    uint8_t password[64];  /**< password of target AP*/
    bool bssid_set;        /**< whether set MAC address of target AP or not. Generally, station_config.bssid_set needs to be 0; and it needs to be 1 only when users need to check the MAC address of the AP.*/
    uint8_t bssid[6];     /**< MAC address of target AP*/
    uint8_t channel;       /**< channel of target AP. Set to 1~13 to scan starting from the specified channel before connecting to AP. If the channel of AP is unknown, set it to 0.*/
    uint16_t listen_interval;   /**< Listen interval for ESP8266 station to receive beacon when WIFI_PS_MAX_MODEM is set. Units: AP beacon intervals. Defaults to 3 if set to 0. */
    uint32_t rm_enabled:1;        /**< Whether radio measurements are enabled for the connection */
    uint32_t btm_enabled:1;       /**< Whether BTM is enabled for the connection */
    uint32_t reserved:30;         /**< Reserved for future feature set */
} wifi_sta_config_t;

typedef union {
    wifi_ap_config_t  ap;  /**< configuration of AP */
    wifi_sta_config_t sta; /**< configuration of STA */
} wifi_config_t;

static bool get_wifi_info_from_nvs(wifi_config_t *config)
{
        char ssid[sizeof(config->sta.ssid) + 1];
        char password[sizeof(config->sta.password) + 1];

        strcpy(ssid, "CMCC-MY");
        strcpy(password, "wysjmmqq166145");

        printf("get_wifi_info_from_nvs ssid:     %s\n", ssid);
        printf("get_wifi_info_from_nvs password: %s\n", password);

        memcpy(config->sta.ssid, ssid, sizeof(config->sta.ssid));
        memcpy(config->sta.password, password, sizeof(config->sta.password));
        return true;
}

int main(void)
{
	wifi_config_t config;

        get_wifi_info_from_nvs(&config);
        printf("get ssid: %s\n", config.sta.ssid);
        printf("get password: %s\n", config.sta.password);
        return 0;
}