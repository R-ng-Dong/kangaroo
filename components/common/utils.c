#include "utils.h"

#define MAX_SIZE  512

void Swap(UI16 *a, UI16 *b)
{
    if(!a || !b)
        return;
    UI16 temp = *a;
    *a = *b;
    *b = temp;
}

static void MergeParts(UI16 *arr, UI16 l, UI16 mid, UI16 r) {
    UI16 i = l, j = mid + 1;
    UI16 k = 0;
    UI16 temp[MAX_SIZE];
    while (i <= mid && j <= r) {
        UI16 nextValue;
 
        if (arr[i] < arr[j]) nextValue = arr[i++];
        else nextValue = arr[j++];
 
        temp[k++] = nextValue;
    }
 
    while (i <= mid) temp[k++] = arr[i++];
    while (j <= r) temp[k++] = arr[j++];
 
    for (UI16 i=0; i<k; i++) arr[l + i] = temp[i];
 
}
 
void MergeSort(UI16 *arr, UI16 l, UI16 r) {
    if (l >= r) return;
 
    UI16 mid = (l + r) / 2;
 
    MergeSort(arr, l, mid);
    MergeSort(arr, mid + 1, r);
 
    MergeParts(arr, l, mid, r);
}