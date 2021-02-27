/*******************************************************************************
  * @file    esp8266_ap.h
  * @author  Перминов Р.И.
  * @version v0.0.0.1
  * @date    19.04.2019
  *****************************************************************************/

#ifndef ESP8266_AP_H_
#define ESP8266_AP_H_

#define ESP_AP_NAME_MAX_SIZE	32
#define ESP_AP_PASSWD_MAX_SIZE	32
#define ESP_AP_MAC_SIZE			17


/*! ---------------------------------------------------------------------------
 * @def: ESP_AP_ENC_e
 *
 * @brief: Спецификация шифрования ТД
*/
typedef enum {
	ESP_AP_ENC_OPEN,
	ESP_AP_ENC_WEP,
	ESP_AP_ENC_WPA_PSK,
	ESP_AP_ENC_WPA2_PSK,
	ESP_AP_ENC_WPA_WPA2_PSK,
	ESP_AP_ENC_WPA2_Enterprise
} ESP_AP_ENC_e;



/*! ---------------------------------------------------------------------------
 * @def: ESP_AP_CIPHER_e
 *
 * @brief: Метод шифрования ТД
*/
typedef enum {
	ESP_AP_CIPHER_NONE,
	ESP_AP_CIPHER_WEP40,
	ESP_AP_CIPHER_WEP104,
	ESP_AP_CIPHER_TKIP,
	ESP_AP_CIPHER_CCMP,
	ESP_AP_CIPHER_TKIP_CCMP,
	ESP_AP_CIPHER_UNKNOWN,
} ESP_AP_CIPHER_e;



/*! ---------------------------------------------------------------------------
 * @def: ESP_AP_BGN_t
 *
 * @brief: Стандарты работы ТД
*/
typedef union {
	struct {
		unsigned char b802_11b : 1;
		unsigned char b802_11g : 1;
		unsigned char b802_11n : 1;
	};
	unsigned char val;
} ESP_AP_BGN_t;



/*! ---------------------------------------------------------------------------
 * @def: ESP_AP_WPS_e
 *
 * @brief: Доступ функции WPS ТД
*/
typedef enum {
	ESP_AP_WPS_OFF,
	ESP_AP_WPS_ON,
} ESP_AP_WPS_e;



/*! ---------------------------------------------------------------------------
 * @def: ESP_AccessPoint_t
 *
 * @brief: Параметры точки доступа
 *
 * NOTE:
 * 		+CWLAP:(4,"MicroPython-149431",-75,"b6:e6:2d:14:94:31",1,-39,0,5,3,3,0)
*/
typedef struct {
	ESP_AP_ENC_e enc;		/* спецификация шифрования */
	char name[ESP_AP_NAME_MAX_SIZE+1];	/* имя ТД = ssid */
	int RSSI;				/* уровень сигнала */
	char MAC[ESP_AP_MAC_SIZE+1];			/* MAC-адрес ТД */
	int channel;			/* канал 2.4 ГГц */
	int freq_off;			/* смещение частоты */
	int freq_cali;			/* калибровка для смещения */
	ESP_AP_CIPHER_e pc;		/* тип шифрования */
	ESP_AP_CIPHER_e gc;		/* тип шифрования */
	ESP_AP_BGN_t bgn;		/* стандарты работы wifi */
	ESP_AP_WPS_e wps;		/* доступ WPS */
	char password[ESP_AP_PASSWD_MAX_SIZE+1];	/* пароль */
} ESP_AccessPoint_t;

#endif /* ESP8266_AP_H_ */
