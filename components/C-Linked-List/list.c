#include "list.h"
#include <stdlib.h>

void list_push(List** head, void* data) {
    List* node = malloc(sizeof(List));
    if (node == NULL) {
        return;
    }

    node->data = data;
    node->next = NULL;

    if (*head == NULL) {
        *head = node;
        return;
    }

    while ((*head)->next != NULL) {
        head = &(*head)->next;
    }

    (*head)->next = node;
}

void* list_pop(List** head) {
    void* data = NULL;

    if (*head == NULL) {
        return data;
    }

    List** last = head;
    while ((*head)->next != NULL) {
        last = head;
        head = &(*head)->next;
    }

    data = (void*) (*head)->data;

    (*last)->next = NULL;

    free(*head);
    *head = NULL;

    return data;
}

void list_insert(List** head, void* data) {
    List* node = malloc(sizeof(List));
    node->data = data;
    node->next = *head;

    *head = node;
}

unsigned int list_sizeof(const List* head) {
    if (head == NULL) {
        return 0;
    }

    unsigned int size = 1;

    while (head->next != NULL) {
        size++;
        head = head->next;
    }

    return size;
}

List* list_get(List* head, unsigned int index) {
    for (unsigned int i = 0; i < index; i++) {
        head = head->next;
    }

    return head;
}

void list_remove(List** head, unsigned int index) {
    if (!head || !*head || index >= list_sizeof(*head)) return;

    if (index == 0) {
        List* prev_head = (*head);
        (*head) = (*head)->next;
        free(prev_head);
    } else {
        List* a_cursor = (*head);
        for (unsigned int i = 1; i < index; i++) {
            a_cursor = a_cursor->next;
        }
        List* tmp_cursor = a_cursor->next;
        tmp_cursor = tmp_cursor->next;
        free(a_cursor->next);
        a_cursor->next = tmp_cursor;
    }
}

