


namespace SinglyLinked {

    // TODO: specialize for sets and dicts to search item directly.
    // -> remember to cast curr to Hashed or Mapped whenever we advance to next/prev

    /*Insert an item at the given index.*/
    template <typename NodeType>
    void insert(ListView<NodeType>* PyObject* item, long long index) {
        // normalize index
        size_t norm_index = normalize_index(index, view->size, true);

        // allocate a new node
        NodeType* node = view->allocate(item);
        if (node == NULL) {  // MemoryError() or TypeError() during hash()
            return;
        }

        // iterate from head
        NodeType* curr = view->head;
        NodeType* prev = NULL;  // shadows curr
        for (size_t i = 0; i < norm_index; i++) {
            prev = curr;
            curr = curr->next;
        }

        // insert node
        view->link(prev, node, curr);
    }

    // TODO: specialize for sets and dicts to search for item before iterating.

}


namespace DoublyLinked {

    /*Insert an item at the given index.*/
    template <template <typename> class ViewType, typename NodeType>
    void insert(ViewType<NodeType>* view, long long index, PyObject* item) {
        // normalize index
        size_t norm_index = normalize_index(index, view->size, true);

        // if index is closer to head, use singly-linked version
        if (norm_index <= view->size / 2) {
            return SingleIndex::insert(view, item, index);
        }

        // else, start from tail
        NodeType* node = view->allocate(item);
        if (node == NULL) {  // MemoryError() or TypeError() during hash()
            return;
        }

        // find node at index
        NodeType* curr = view->tail;
        NodeType* next = NULL;  // shadows curr
        for (size_t i = view->size - 1; i > norm_index; i--) {
            next = curr;
            curr = curr->prev;
        }

        // insert node
        view->link(curr, node, next);
    }

}


