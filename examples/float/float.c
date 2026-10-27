float global_single = 0.75f;
double global_precise = 1.25;
const double constants[2] = {1.5, 2.5};

double identity_double(double value)
{
    return value;
}

void main()
{
    int32_t negative = -7;
    int8_t narrow = -5;
    float single = 1.5f;
    double precise = identity_double(2.25);
    float samples[2] = {0.5f, 3.75f};
    double values[2] = {precise, 4.5};
    float stepper = 0.5f;
    double *pointer = &values[0];
    double exact = 16777217.0;
    float rounded = 16777217.0f;
    double infinity = 1.0 / 0.0;
    double not_a_number = 0.0 / 0.0;

    printf("signed: %d %d %d\n", negative / 3, negative % 3, narrow + 2);
    printf("signed-width: %d %d\n", narrow, negative >> 1);
    printf("float: %d %d %d\n", (int32_t)(single * 2.0f),
           (int32_t)(precise + single), (int32_t)(samples[1] + values[0]));
    printf("compare: %d %d %d\n", single < 2.0f,
           precise == 2.25, precise > single);

    values[0] = values[1] - 1.0;
    printf("array: %d %d\n", (int32_t)values[0], (int32_t)samples[0]);
    double total = constants[0] + global_precise + global_single;
    printf("sources: %d %d %d %d\n", (int32_t)constants[0],
           (int32_t)global_precise, (int32_t)global_single, (int32_t)total);
    *pointer = total;
    printf("storage: %d %d\n", (int32_t)*pointer, (int32_t)constants[1]);
    printf("increment: %d %d %d\n", (int32_t)++stepper,
           (int32_t)stepper++, (int32_t)stepper);
    printf("precision: %d %d\n", (int32_t)(exact - 16777216.0),
           (int32_t)(rounded - 16777216.0f));
    printf("truncate: %d %u\n", (int8_t)127.9, (uint8_t)255.9);
    printf("ieee: %d %d %d\n", infinity > 1.0, not_a_number != not_a_number,
           (int32_t)infinity);
    printf("cast: %u\n", (uint32_t)negative);
}
