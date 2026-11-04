; =============================================================================
; Test Program 1: Full-Descending Stack Operations & Subroutine Frame
; Demonstrates push, pop, enter, leave macros expanded by StackPreprocessor
; =============================================================================
main:
  mov r1, 10
  mov r2, 20
  mov r3, 30

  ; Push multiple registers onto full-descending stack
  push r1
  push r2
  push r3

  ; Allocate local stack frame of 8 bytes
  enter 8

  ; Store local temporary variables
  mov r4, 99
  st r4, 0[sp]

  ; Call compute subroutine
  call compute_sum

  ; Deallocate local frame
  leave 8

  ; Pop registers in reverse order
  pop r3
  pop r2
  pop r1
  b end_program

compute_sum:
  push ra
  mov r5, 50
  add r0, r1, r2
  add r0, r0, r3
  pop ra
  ret

end_program:
  nop
