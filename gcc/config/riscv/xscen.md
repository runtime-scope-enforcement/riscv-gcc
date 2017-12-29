(define_expand "xscen_stack_prologue"
  [(unspec_volatile [(const_int 0)] UNSPEC_XSCEN_STACK_PROLOGUE)]
  ""
  { riscv_xscen_stack_prologue (); DONE; }
)

(define_insn "xscen_subregion"
  [(unspec_volatile [
    (match_operand 0 "register_operand" "r")
    (match_operand 1 "register_operand" "r")
    (match_operand 2 "const_int_operand")
    ] UNSPEC_XSCEN_SUBREGION)]
  ""
  "srsub\t%0,%2(%1)"
)

(define_insn "xscen_delegate_move"
  [(unspec_volatile [
    (match_operand 0 "register_operand" "r")
    (match_operand 1 "const_int_operand")
    ] UNSPEC_XSCEN_DELEGATE_MOVE)]
  ""
  "srdlgm\t%1(%0)"
)

(define_insn "xscen_delegate"
  [(unspec_volatile [
    (match_operand 0 "register_operand" "r")
    (match_operand 1 "const_int_operand")
    ] UNSPEC_XSCEN_DELEGATE)]
  ""
  "srdlg\t%1(%0)"
)

(define_insn "xscen_epilogue"
  [(unspec_volatile [(const_int 0)] UNSPEC_XSCEN_EPILOGUE)]
  ""
  "sbxit"
)

(define_expand "xscen_call_site_delegate"
  [(unspec_volatile [(match_operand 0 "const_int_operand")] UNSPEC_XSCEN_CALL_SITE_DELEGATE)]
  ""
  { riscv_xscen_call_site_delegate (INTVAL (operands[0])); DONE; }
)

(define_insn "xscen_enter_scope"
  [(unspec_volatile [(const_int 0)] UNSPEC_XSCEN_ENTER_SCOPE)]
  ""
  "sbent"
)

(define_insn "xscen_storage_region_base"
  [(unspec_volatile [
    (match_operand 0 "register_operand" "r")
    (match_operand 1 "const_int_operand")]
     UNSPEC_XSCEN_STORAGE_REGION_BASE)]
  ""
  "srbse\t%1(%0)"
)

(define_insn "xscen_storage_region_limit"
  [(unspec_volatile [
    (match_operand 0 "register_operand" "r")
    (match_operand 1 "const_int_operand")]
     UNSPEC_XSCEN_STORAGE_REGION_LIMIT)]
  ""
  "srlmt\t%1(%0)"
)

(define_expand "xscen_new_storage_region2"
  [(unspec_volatile [
    (match_operand 0 "register_operand" "r")
    (match_operand 1 "const_int_operand")]
     UNSPEC_XSCEN_NEW_STORAGE_REGION2)]
  ""
  {
    riscv_xscen_new_storage_region2(operands[0], INTVAL(operands[1]));
    DONE;
  }
)

(define_expand "xscen_new_storage_region"
  [(unspec_volatile [
    (match_operand 0 "register_operand" "r")
    (match_operand 1 "const_int_operand")]
     UNSPEC_XSCEN_NEW_STORAGE_REGION)]
  ""
  {
  emit_insn(gen_xscen_storage_region_base(operands[0], GEN_INT(0)));
  emit_insn(gen_xscen_storage_region_limit(operands[0], operands[1]));
  DONE;
  }
)

(define_expand "xscen_new_stack_storage_region"
  [(unspec_volatile [
    (match_operand 0 "const_int_operand")
    (match_operand 1 "const_int_operand")]
     UNSPEC_XSCEN_NEW_STACK_STORAGE_REGION)]
  ""
  {
    riscv_xscen_new_stack_storage_region(INTVAL(operands[0]), INTVAL(operands[1]));
    DONE;
  }
)
