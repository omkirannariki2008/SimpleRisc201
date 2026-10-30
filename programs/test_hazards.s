; =============================================================================
; Test Program 2: Pipeline Data & Control Hazard Evaluation
; Demonstrates EX/MEM & MEM/WB Data Forwarding, Load-Use Stalls, and Branch Flushes
; =============================================================================
_start:
  ; 1. Back-to-back RAW dependencies (Bypass / Forwarding tests)
  mov r1, 15
  add r2, r1, 10      ; RAW on r1 (requires EX/MEM forwarding)
  sub r3, r2, r1      ; RAW on r2 and r1 (requires EX/MEM & MEM/WB forwarding)

  ; 2. Memory Load followed immediately by dependent instruction (Load-Use Hazard)
  st  r3, 100[r0]
  ld  r4, 100[r0]     ; Load from memory
  add r5, r4, 5       ; Immediate use of r4! (Requires 1-cycle hardware pipeline stall)

  ; 3. Conditional comparison and branch penalty
  cmp r5, 15
  beq branch_target   ; Branch taken -> requires pipeline squash/flush
  mov r6, 999         ; Should be flushed (never retired)

branch_target:
  mov r7, 77
  nop
