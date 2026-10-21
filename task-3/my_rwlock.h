#ifndef MY_RWLOCK_H
#define MY_RWLOCK_H

#include <pthread.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  read_cond;
    pthread_cond_t  write_cond;
    int readers;          //количество активных читателей
    int writers;          //1, если писатель активен, иначе 0
    int wait_readers;     // количество читателей в очереди
    int wait_writers;     // количество писателей в очереди
} my_rwlock_t;

static inline int my_rwlock_init(my_rwlock_t* rw) {
    rw->readers = 0;
    rw->writers = 0;
    rw->wait_readers = 0;
    rw->wait_writers = 0;
    pthread_mutex_init(&rw->mutex, NULL);
    pthread_cond_init(&rw->read_cond, NULL);
    pthread_cond_init(&rw->write_cond, NULL);
    return 0;
}

static inline int my_rwlock_destroy(my_rwlock_t* rw) {
    pthread_mutex_destroy(&rw->mutex);
    pthread_cond_destroy(&rw->read_cond);
    pthread_cond_destroy(&rw->write_cond);
    return 0;
}

static inline int my_rwlock_rdlock(my_rwlock_t* rw) {
    pthread_mutex_lock(&rw->mutex);
    rw->wait_readers++;
    // ждём, пока есть активный писатель или пока в очереди ждут писатели (защита от голодания писателей)
    while (rw->writers > 0 || rw->wait_writers > 0) {
        pthread_cond_wait(&rw->read_cond, &rw->mutex);
    }
    rw->wait_readers--;
    rw->readers++;
    pthread_mutex_unlock(&rw->mutex);
    return 0;
}

static inline int my_rwlock_wrlock(my_rwlock_t* rw) {
    pthread_mutex_lock(&rw->mutex);
    rw->wait_writers++;
    // ждём, пока есть хоть один читатель или активный писатель
    while (rw->readers > 0 || rw->writers > 0) {
        pthread_cond_wait(&rw->write_cond, &rw->mutex);
    }
    rw->wait_writers--;
    rw->writers = 1;
    pthread_mutex_unlock(&rw->mutex);
    return 0;
}

static inline int my_rwlock_unlock(my_rwlock_t* rw) {
    pthread_mutex_lock(&rw->mutex);
    if (rw->writers > 0) {
        // освобождает писатель
        rw->writers = 0;
    } else {
        // освобождает один из читателей
        rw->readers--;
    }

    // приоритет отдаем ожидающим писателям, чтобы не допустить их голодания
    if (rw->wait_writers > 0) {
        if (rw->readers == 0) {
            pthread_cond_signal(&rw->write_cond);
        }
    } else if (rw->wait_readers > 0) {
        pthread_cond_broadcast(&rw->read_cond);
    }

    pthread_mutex_unlock(&rw->mutex);
    return 0;
}

#endif