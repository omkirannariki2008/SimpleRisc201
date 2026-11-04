; =============================================================================
; Test Program 3: Runtime Exceptions and Division by Zero
; Demonstrates division by zero and stack boundary protections
; =============================================================================
_start:
  mov r1, 100
  mov r2, 0
  ; Division by zero test
  div r3, r1, r2      ; Triggers DIVISION_BY_ZERO runtime exception trap

  ; Stack underflow test: popping without pushing
  pop r4              ; Triggers STACK_UNDERFLOW runtime exception trap
  nop
