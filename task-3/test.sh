#!/bin/bash

# количество потоков, которые проверяем
THREADS="1 2 4 8"

echo "Read-dominated test"
for t in $THREADS; do
    echo "Threads: $t "
    
    echo "Sys:"
    for i in 1 2 3; do
        printf "1000\n100000\n0.99\n0.005\n" | ./pth_ll_rwl_sys $t | grep "Elapsed time"
    done

    echo "My:"
    for i in 1 2 3; do
        printf "1000\n100000\n0.99\n0.005\n" | ./pth_ll_rwl_my $t | grep "Elapsed time"
    done
done

echo ""
echo "Mixed test"
for t in $THREADS; do
    echo "Threads: $t "
    
    echo "Sys:"
    for i in 1 2 3; do
        printf "1000\n100000\n0.80\n0.10\n" | ./pth_ll_rwl_sys $t | grep "Elapsed time"
    done

    echo "My:"
    for i in 1 2 3; do
        printf "1000\n100000\n0.80\n0.10\n" | ./pth_ll_rwl_my $t | grep "Elapsed time"
    done
done