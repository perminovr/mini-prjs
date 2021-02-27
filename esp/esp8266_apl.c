/*******************************************************************************
  * @file    esp8266_apl.c
  * @author  Перминов Р.И.
  * @version v0.0.0.1
  * @date    19.04.2019
  *****************************************************************************/

#include "esp8266_ap_list.h"
#include <string.h>
#include <ctype.h>



#define BUBBLE_SORT_NODE_TYPE	ESP_AccessPoint_Node_t
#define BUBBLE_SORT_LIST_TYPE	ESP_AccessPoint_List_t



/*! ---------------------------------------------------------------------------
 * @fn: regfreecmp
 *
 * @brief: Сравнение строк без учета регистра
 *
 * input parameters
 * @param s1 - строка
 * @param s2 - строка
 *
 * output parameters
 *
 * return
 * 		-1: s1 < s2
 * 		 0: s1 = s2
 * 		 1: s1 > s2
*/
static int regfreecmp(const char *s1, const char *s2)
{
	for (; toupper(*s1) == toupper(*s2); ++s1, ++s2)
		if (*s1 == 0 || *s2 == 0)
			return 0;
	return ((toupper(*s1) < toupper(*s2)) ? -1 : +1);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_SortCond_SSID
 *
 * @brief: Условие сортировки списка ТД по SSID
 *
 * input parameters
 * @param a - элемент списка
 * @param b - элемент списка
 *
 * output parameters
 *
 * return a->ssid > b->ssid ?
*/
static int _ESP_SortCond_SSID(BUBBLE_SORT_NODE_TYPE *a, BUBBLE_SORT_NODE_TYPE *b)
{
	return (regfreecmp(a->ap.name, b->ap.name) > 0);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_BubbleSort
 *
 * @brief: Сортировка списка ТД по SSID
 *
 * input parameters
 * @param list - список ТД
 * @param sortCond - условие сортировки
 *
 * output parameters
 * @param list - список ТД
 *
 * no return
*/
static void _ESP_BubbleSort(BUBBLE_SORT_LIST_TYPE *list, int (*sortCond)(BUBBLE_SORT_NODE_TYPE *, BUBBLE_SORT_NODE_TYPE *))
{
	if (!list || !list->head || !list->head->next)
		return;

	BUBBLE_SORT_NODE_TYPE *a = list->head;
	BUBBLE_SORT_NODE_TYPE *b = list->head->next;
	BUBBLE_SORT_NODE_TYPE *c = 0;
	int swpd = 0;

	for (;;) {
		for (;;) {
			if (a && b && sortCond(a, b)) {
				if (a == list->head) {
					list->head = b;
					a->next = b->next;
					b->next = a;
				} else if (c) {
					c->next = b;
					a->next = b->next;
					b->next = a;
				}
				c = b;
				b = a->next;
				swpd = 1;
			} else {
				c = a;
				a = b;
				b = b->next;
			}
			if (!b) break;
		}
		if (swpd) {
			swpd = 0;
		} else {
			return;
		}
		a = list->head;
		b = list->head->next;
	}
}



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
void ESP_SortAccessPointList(ESP_AccessPoint_List_t *list)
{
	switch (list->sortType) {
		case ESP_APLL_S_SSID: {
			_ESP_BubbleSort(list, _ESP_SortCond_SSID);
		} break;
		case ESP_APLL_S_RSSI: {
			return; // already done
		} break;
		default: break;
	}
}
