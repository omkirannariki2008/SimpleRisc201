; =============================================================================
; Test Program: Load-Use Delay Slot Optimization (Static Instruction Scheduling)
; Demonstrates hoisting independent instructions into load-use delay slots
; to eliminate 1-cycle pipeline stalls.
; =============================================================================
main:
  mov r2, 0
  mov r4, 25
  mov r7, 50
  st  r4, 0[r2]
  st  r7, 4[r2]

  ; Hazard Block 1:
  ld  r1, 0[r2]       ; Loads r1 = 25
  add r3, r1, r4      ; Consumer reads r1! (Stalls without optimization)
  mov r5, 100         ; Independent instruction! (Can be hoisted)

  ; Hazard Block 2:
  ld  r6, 4[r2]       ; Loads r6 = 50
  sub r8, r6, r4      ; Consumer reads r6! (Stalls without optimization)
  mov r9, 200         ; Independent instruction! (Can be hoisted)

  nop
