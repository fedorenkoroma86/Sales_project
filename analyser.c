#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include "structs.h"

// ---------- Глобальные данные ----------
Product  products[MAX_PRODUCTS];
int      product_count = 0;

Customer customers[MAX_CUSTOMERS];
int      customer_count = 0;

Sale     sales[MAX_SALES];
int      sale_count = 0;

// Пороговая сумма (меняется пользователем)
int threshold_sum = 500;

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *log_file = NULL;

void log_event(const char *msg) {
    pthread_mutex_lock(&data_mutex);
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(log_file, "[%s] %s\n", buf, msg);
    fflush(log_file);
    pthread_mutex_unlock(&data_mutex);
}

// ---------- Загрузка данных ----------
int load_products() {
    FILE *f = fopen("products.txt", "r");
    if (!f) { printf("Ошибка: products.txt не найден\n"); return 0; }
    product_count = 0;
    while (fscanf(f, "%d;%49[^;];%d\n",
           &products[product_count].id,
           products[product_count].name,
           &products[product_count].price) == 3)
        product_count++;
    fclose(f);
    return 1;
}

int load_customers() {
    FILE *f = fopen("customers.txt", "r");
    if (!f) { printf("Ошибка: customers.txt не найден\n"); return 0; }
    customer_count = 0;
    while (fscanf(f, "%d;%49[^\n]\n",
           &customers[customer_count].id,
           customers[customer_count].name) == 2)
        customer_count++;
    fclose(f);
    return 1;
}

int load_sales() {
    FILE *f = fopen("sales.txt", "r");
    if (!f) { printf("Ошибка: sales.txt не найден\n"); return 0; }
    sale_count = 0;
    while (fscanf(f, "%d;%d;%d;%d\n",
           &sales[sale_count].id,
           &sales[sale_count].timestamp,
           &sales[sale_count].customer_id,
           &sales[sale_count].product_id) == 4)
        sale_count++;
    fclose(f);
    return 1;
}

int get_product_price(int product_id) {
    for (int i = 0; i < product_count; i++)
        if (products[i].id == product_id) return products[i].price;
    return 0;
}

const char* get_product_name(int product_id) {
    for (int i = 0; i < product_count; i++)
        if (products[i].id == product_id) return products[i].name;
    return "Неизвестно";
}

const char* get_customer_name(int customer_id) {
    for (int i = 0; i < customer_count; i++)
        if (customers[i].id == customer_id) return customers[i].name;
    return "Неизвестно";
}

// ---------- Результаты потоков ----------
typedef struct {
    int start;
    int end;
    long long partial_sum;
    int product_sales[MAX_PRODUCTS];   // сколько раз продан каждый товар
    int customer_spending[MAX_CUSTOMERS]; // сумма покупок каждого клиента
} ThreadResult;

#define NUM_WORKERS 4

// Поток 1: общая сумма продаж
void *calc_total_sum(void *arg) {
    ThreadResult *tr = (ThreadResult *)arg;
    tr->partial_sum = 0;
    for (int i = tr->start; i < tr->end; i++) {
        tr->partial_sum += get_product_price(sales[i].product_id);
    }
    return NULL;
}

// Поток 2: популярность товаров
void *calc_product_popularity(void *arg) {
    ThreadResult *tr = (ThreadResult *)arg;
    memset(tr->product_sales, 0, sizeof(tr->product_sales));
    for (int i = tr->start; i < tr->end; i++) {
        for (int p = 0; p < product_count; p++) {
            if (products[p].id == sales[i].product_id) {
                tr->product_sales[p]++;
                break;
            }
        }
    }
    return NULL;
}

// Поток 3: траты покупателей
void *calc_customer_spending(void *arg) {
    ThreadResult *tr = (ThreadResult *)arg;
    memset(tr->customer_spending, 0, sizeof(tr->customer_spending));
    for (int i = tr->start; i < tr->end; i++) {
        int price = get_product_price(sales[i].product_id);
        for (int c = 0; c < customer_count; c++) {
            if (customers[c].id == sales[i].customer_id) {
                tr->customer_spending[c] += price;
                break;
            }
        }
    }
    return NULL;
}

// Поток 4: тенденции продаж (по часам)
typedef struct {
    int start;
    int end;
    int hourly_sales[24];
} TrendResult;

void *calc_trends(void *arg) {
    TrendResult *tr = (TrendResult *)arg;
    memset(tr->hourly_sales, 0, sizeof(tr->hourly_sales));
    for (int i = tr->start; i < tr->end; i++) {
        time_t t = (time_t)sales[i].timestamp;
        struct tm *tm_info = localtime(&t);
        tr->hourly_sales[tm_info->tm_hour]++;
    }
    return NULL;
}

// ---------- Генерация отчёта ----------
void generate_report() {
    FILE *report = fopen("report.txt", "w");
    if (!report) { printf("Ошибка создания отчёта\n"); return; }

    fprintf(report, "=== ОТЧЁТ О ПРОДАЖАХ ===\n");
    fprintf(report, "Дата генерации: %s\n", ctime(&(time_t){time(NULL)}));
    fprintf(report, "Всего продаж: %d\n\n", sale_count);

    // 1. Общая сумма
    pthread_t threads[NUM_WORKERS];
    ThreadResult results[NUM_WORKERS];
    int chunk = sale_count / NUM_WORKERS;

    for (int i = 0; i < NUM_WORKERS; i++) {
        results[i].start = i * chunk;
        results[i].end = (i == NUM_WORKERS - 1) ? sale_count : (i + 1) * chunk;
        pthread_create(&threads[i], NULL, calc_total_sum, &results[i]);
    }
    long long total_sum = 0;
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(threads[i], NULL);
        total_sum += results[i].partial_sum;
    }
    fprintf(report, "1. ОБЩАЯ СУММА ПРОДАЖ: %lld руб.\n\n", total_sum);

    // 2. Топ-5 популярных и непопулярных товаров
    ThreadResult pop_results[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; i++) {
        pop_results[i].start = i * chunk;
        pop_results[i].end = (i == NUM_WORKERS - 1) ? sale_count : (i + 1) * chunk;
        pthread_create(&threads[i], NULL, calc_product_popularity, &pop_results[i]);
    }
    int total_sales_per_product[MAX_PRODUCTS] = {0};
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(threads[i], NULL);
        for (int p = 0; p < product_count; p++)
            total_sales_per_product[p] += pop_results[i].product_sales[p];
    }

    // Сортировка по убыванию
    int sorted_idx[MAX_PRODUCTS];
    for (int i = 0; i < product_count; i++) sorted_idx[i] = i;
    for (int i = 0; i < product_count - 1; i++)
        for (int j = i + 1; j < product_count; j++)
            if (total_sales_per_product[sorted_idx[j]] > total_sales_per_product[sorted_idx[i]]) {
                int tmp = sorted_idx[i]; sorted_idx[i] = sorted_idx[j]; sorted_idx[j] = tmp;
            }

    fprintf(report, "2. ТОП-5 ПОПУЛЯРНЫХ ТОВАРОВ:\n");
    for (int i = 0; i < 5 && i < product_count; i++)
        fprintf(report, "   %d. %s — продан %d раз(а)\n",
                i + 1, products[sorted_idx[i]].name, total_sales_per_product[sorted_idx[i]]);

    fprintf(report, "\n   ТОП-5 НЕПОПУЛЯРНЫХ ТОВАРОВ:\n");
    for (int i = 0; i < 5 && i < product_count; i++) {
        int idx = product_count - 1 - i;
        fprintf(report, "   %d. %s — продан %d раз(а)\n",
                i + 1, products[sorted_idx[idx]].name, total_sales_per_product[sorted_idx[idx]]);
    }
    fprintf(report, "\n");

    // 3. Покупатели выше порога
    ThreadResult spend_results[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; i++) {
        spend_results[i].start = i * chunk;
        spend_results[i].end = (i == NUM_WORKERS - 1) ? sale_count : (i + 1) * chunk;
        pthread_create(&threads[i], NULL, calc_customer_spending, &spend_results[i]);
    }
    int total_spending[MAX_CUSTOMERS] = {0};
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(threads[i], NULL);
        for (int c = 0; c < customer_count; c++)
            total_spending[c] += spend_results[i].customer_spending[c];
    }

    fprintf(report, "3. ПОКУПАТЕЛИ С СУММОЙ БОЛЕЕ %d руб.:\n", threshold_sum);
    int found = 0;
    for (int c = 0; c < customer_count; c++) {
        if (total_spending[c] > threshold_sum) {
            fprintf(report, "   %s (ID=%d) — %d руб.\n",
                    customers[c].name, customers[c].id, total_spending[c]);
            found++;
        }
    }
    if (!found) fprintf(report, "   (нет таких покупателей)\n");
    fprintf(report, "\n");

    // 4. Тенденции продаж по часам
    TrendResult trend_results[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; i++) {
        trend_results[i].start = i * chunk;
        trend_results[i].end = (i == NUM_WORKERS - 1) ? sale_count : (i + 1) * chunk;
        pthread_create(&threads[i], NULL, calc_trends, &trend_results[i]);
    }
    int hourly[24] = {0};
    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(threads[i], NULL);
        for (int h = 0; h < 24; h++)
            hourly[h] += trend_results[i].hourly_sales[h];
    }

    fprintf(report, "4. ТЕНДЕНЦИИ ПРОДАЖ (по часам):\n");
    int max_hour = 0;
    for (int h = 0; h < 24; h++) {
        fprintf(report, "   %02d:00 — %d продаж", h, hourly[h]);
        for (int b = 0; b < hourly[h] && b < 50; b++) fprintf(report, "#");
        fprintf(report, "\n");
        if (hourly[h] > hourly[max_hour]) max_hour = h;
    }
    fprintf(report, "\n   Пик продаж: %02d:00\n", max_hour);

    fclose(report);
    printf("Отчёт сохранён в report.txt\n");
    log_event("Отчёт сгенерирован");
}

// ---------- Меню ----------
void show_menu() {
    printf("\n===== МЕНЮ =====\n");
    printf("1. Перезагрузить данные и сгенерировать отчёт\n");
    printf("2. Изменить пороговую сумму (текущая: %d)\n", threshold_sum);
    printf("3. Показать сводку в консоли\n");
    printf("0. Выход\n");
    printf("Выбор: ");
}

int main() {
    log_file = fopen("analyzer.log", "a");
    if (!log_file) { printf("Ошибка: analyzer.log\n"); return 1; }
    log_event("=== Анализатор продаж запущен ===");

    if (!load_products())  return 1;
    if (!load_customers()) return 1;

    printf("Товаров: %d, Покупателей: %d\n", product_count, customer_count);

    int choice;
    while (1) {
        show_menu();
        scanf("%d", &choice);

        switch (choice) {
            case 1:
                if (!load_sales()) { printf("Нет данных о продажах\n"); break; }
                printf("Загружено продаж: %d\n", sale_count);
                generate_report();
                break;
            case 2:
                printf("Введите новую пороговую сумму: ");
                scanf("%d", &threshold_sum);
                log_event("Пороговая сумма изменена");
                break;
            case 3:
                if (!load_sales()) { printf("Нет данных\n"); break; }
                printf("Всего продаж: %d\n", sale_count);
                break;
            case 0:
                log_event("Анализатор завершён");
                fclose(log_file);
                return 0;
            default:
                printf("Неверный выбор\n");
        }
    }
    return 0;
}