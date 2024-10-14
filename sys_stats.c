
#include <time.h>
#include <stdio.h>

typedef enum {
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE
} Operation;

double measure_operation_speed(Operation op) {
    const long long num_operations = 1000000000LL; // 1 billion operations
    long long i;
    volatile long long result = 1; // Initialize to 1 for multiply and divide

    clock_t start = clock();

    switch (op) {
        case OP_ADD:
            for (i = 0; i < num_operations; i++) {
                result += 1;
            }
            break;
        case OP_SUBTRACT:
            for (i = 0; i < num_operations; i++) {
                result -= 1;
            }
            break;
        case OP_MULTIPLY:
            for (i = 0; i < num_operations; i++) {
                result *= 2;
            }
            break;
        case OP_DIVIDE:
            for (i = 0; i < num_operations; i++) {
                result /= 2;
            }
            break;
    }

    clock_t end = clock();

    double cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    double operations_per_second = num_operations / cpu_time_used;

    return operations_per_second;
}

double ops_per_second_to_ns_per_op(double ops_per_second) {
    return 1e9 / ops_per_second;
}

void print_operation_speed(const char* op_name, double ops_per_second) {
    double ns_per_op = ops_per_second_to_ns_per_op(ops_per_second);
    printf("%s speed: %.2f ns per operation\n", op_name, ns_per_op);
}

int main() {
    double add_speed = measure_operation_speed(OP_ADD);
    double subtract_speed = measure_operation_speed(OP_SUBTRACT);
    double multiply_speed = measure_operation_speed(OP_MULTIPLY);
    double divide_speed = measure_operation_speed(OP_DIVIDE);

    print_operation_speed("Addition", add_speed);
    print_operation_speed("Subtraction", subtract_speed);
    print_operation_speed("Multiplication", multiply_speed);
    print_operation_speed("Division", divide_speed);

    return 0;
}