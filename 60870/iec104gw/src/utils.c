
#include "iec104gw.h"

que_t *que_init(que_t *pque, int maxSize)
{
	if (!pque) return 0;
	pthread_mutex_init(&pque->mu, 0);
	pque->maxSize = maxSize;
	return pque;
}

int que_get_free_size(que_t *pque)
{
	if (!pque) return 0;
	return pque->maxSize - pque->size;
}

static void que_add_head0(que_t *pque, qitem_t *item)
{
	if (item->onHandling) return;
	if (pque->head) {
		qitem_t *head = pque->head;
		head->prev = item;
		item->next = pque->head;
	} else {
		item->next = NULL;
		pque->tail = item;
	}
	pque->size++;
	item->prev = NULL;
	item->pque = pque;
	pque->head = item;
}

void que_add_head(que_t *pque, qitem_t *item)
{
	if (item->pque) return;
	pthread_mutex_lock(&pque->mu);
	que_add_head0(pque, item);
	pthread_mutex_unlock(&pque->mu);
}

qitem_t *que_get_tail(que_t *pque)
{
	return pque->tail;
}

static qitem_t *que_get_next_for_handling0(que_t *pque)
{
	if (!pque->ihandling) {
		return pque->tail;
	} else {
		if (pque->ihandling->prev == pque->tail) return 0;
		return pque->ihandling->prev;
	}
}

qitem_t *que_get_next_for_handling(que_t *pque)
{
	pthread_mutex_lock(&pque->mu);
	qitem_t *ret = que_get_next_for_handling0(pque);
	pthread_mutex_unlock(&pque->mu);
	return ret;
}

static qitem_t *que_set_last_for_handling0(que_t *pque)
{
	pque->ihandling = que_get_next_for_handling0(pque);
	if (pque->ihandling) {
		pque->onHandling++;
		pque->ihandling->onHandling = 1;
	}
	return pque->ihandling;
}

qitem_t *que_set_last_for_handling(que_t *pque)
{
	pthread_mutex_lock(&pque->mu);
	qitem_t *ret = que_set_last_for_handling0(pque);
	pthread_mutex_unlock(&pque->mu);
	return ret;
}

static qitem_t *que_del_tail0(que_t *pque)
{
	qitem_t *item = pque->tail;
	if (item->prev) {
		qitem_t *prev = item->prev;
		prev->next = NULL;
		pque->tail = item->prev;
	} else {
		pque->tail = NULL;
		pque->head = NULL;
	}
	pque->size--;
	item->next = NULL;
	item->prev = NULL;
	item->pque = NULL;
	return item;
}

qitem_t *que_del_tail(que_t *pque)
{
	if (!pque->tail) return 0;
	pthread_mutex_lock(&pque->mu);
	qitem_t *ret = que_del_tail0(pque);
	pthread_mutex_unlock(&pque->mu);
	return ret;
}

static qitem_t *que_del_first_handled0(que_t *pque)
{
	if (!pque || !pque->tail) return 0;
	if (!pque->tail->onHandling) return 0;

	qitem_t *item = pque->tail->prev;
	if (pque->onHandling > 0) pque->onHandling--;
	if (item && (!item->onHandling || item == pque->tail)) pque->ihandling = 0;
	pque->tail->onHandling = 0;
	return que_del_tail0(pque);
}

qitem_t *que_del_first_handled(que_t *pque)
{
	if (!pque || !pque->tail) return 0;
	if (!pque->tail->onHandling) return 0;
	pthread_mutex_lock(&pque->mu);
	qitem_t *ret = que_del_first_handled0(pque);
	pthread_mutex_unlock(&pque->mu);
	return ret;
}

void que_clear_handled(que_t *pque, void (*free_func)(qitem_t *))
{
	pthread_mutex_lock(&pque->mu);
	while (pque->tail && pque->ihandling) {
		qitem_t *ret = que_del_first_handled0(pque);
		if (free_func) free_func(ret);
	}
	pthread_mutex_unlock(&pque->mu);
}

void que_clear(que_t *pque, void (*free_func)(qitem_t *))
{
	pthread_mutex_lock(&pque->mu);
	while (pque->tail) {
		qitem_t *ret = que_del_tail0(pque);
		if (free_func) free_func(ret);
	}
	pthread_mutex_unlock(&pque->mu);
}

que_t *que_deinit(que_t *pque)
{
	if (!pque) return 0;
	pthread_mutex_destroy(&pque->mu);
	return pque;
}



void smart_mutex_lock(smart_mutex_t *smutex)
{
	int rc = pthread_mutex_trylock(&((smutex)->self));
	if (rc != 0) {
		if ((smutex)->pth != pthread_self()) {
			pthread_mutex_lock(&((smutex)->self));
		}
	} else {
		(smutex)->pth = pthread_self();
	}
}

void smart_mutex_unlock(smart_mutex_t *smutex)
{
	pthread_mutex_unlock(&((smutex)->self));
	(smutex)->pth = 0;
}


#ifdef DEBUG_COUNTERS
uint64_t getsystick(void)
{
	struct timespec tv;
	clock_gettime(CLOCK_MONOTONIC,&tv);
	return ((uint64_t)tv.tv_sec*1000000L) + (uint64_t)tv.tv_nsec/1000L;
}
#endif