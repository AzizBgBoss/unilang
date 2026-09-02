uint32 main() {
    uint8 val = 65;
    printf("val before = %d\n", val);
    
    MEMORY[100:8] = 99;
    val = MEMORY[100:8];
    printf("val after = %d\n", val);
    
    return 0;
}
