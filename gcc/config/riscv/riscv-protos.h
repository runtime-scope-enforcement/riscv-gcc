/* Definition of RISC-V target for GNU compiler.
   Copyright (C) 2011-2016 Free Software Foundation, Inc.
   Contributed by Andrew Waterman (andrew@sifive.com).
   Based on MIPS target for GNU compiler.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef GCC_RISCV_PROTOS_H
#define GCC_RISCV_PROTOS_H

/* Symbol types we understand.  The order of this list must match that of
   the unspec enum in riscv.md, subsequent to UNSPEC_ADDRESS_FIRST.  */
enum riscv_symbol_type {
  SYMBOL_ABSOLUTE,
  SYMBOL_PCREL,
  SYMBOL_GOT_DISP,
  SYMBOL_TLS,
  SYMBOL_TLS_LE,
  SYMBOL_TLS_IE,
  SYMBOL_TLS_GD
};
#define NUM_SYMBOL_TYPES (SYMBOL_TLS_GD + 1)

extern enum riscv_symbol_type riscv_classify_symbolic_expression (rtx);
extern bool riscv_symbolic_constant_p (rtx, enum riscv_symbol_type *);
extern int riscv_regno_mode_ok_for_base_p (int, enum machine_mode, bool);
extern bool riscv_hard_regno_mode_ok_p (unsigned int, enum machine_mode);
extern int riscv_address_insns (rtx, enum machine_mode, bool);
extern int riscv_const_insns (rtx);
extern int riscv_split_const_insns (rtx);
extern int riscv_load_store_insns (rtx, rtx_insn *);
extern rtx riscv_emit_move (rtx, rtx);
extern bool riscv_split_symbol (rtx, rtx, enum machine_mode, rtx *);
extern bool riscv_split_symbol_type (enum riscv_symbol_type);
extern rtx riscv_unspec_address (rtx, enum riscv_symbol_type);
extern void riscv_move_integer (rtx, rtx, HOST_WIDE_INT);
extern bool riscv_legitimize_move (enum machine_mode, rtx, rtx);
extern rtx riscv_subword (rtx, bool);
extern bool riscv_split_64bit_move_p (rtx, rtx);
extern void riscv_split_doubleword_move (rtx, rtx);
extern const char *riscv_output_move (rtx, rtx);
extern const char *riscv_output_gpr_save (unsigned);
extern const char *riscv_output_xscen_globals_prologue (void);
extern const char *riscv_output_xscen_return_delegate (void);
extern const char *riscv_output_xscen_epilogue (void);
extern const char *riscv_output_xscen_call_site_delegate (unsigned int);
extern void riscv_xscen_call_site_delegate (unsigned int uid);
extern const char *riscv_xscen_new_storage_region(rtx reg, HOST_WIDE_INT size);
extern void riscv_xscen_stack_prologue (void);
extern void riscv_xscen_new_storage_region2(rtx reg, HOST_WIDE_INT size);
extern void riscv_xscen_new_stack_storage_region(HOST_WIDE_INT size, HOST_WIDE_INT args);
extern void riscv_xscen_register_jumptable (rtx l, rtx_insn* insn);
#ifdef RTX_CODE
extern void riscv_expand_scc (rtx *);
extern void riscv_expand_conditional_branch (rtx *);
#endif
extern rtx_insn *riscv_expand_call (bool, rtx, rtx, rtx);
extern void riscv_set_return_address (rtx, rtx);
extern bool riscv_expand_block_move (rtx, rtx, rtx);
extern rtx riscv_return_addr (int, rtx);
extern HOST_WIDE_INT riscv_initial_elimination_offset (int, int);
extern void riscv_expand_prologue (void);
extern void riscv_expand_epilogue (bool);
extern bool riscv_can_use_return_insn (void);
extern rtx riscv_function_value (const_tree, const_tree, enum machine_mode, bool outgoing);
extern rtx riscv_libcall_value (enum machine_mode);
//extern rtx riscv_function_value (const_tree, const_tree, enum machine_mode);
extern unsigned int riscv_hard_regno_nregs (int, enum machine_mode);

#endif /* ! GCC_RISCV_PROTOS_H */
