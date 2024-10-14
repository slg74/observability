#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

typedef enum {
    OP_INT_ADD,
    OP_INT_SUBTRACT,
    OP_INT_MULTIPLY,
    OP_INT_DIVIDE,
    OP_FLOAT_ADD,
    OP_FLOAT_MULTIPLY,
    OP_FLOAT_DIVIDE,
    OP_SINE,
    OP_COSINE,
    OP_TANGENT,
    OP_ARCTANGENT

} Operation;

double measure_operation_speed(Operation op) {
    const long long num_operations = 1000000000LL; // 1 billion operations
    long long i;
    volatile long long int_result = 1;
    volatile double float_result = 1.0;

    clock_t start = clock();

    switch (op) {
        case OP_INT_ADD:
            for (i = 0; i < num_operations; i++) int_result += 1;
            break;
        case OP_INT_SUBTRACT:
            for (i = 0; i < num_operations; i++) int_result -= 1;
            break;
        case OP_INT_MULTIPLY:
            for (i = 0; i < num_operations; i++) int_result *= 2;
            break;
        case OP_INT_DIVIDE:
            for (i = 0; i < num_operations; i++) int_result /= 2;
            break;
        case OP_FLOAT_ADD:
            for (i = 0; i < num_operations; i++) float_result += 1.0;
            break;
        case OP_FLOAT_MULTIPLY:
            for (i = 0; i < num_operations; i++) float_result *= 1.01;
            break;
        case OP_FLOAT_DIVIDE:
            for (i = 0; i < num_operations; i++) float_result /= 1.01;
            break;
        case OP_SINE:
            for (i = 0; i < num_operations; i++) float_result = sin(float_result);
            break;
        case OP_COSINE:
            for (i = 0; i < num_operations; i++) float_result = cos(float_result);
            break;
        case OP_TANGENT:
            for (i = 0; i < num_operations; i++) float_result = tan(float_result);
            break;
        case OP_ARCTANGENT:
            for (i = 0; i < num_operations; i++) float_result = atan(float_result);
            break;
    }

    clock_t end = clock();

    double cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    return num_operations / cpu_time_used;
}

double ops_per_second_to_ns_per_op(double ops_per_second) {
    return 1e9 / ops_per_second;
}

void print_operation_speed(const char* op_name, double ops_per_second) {
    double ns_per_op = ops_per_second_to_ns_per_op(ops_per_second);
    printf("%s speed: %.2f ns per operation\n", op_name, ns_per_op);
}

int main() {
    print_operation_speed("Integer Addition", measure_operation_speed(OP_INT_ADD));
    print_operation_speed("Integer Subtraction", measure_operation_speed(OP_INT_SUBTRACT));
    print_operation_speed("Integer Multiplication", measure_operation_speed(OP_INT_MULTIPLY));
    print_operation_speed("Integer Division", measure_operation_speed(OP_INT_DIVIDE));
    print_operation_speed("Float Addition", measure_operation_speed(OP_FLOAT_ADD));
    print_operation_speed("Float Multiplication", measure_operation_speed(OP_FLOAT_MULTIPLY));
    print_operation_speed("Float Division", measure_operation_speed(OP_FLOAT_DIVIDE));
    print_operation_speed("Sine", measure_operation_speed(OP_SINE));
    print_operation_speed("Cosine", measure_operation_speed(OP_COSINE));
    print_operation_speed("Tangent", measure_operation_speed(OP_TANGENT));
    print_operation_speed("Arctangent", measure_operation_speed(OP_ARCTANGENT));

    return EXIT_SUCCESS;
}