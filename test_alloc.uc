uint32 main() {
    alloc 5 my_var;
    my_var = 7;
    printf("alloc 5 var = %d\n", my_var);
    
    alloc 12 my_word;
    my_word = 4095;
    printf("alloc 12 var = %d\n", my_word);
    
    return 0;
}
