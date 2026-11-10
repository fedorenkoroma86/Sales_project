#ifndef STRUCTS_H
#define STRUCTS_H

#define MAX_PRODUCTS 100
#define MAX_CUSTOMERS 200
#define MAX_SALES 10000
#define NAME_LEN 50

typedef struct {
    int id;
    char name[NAME_LEN];
    int price;
} Product;

typedef struct {
    int id;
    char name[NAME_LEN];
} Customer;

typedef struct {
    int id;
    int timestamp; 
    int customer_id;
    int product_id;
} Sale;

#endif