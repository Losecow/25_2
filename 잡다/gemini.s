# AI(Gemini)가 생성한 MIPS 중앙값 찾기 프로그램

.data
num1: .word 25
num2: .word 40
num3: .word 15

.text
.globl main

main:
    # 값 불러오기
    lw $s0, num1
    lw $s1, num2
    lw $s2, num3

    # 중앙값 계산 (Swap 방식)
    # 1. ($s0 > $s1) 이면 두 값을 교환
    bgt $s0, $s1, swap_s0_s1
after_swap1:

    # 2. ($s1 > $s2) 이면 두 값을 교환
    bgt $s1, $s2, swap_s1_s2
after_swap2:

    # 3. 마지막으로 ($s0 > $s1) 을 다시 확인하여 교환
    bgt $s0, $s1, swap_s0_s1_again
after_swap3:
    
    # 이 시점에서 $s1은 중앙값이 됨
    move $s5, $s1

    # 결과 출력
    li $v0, 1
    move $a0, $s5
    syscall

    # 프로그램 종료
    li $v0, 10
    syscall

# --- 값 교환(Swap)을 위한 서브 루틴 ---
swap_s0_s1:
    move $t0, $s0
    move $s0, $s1
    move $s1, $t0
    j after_swap1

swap_s1_s2:
    move $t0, $s1
    move $s1, $s2
    move $s2, $t0
    j after_swap2

swap_s0_s1_again:
    move $t0, $s0
    move $s0, $s1
    move $s1, $t0
    j after_swap3