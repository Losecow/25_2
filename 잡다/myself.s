.data
num1:    .word 25
num2:    .word 40
num3:    .word 15

.text
.globl main
main:
    lw $s0, num1
    lw $s1, num2
    lw $s2, num3

    ble $s0, $s1, step2
    move $t0, $s0
    move $s0, $s1
    move $s1, $t0

step2:
    ble $s1, $s2, step3
    move $t0, $s1
    move $s1, $s2
    move $s2, $t0

step3:
    ble $s0, $s1, result
    move $t0, $s0
    move $s0, $s1
    move $s1, $t0

result:
    move $s5, $s1

    li $v0, 1
    move $a0, $s5
    syscall

    li $v0, 10
    syscall
