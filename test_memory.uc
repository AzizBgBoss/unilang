uint32 main() {
    alloc 8 buffer;
    
    MEMORY[0:8] = 65;
    uint8 val = MEMORY[0:8];
    printf("MEMORY[0:8] = %d (expect 65)\n", val);
    
    uint32 addr = 0;
    MEMORY[addr:8] = 72;
    val = MEMORY[addr:8];
    printf("MEMORY[addr:8] = %d (expect 72)\n", val);
    
    return 0;
}
