#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include "structs.h"

#define NUM_THREADS 4

Product products[MAX_PRODUCTS];
int product_count = 0;

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *sales_file = NULL;
int global_sale_id = 1;

// Логирование
FILE *log_file = NULL;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_event(const char *msg) {
    pthread_mutex_lock(&log_mutex);
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(log_file, "[%s] %s\n", buf, msg);
    fflush(log_file);
    pthread_mutex_unlock(&log_mutex);
}

int load_products() {
    FILE *f = fopen("products.txt", "r");
    if (!f) {
        printf("Ошибка: не найден файл products.txt\n");
        return 0;
    }
    product_count = 0;
    while (fscanf(f, "%d;%49[^;];%d\n",
           &products[product_count].id,
           products[product_count].name,
           &products[product_count].price) == 3) {
        product_count++;
    }
    fclose(f);
    return 1;
}

// Каждый поток имитирует продажу одного конкретного товара
typedef struct {
    int thread_id;
    int product_index;   // индекс товара в массиве
    int interval_sec;    // интервал между продажами
} ThreadArg;

void *sell_product(void *arg) {
    ThreadArg *ta = (ThreadArg *)arg;
    char msg[256];

    snprintf(msg, sizeof(msg), "Поток %d запущен: продаёт товар '%s' (id=%d) каждые %d сек",
             ta->thread_id, products[ta->product_index].name,
             products[ta->product_index].id, ta->interval_sec);
    log_event(msg);

    while (1) {
        Sale s;
        pthread_mutex_lock(&file_mutex);
        s.id = global_sale_id++;
        pthread_mutex_unlock(&file_mutex);

        s.timestamp = (int)time(NULL);
        s.customer_id = rand() % 50 + 1;
        s.product_id = products[ta->product_index].id;

        pthread_mutex_lock(&file_mutex);
        fprintf(sales_file, "%d;%d;%d;%d\n",
                s.id, s.timestamp, s.customer_id, s.product_id);
        fflush(sales_file);
        pthread_mutex_unlock(&file_mutex);

        snprintf(msg, sizeof(msg), "Поток %d: Продажа #%d — товар '%s' (клиент #%d)",
                 ta->thread_id, s.id, products[ta->product_index].name, s.customer_id);
        log_event(msg);
        printf("%s\n", msg);

        sleep(ta->interval_sec);
    }
    return NULL;
}

int main() {
    srand(time(NULL));

    log_file = fopen("generator.log", "a");
    if (!log_file) {
        printf("Ошибка: не могу открыть generator.log\n");
        return 1;
    }
    log_event("=== Генератор продаж запущен ===");

    if (!load_products()) return 1;

    printf("Найдено товаров: %d\n", product_count);

    sales_file = fopen("sales.txt", "a");
    if (!sales_file) {
        printf("Ошибка: не могу открыть sales.txt\n");
        return 1;
    }

    pthread_t threads[NUM_THREADS];
    ThreadArg args[NUM_THREADS];

    // Каждый поток продаёт свой товар (берём первые NUM_THREADS товаров)
    for (int i = 0; i < NUM_THREADS && i < product_count; i++) {
        args[i].thread_id = i + 1;
        args[i].product_index = i;
        args[i].interval_sec = rand() % 3 + 1; // 1-3 секунды

        if (pthread_create(&threads[i], NULL, sell_product, &args[i]) != 0) {
            printf("Ошибка создания потока %d\n", i + 1);
            return 1;
        }
    }

    // Ждём завершения (бесконечно, т.к. потоки в цикле while(1))
    for (int i = 0; i < NUM_THREADS && i < product_count; i++) {
        pthread_join(threads[i], NULL);
    }

    fclose(sales_file);
    fclose(log_file);
    return 0;
}