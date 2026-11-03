#include "timer_heap.h"

#include "IOT_Memory.h"

static const char *TAG = "timer_heap";

static void swapEntries(TimerHeapEntry_t *a, TimerHeapEntry_t *b)
{
    TimerHeapEntry_t temp = *a;
    *a = *b;
    *b = temp;
}

static void siftUp(TimerHeap_t *h, size_t idx)
{
    while (idx > 0)
    {
        size_t parent = (idx - 1) / 2;
        if (h->entries[idx].dueMs >= h->entries[parent].dueMs)
            break;
        swapEntries(&h->entries[idx], &h->entries[parent]);
        idx = parent;
    }
}

static void siftDown(TimerHeap_t *h, size_t idx)
{
    while (true)
    {
        size_t smallest = idx;
        size_t left = 2 * idx + 1;
        size_t right = 2 * idx + 2;

        if (left < h->size && h->entries[left].dueMs < h->entries[smallest].dueMs)
            smallest = left;

        if (right < h->size && h->entries[right].dueMs < h->entries[smallest].dueMs)
            smallest = right;

        if (smallest == idx)
            break;

        swapEntries(&h->entries[idx], &h->entries[smallest]);
        idx = smallest;
    }
}

bool TimerHeap_Init(TimerHeap_t *h, size_t capacity)
{
    if (h == NULL)
        return false;
    if (capacity == 0)
        capacity = 16;

    h->entries = Mem_SafeMalloc(sizeof(TimerHeapEntry_t) * capacity, TAG, "timer events");
    h->size = 0;
    h->capacity = (h->entries != NULL) ? capacity : 0;
    return h->entries != NULL;
}

bool TimerHeap_Push(TimerHeap_t *h, const TimerHeapEntry_t *e)
{
    if (h == NULL || e == NULL)
        return false;

    if (h->size >= h->capacity)
    {
        size_t newCapacity = (h->capacity == 0) ? 16 : h->capacity * 2;
        TimerHeapEntry_t *grown =
            Mem_SafeRealloc(h->entries, sizeof(TimerHeapEntry_t) * newCapacity, TAG, "timer events grow");
        if (grown == NULL)
            return false;
        h->entries = grown;
        h->capacity = newCapacity;
    }

    h->entries[h->size] = *e;
    siftUp(h, h->size);
    h->size++;
    return true;
}

bool TimerHeap_PopDue(TimerHeap_t *h, uint64_t nowMs, TimerHeapEntry_t *out)
{
    if (h == NULL || out == NULL || h->size == 0 || h->entries[0].dueMs > nowMs)
        return false;

    *out = h->entries[0];
    h->size--;
    if (h->size > 0)
    {
        h->entries[0] = h->entries[h->size];
        siftDown(h, 0);
    }
    return true;
}

bool TimerHeap_Remove(TimerHeap_t *h, IOT_TimerHandle_t id)
{
    if (h == NULL)
        return false;

    for (size_t i = 0; i < h->size; i++)
    {
        if (h->entries[i].id == id)
        {
            /* Move the last entry into the hole, then restore order in whichever direction
             * it is out of place. */
            h->size--;
            if (i < h->size)
            {
                h->entries[i] = h->entries[h->size];
                siftDown(h, i);
                siftUp(h, i);
            }
            return true; /* handles are unique */
        }
    }
    return false;
}

void TimerHeap_Clear(TimerHeap_t *h)
{
    if (h != NULL)
        h->size = 0;
}
