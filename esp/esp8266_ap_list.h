/*******************************************************************************
  * @file    esp8266_ap_list.h
  * @author  Перминов Р.И.
  * @version v0.0.0.1
  * @date    19.04.2019
  *****************************************************************************/

#ifndef ESP8266_AP_LIST_H_
#define ESP8266_AP_LIST_H_

#include "esp8266_ap.h"



/*! ---------------------------------------------------------------------------
 * @def: ESP_APList_Sort_e
 *
 * @brief: Метод сортировки списка ТД
*/
typedef enum {
	ESP_APLL_S_No,
	ESP_APLL_S_SSID,
	ESP_APLL_S_RSSI,
} ESP_APList_Sort_e;



typedef struct ESP_AccessPoint_Node_s ESP_AccessPoint_Node_t;

struct ESP_AccessPoint_Node_s {
	ESP_AccessPoint_t ap;
	ESP_AccessPoint_Node_t *next;
};



/*! ---------------------------------------------------------------------------
 * @def: ESP_AccessPoint_List_t
 *
 * @brief: Список точек доступа
*/
typedef struct {
	ESP_AccessPoint_Node_t *head;
	int count;
	ESP_APList_Sort_e sortType;
} ESP_AccessPoint_List_t;



/*! ---------------------------------------------------------------------------
 * @fn: ESP_SortAccessPointList
 *
 * @brief: Сортировка списка ТД
 *
 * input parameters
 * @param list - список ТД
 *
 * output parameters
 * @param list - список ТД
 *
 * no return
*/
extern void ESP_SortAccessPointList(ESP_AccessPoint_List_t *list);



#endif /* ESP8266_AP_LIST_H_ */
