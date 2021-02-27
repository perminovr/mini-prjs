/*******************************************************************************
  * @file    utils.h
  * @author  Перминов Р.И.
  * @version v 1
  * @date    12.02.2020
  *****************************************************************************/

/*

    COMMON

*/


#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#ifdef typeof
    #define container_of(ptr, type, member) ({                      \
            const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
            (type *)( (char *)__mptr - offsetof(type,member) );})
#else
    #define container_of(ptr, type, member) ({ \
            (type *)( (char *)ptr - offsetof(type,member) );})
#endif


static int extractNumbers(const char *string, uint8_t *array, int arraySize)
{
	int ret = 0;
	char buffer[256];
	snprintf(buffer, sizeof(buffer)-1, "%s.", string);
	buffer[sizeof(buffer)-2] = '.';
	buffer[sizeof(buffer)-1] = 0;
	int strSize = strlen(buffer);

	int i = 0;
	int findForEnd = 0;
	char *start = 0;
	for (; i < strSize && ret != arraySize; ++i) {
		if (isdigit(buffer[i])) { // for first numeral
			if (!start) {
				start = buffer+i;
			}
		} else { // first alpha
			if (start) { // after numeral(s)
				int value = atoi(start);
				if (findForEnd) { // we are finding for end of range
					findForEnd = 0;
					if (ret > 0) { // paranoid if
						for (int j = (int)array[ret-1]+1; j < value && ret != arraySize; ++j) {
							array[ret++] = (uint8_t)j;
						}
					}
				}
				if (ret != arraySize) {
					array[ret++] = (uint8_t)value;
					start = 0;
				}
			}
			if (ret > 0) { // range
				// hyphen or two dots
				if (buffer[i] == '-' || (i > 0 && buffer[i-1] == '.' && buffer[i] == '.')) {
					findForEnd = 1; // find for end of range
				}
			}
		}
	}
	return ret;
}


/*

    LIST

*/



typedef struct list_s list_t;
struct list_s {
    list_t *next;
};



static list_t *list_add(list_t **head, list_t *new)
{
    if (!head || !new)
        return 0;

    list_t *lhead = *head;

    if (new) {
        new->next = 0;
        if (lhead) {
            for (;lhead->next;lhead=lhead->next)
                ;
            lhead->next = new;
        } else {
            *head = new;
        }
    }

    return new;
}



typedef list_t *(*list_create_node)(void);
static list_t *list_add_alloc(list_t **head, list_create_node ncreate)
{
    if (!head || !ncreate)
        return 0;
    return list_add(head, (list_t*)ncreate());
}



typedef void(*list_free_node)(void *head);
static void list_free(list_t **head, list_free_node nfree)
{
    if (!head || !nfree)
        return;

    list_t *lhead = *head;
    for (list_t *l = lhead->next; l; l = l->next) {
        nfree(lhead);
        lhead = l;
    }
    nfree(lhead);
    *head = 0;
}



/*

    QUEUE

*/



typedef struct {
	void *head;
	void *tail;
} queue_t;



typedef struct {
	void *next;
	void *prev;
	queue_t *queue;
} qitem_t;



static qitem_t *que_get_head(queue_t *queue)
{
    return queue->head;
}



static int que_add_tail(queue_t *queue, qitem_t *item)
{
	qitem_t *tail;

	if (item->queue)
        return 1;

	if (queue->tail) {
		tail=(qitem_t*)queue->tail;
		tail->next=item;
		item->prev=queue->tail;
	} else {
		item->prev=0;
		queue->head=item;
	}

	item->next=0;
	item->queue=queue;
	queue->tail=item;

	return 0;
}



static void que_del_item(qitem_t *item)
{
	queue_t *queue;
	qitem_t *next;
	qitem_t *prev;

	if (!item->queue) return;

	queue=item->queue;

	if (item->next) {
		next=(qitem_t*)item->next;
		next->prev=item->prev;
	}

	if (item->prev) {
		prev=(qitem_t*)item->prev;
		prev->next=item->next;
	}

	if (item == queue->head)
        queue->head=item->next;
	if (item == queue->tail)
        queue->tail=item->prev;

	item->next=0;
	item->prev=0;
	item->queue=0;
}



/*

    TREE

*/



#ifndef GETNEXTNODE_DEEP
#define GETNEXTNODE_DEEP    10
#endif



#define GetNextNode(v) _GetNextNode(v,0,0)



static int GetNextNode_valueIsArray(void *val);
static void *GetNextNode_getArrayElem(void *val, int idx);
static int GetNextNode_getArraySize(void *val);
static int GetNextNode_getIndex(void *val);
static void *GetNextNode_getParent(void *val);



static void *_GetNextNode(void *val, int lastChild, int deep)
{
    deep++;
    if (!val || deep > GETNEXTNODE_DEEP) {
        deep--;
        return 0;
    }

    void *parent = GetNextNode_getParent(val);
    int idx = GetNextNode_getIndex(val);
    void *ret = 0;
    int i;

    // this val is an array
    if (GetNextNode_valueIsArray(val)) {
        // the last child in this array (val)
        if (lastChild) {
            // has a parent
            if (parent) {
                int size = GetNextNode_getArraySize(parent);
                // get the next not null child
                for (i = idx+1; i < size; ++i) {
                    ret = GetNextNode_getArrayElem(parent, i);
                    if (ret) break;
                }
                // the next child found
                if (ret) {
                    ret = GetNextNode_valueIsArray(ret)?
                        _GetNextNode(ret, 0, deep) : ret;
                }
                // this val is the last parent child or
                //      all other parent child are null
                else {
                    ret = _GetNextNode(parent, 1, deep);
                }
            }
            // no parent -> ret = 0
        }
        // this val is array and not last -> get first not null child
        else {
            int size = GetNextNode_getArraySize(val);
            for (i = 0; i < size; ++i) {
                ret = GetNextNode_getArrayElem(val, i);
                if (ret) break;
            }
            // the child found
            if (ret) {
                ret = GetNextNode_valueIsArray(ret)?
                    _GetNextNode(ret, 0, deep) : ret;
            }
            // all child are null or this is an empty array
            else {
                ret = _GetNextNode(val, 1, deep);
            }
        }
    }
    // this val is simple val
    else {
        // this val couldn't has child
        if (!lastChild) {
            // has parent
            if (parent) {
                int size = GetNextNode_getArraySize(parent);
                // get the next not null child
                for (i = idx+1; i < size; ++i) {
                    ret = GetNextNode_getArrayElem(parent, i);
                    if (ret) break;
                }
                // this val is the last parent child or
                //      all other parent child are null
                if (!ret) {
                    ret = _GetNextNode(parent, 1, deep);
                }
            }
            // no parent: may be have actually one orphan value
            else {
                ret = val;
            }
        }
    }

    deep--;
    return ret;
}



