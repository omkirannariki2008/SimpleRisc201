; =============================================================================
; Test Program: Advanced Datapath Enhancements
; Demonstrates Return Address Stack (RAS), Store-to-Load Forwarding,
; and L1 Split Cache Hit Locality
; =============================================================================
main:
  mov r1, 100
  st  r1, 200[r0]      ; Store r1 to memory address 200
  ld  r2, 200[r0]      ; Store-to-Load Forwarding from Store Buffer!
  call subroutine      ; Call subroutine (Pushes PC+4 to Return Address Stack)
  mov r4, 77           ; Target returned to by RAS without flush penalty
  b finish

subroutine:
  add r3, r2, 50       ; r3 = 100 + 50 = 150
  ret                  ; Speculatively predicted by RAS (0 penalty bubbles)

finish:
  nop
