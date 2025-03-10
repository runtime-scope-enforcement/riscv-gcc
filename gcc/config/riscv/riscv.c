/* Subroutines used for code generation for RISC-V.
   Copyright (C) 2011-2014 Free Software Foundation, Inc.
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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "insn-config.h"
#include "conditions.h"
#include "insn-attr.h"
#include "recog.h"
#include "output.h"
#include "hash-set.h"
#include "machmode.h"
#include "vec.h"
#include "double-int.h"
#include "input.h"
#include "alias.h"
#include "symtab.h"
#include "wide-int.h"
#include "inchash.h"
#include "tree.h"
#include "fold-const.h"
#include "varasm.h"
#include "stringpool.h"
#include "stor-layout.h"
#include "calls.h"
#include "function.h"
#include "hashtab.h"
#include "flags.h"
#include "statistics.h"
#include "real.h"
#include "fixed-value.h"
#include "expmed.h"
#include "dojump.h"
#include "explow.h"
#include "emit-rtl.h"
#include "stmt.h"
#include "expr.h"
#include "insn-codes.h"
#include "optabs.h"
#include "libfuncs.h"
#include "reload.h"
#include "tm_p.h"
#include "ggc.h"
#include "gstab.h"
#include "hash-table.h"
#include "debug.h"
#include "target.h"
#include "target-def.h"
#include "common/common-target.h"
#include "langhooks.h"
#include "dominance.h"
#include "cfg.h"
#include "cfgrtl.h"
#include "cfganal.h"
#include "lcm.h"
#include "cfgbuild.h"
#include "cfgcleanup.h"
#include "predict.h"
#include "basic-block.h"
#include "bitmap.h"
#include "regset.h"
#include "df.h"
#include "sched-int.h"
#include "tree-ssa-alias.h"
#include "internal-fn.h"
#include "gimple-fold.h"
#include "tree-eh.h"
#include "gimple-expr.h"
#include "is-a.h"
#include "gimple.h"
#include "gimplify.h"
#include "diagnostic.h"
#include "target-globals.h"
#include "opts.h"
#include "tree-pass.h"
#include "context.h"
#include "hash-map.h"
#include "plugin-api.h"
#include "ipa-ref.h"
#include "cgraph.h"
#include "builtins.h"
#include "rtl-iter.h"
#include "gimple-pretty-print.h"
#include <stdint.h>
#include "xscen.h"

/* True if X is an UNSPEC wrapper around a SYMBOL_REF or LABEL_REF.  */
#define UNSPEC_ADDRESS_P(X)					\
  (GET_CODE (X) == UNSPEC					\
   && XINT (X, 1) >= UNSPEC_ADDRESS_FIRST			\
   && XINT (X, 1) < UNSPEC_ADDRESS_FIRST + NUM_SYMBOL_TYPES)

/* Extract the symbol or label from UNSPEC wrapper X.  */
#define UNSPEC_ADDRESS(X) \
  XVECEXP (X, 0, 0)

/* Extract the symbol type from UNSPEC wrapper X.  */
#define UNSPEC_ADDRESS_TYPE(X) \
  ((enum riscv_symbol_type) (XINT (X, 1) - UNSPEC_ADDRESS_FIRST))

/* True if bit BIT is set in VALUE.  */
#define BITSET_P(VALUE, BIT) (((VALUE) & (1ULL << (BIT))) != 0)

/* Classifies an address.

   ADDRESS_REG
       A natural register + offset address.  The register satisfies
       riscv_valid_base_register_p and the offset is a const_arith_operand.

   ADDRESS_LO_SUM
       A LO_SUM rtx.  The first operand is a valid base register and
       the second operand is a symbolic address.

   ADDRESS_CONST_INT
       A signed 16-bit constant address.

   ADDRESS_SYMBOLIC:
       A constant symbolic address.  */
enum riscv_address_type {
  ADDRESS_REG,
  ADDRESS_LO_SUM,
  ADDRESS_CONST_INT,
  ADDRESS_SYMBOLIC
};

/* Information about a function's frame layout.  */
struct GTY(())  riscv_frame_info {
  /* The size of the frame in bytes.  */
  HOST_WIDE_INT total_size;

  /* Bit X is set if the function saves or restores GPR X.  */
  unsigned int mask;

  /* Likewise FPR X.  */
  unsigned int fmask;

  /* How much the GPR save/restore routines adjust sp (or 0 if unused).  */
  unsigned save_libcall_adjustment;

  /* Offsets of fixed-point and floating-point save areas from frame bottom */
  HOST_WIDE_INT gp_sp_offset;
  HOST_WIDE_INT fp_sp_offset;

  /* Offset of virtual frame pointer from stack pointer/frame bottom */
  HOST_WIDE_INT frame_pointer_offset;

  /* Offset of hard frame pointer from stack pointer/frame bottom */
  HOST_WIDE_INT hard_frame_pointer_offset;

  /* The offset of arg_pointer_rtx from the bottom of the frame.  */
  HOST_WIDE_INT arg_pointer_offset;
};


struct GTY(()) xscen_delegation {
  unsigned int pointer_depth;
  unsigned int struct_pointers;
  unsigned int offsets[XSCEN_MAX_FIELDS];
  bool subregion;
  HOST_WIDE_INT subsize;
};

struct GTY(())  machine_function {
  /* The number of extra stack bytes taken up by register varargs.
     This area is allocated by the callee at the very top of the frame.  */
  int varargs_size;

  /* Memoized return value of leaf_function_p.  <0 if false, >0 if true.  */
  int is_leaf;

  /* The current frame information, calculated by riscv_compute_frame_info.  */
  struct riscv_frame_info frame;

  int returns_pointer;
  int return_register;
  int allocator;
  struct xscen_delegation return_fields;
  int global_count;
  tree globals[XSCEN_MAX_GLOBALS];
  HOST_WIDE_INT global_sizes[XSCEN_MAX_GLOBALS];

  int total_callees;
  tree callees[XSCEN_MAX_CALLEES];
  unsigned int callee_uids[XSCEN_MAX_CALLEES];
  struct xscen_delegation* callee_pointer_args[XSCEN_MAX_CALLEES][XSCEN_MAX_ARGS];
  unsigned int unknown_callee;

  rtx jumptables[XSCEN_MAX_SWITCHES];
  unsigned jtcount;
};

/* Information about a single argument.  */
struct riscv_arg_info {
  /* True if the argument is at least partially passed on the stack.  */
  bool stack_p;

  /* The number of integer registers allocated to this argument.  */
  unsigned int num_gprs;

  /* The offset of the first register used, provided num_gprs is nonzero.
     If passed entirely on the stack, the value is MAX_ARGS_IN_REGISTERS.  */
  unsigned int gpr_offset;

  /* The number of floating-point registers allocated to this argument.  */
  unsigned int num_fprs;

  /* The offset of the first register used, provided num_fprs is nonzero.  */
  unsigned int fpr_offset;
};

/* Information about an address described by riscv_address_type.

   ADDRESS_CONST_INT
       No fields are used.

   ADDRESS_REG
       REG is the base register and OFFSET is the constant offset.

   ADDRESS_LO_SUM
       REG and OFFSET are the operands to the LO_SUM and SYMBOL_TYPE
       is the type of symbol it references.

   ADDRESS_SYMBOLIC
       SYMBOL_TYPE is the type of symbol that the address references.  */
struct riscv_address_info {
  enum riscv_address_type type;
  rtx reg;
  rtx offset;
  enum riscv_symbol_type symbol_type;
};

/* One stage in a constant building sequence.  These sequences have
   the form:

	A = VALUE[0]
	A = A CODE[1] VALUE[1]
	A = A CODE[2] VALUE[2]
	...

   where A is an accumulator, each CODE[i] is a binary rtl operation
   and each VALUE[i] is a constant integer.  CODE[0] is undefined.  */
struct riscv_integer_op {
  enum rtx_code code;
  unsigned HOST_WIDE_INT value;
};

/* The largest number of operations needed to load an integer constant.
   The worst case is LUI, ADDI, SLLI, ADDI, SLLI, ADDI, SLLI, ADDI,
   but we may attempt and reject even worse sequences.  */
#define RISCV_MAX_INTEGER_OPS 32

/* Costs of various operations on the different architectures.  */

struct riscv_tune_info
{
  unsigned short fp_add[2];
  unsigned short fp_mul[2];
  unsigned short fp_div[2];
  unsigned short int_mul[2];
  unsigned short int_div[2];
  unsigned short issue_rate;
  unsigned short branch_cost;
  unsigned short memory_cost;
};

/* Information about one CPU we know about.  */
struct riscv_cpu_info {
  /* This CPU's canonical name.  */
  const char *name;

  /* Tuning parameters for this CPU.  */
  const struct riscv_tune_info *tune_info;
};

/* Global variables for machine-dependent things.  */

/* Which tuning parameters to use.  */
static const struct riscv_tune_info *tune_info;

/* Index R is the smallest register class that contains register R.  */
const enum reg_class riscv_regno_to_class[FIRST_PSEUDO_REGISTER] = {
  GR_REGS,	GR_REGS,	GR_REGS,	GR_REGS,
  GR_REGS,	GR_REGS,	SIBCALL_REGS,	SIBCALL_REGS,
  JALR_REGS,	JALR_REGS,	JALR_REGS,	JALR_REGS,
  JALR_REGS,	JALR_REGS,	JALR_REGS,	JALR_REGS,
  JALR_REGS,	JALR_REGS, 	JALR_REGS,	JALR_REGS,
  JALR_REGS,	JALR_REGS,	JALR_REGS,	JALR_REGS,
  JALR_REGS,	JALR_REGS,	JALR_REGS,	JALR_REGS,
  SIBCALL_REGS,	SIBCALL_REGS,	SIBCALL_REGS,	SIBCALL_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FP_REGS,	FP_REGS,	FP_REGS,	FP_REGS,
  FRAME_REGS,	FRAME_REGS,
};

/* Costs to use when optimizing for rocket.  */
static const struct riscv_tune_info rocket_tune_info = {
  {COSTS_N_INSNS (4), COSTS_N_INSNS (5)},	/* fp_add */
  {COSTS_N_INSNS (4), COSTS_N_INSNS (5)},	/* fp_mul */
  {COSTS_N_INSNS (20), COSTS_N_INSNS (20)},	/* fp_div */
  {COSTS_N_INSNS (4), COSTS_N_INSNS (4)},	/* int_mul */
  {COSTS_N_INSNS (6), COSTS_N_INSNS (6)},	/* int_div */
  1,						/* issue_rate */
  3,						/* branch_cost */
  5						/* memory_cost */
};

/* Costs to use when optimizing for size.  */
static const struct riscv_tune_info optimize_size_tune_info = {
  {COSTS_N_INSNS (1), COSTS_N_INSNS (1)},	/* fp_add */
  {COSTS_N_INSNS (1), COSTS_N_INSNS (1)},	/* fp_mul */
  {COSTS_N_INSNS (1), COSTS_N_INSNS (1)},	/* fp_div */
  {COSTS_N_INSNS (1), COSTS_N_INSNS (1)},	/* int_mul */
  {COSTS_N_INSNS (1), COSTS_N_INSNS (1)},	/* int_div */
  1,						/* issue_rate */
  1,						/* branch_cost */
  1						/* memory_cost */
};

/* A table describing all the processors GCC knows about.  */
static const struct riscv_cpu_info riscv_cpu_info_table[] = {
  { "rocket", &rocket_tune_info },
};

/* Return the riscv_cpu_info entry for the given name string.  */

static const struct riscv_cpu_info *
riscv_parse_cpu (const char *cpu_string)
{
  for (unsigned i = 0; i < ARRAY_SIZE (riscv_cpu_info_table); i++)
    if (strcmp (riscv_cpu_info_table[i].name, cpu_string) == 0)
      return riscv_cpu_info_table + i;

  error ("unknown cpu `%s' for -mtune", cpu_string);
  return riscv_cpu_info_table;
}

/* Helper function for riscv_build_integer; arguments are as for
   riscv_build_integer.  */

static int
riscv_build_integer_1 (struct riscv_integer_op *codes, HOST_WIDE_INT value,
		       enum machine_mode mode)
{
  HOST_WIDE_INT low_part = CONST_LOW_PART (value);
  int cost = INT_MAX, alt_cost;
  struct riscv_integer_op alt_codes[RISCV_MAX_INTEGER_OPS];

  if (SMALL_OPERAND (value) || LUI_OPERAND (value))
    {
      /* Simply ADDI or LUI.  */
      codes[0].code = UNKNOWN;
      codes[0].value = value;
      return 1;
    }

  /* End with ADDI.  When constructing HImode constants, do not generate any
     intermediate value that is not itself a valid HImode constant.  The
     XORI case below will handle those remaining HImode constants.  */
  if (low_part != 0
      && !(mode == HImode && (int16_t)(value - low_part) != (value - low_part)))
    {
      cost = 1 + riscv_build_integer_1 (codes, value - low_part, mode);
      codes[cost-1].code = PLUS;
      codes[cost-1].value = low_part;
    }

  /* End with XORI.  */
  if (cost > 2 && (low_part < 0 || mode == HImode))
    {
      alt_cost = 1 + riscv_build_integer_1 (alt_codes, value ^ low_part, mode);
      alt_codes[alt_cost-1].code = XOR;
      alt_codes[alt_cost-1].value = low_part;
      if (alt_cost < cost)
	cost = alt_cost, memcpy (codes, alt_codes, sizeof (alt_codes));
    }

  /* Eliminate trailing zeros and end with SLLI.  */
  if (cost > 2 && (value & 1) == 0)
    {
      int shift = 0;
      while ((value & 1) == 0)
	shift++, value >>= 1;
      alt_cost = 1 + riscv_build_integer_1 (alt_codes, value, mode);
      alt_codes[alt_cost-1].code = ASHIFT;
      alt_codes[alt_cost-1].value = shift;
      if (alt_cost < cost)
	cost = alt_cost, memcpy (codes, alt_codes, sizeof (alt_codes));
    }

  gcc_assert (cost <= RISCV_MAX_INTEGER_OPS);
  return cost;
}

/* Fill CODES with a sequence of rtl operations to load VALUE.
   Return the number of operations needed.  */

static int
riscv_build_integer (struct riscv_integer_op *codes, HOST_WIDE_INT value,
		     enum machine_mode mode)
{
  int cost = riscv_build_integer_1 (codes, value, mode);

  /* Eliminate leading zeros and end with SRLI.  */
  if (value > 0 && cost > 2)
    {
      struct riscv_integer_op alt_codes[RISCV_MAX_INTEGER_OPS];
      int alt_cost, shift = 0;
      HOST_WIDE_INT shifted_val;

      /* Try filling trailing bits with 1s.  */
      while ((value << shift) >= 0)
	shift++;
      shifted_val = (value << shift) | ((((HOST_WIDE_INT) 1) << shift) - 1);
      alt_cost = 1 + riscv_build_integer_1 (alt_codes, shifted_val, mode);
      alt_codes[alt_cost-1].code = LSHIFTRT;
      alt_codes[alt_cost-1].value = shift;
      if (alt_cost < cost)
	cost = alt_cost, memcpy (codes, alt_codes, sizeof (alt_codes));

      /* Try filling trailing bits with 0s.  */
      shifted_val = value << shift;
      alt_cost = 1 + riscv_build_integer_1 (alt_codes, shifted_val, mode);
      alt_codes[alt_cost-1].code = LSHIFTRT;
      alt_codes[alt_cost-1].value = shift;
      if (alt_cost < cost)
	cost = alt_cost, memcpy (codes, alt_codes, sizeof (alt_codes));
    }

  return cost;
}

/* Return the cost of constructing VAL in the event that a scratch
   register is available.  */

static int
riscv_split_integer_cost (HOST_WIDE_INT val)
{
  int cost;
  int32_t loval = val, hival = (val - (int32_t)val) >> 32;
  struct riscv_integer_op codes[RISCV_MAX_INTEGER_OPS];

  cost = 2 + riscv_build_integer (codes, loval, VOIDmode);
  if (loval != hival)
    cost += riscv_build_integer (codes, hival, VOIDmode);

  return cost;
}

/* Return the cost of constructing the integer constant VAL.  */

static int
riscv_integer_cost (HOST_WIDE_INT val)
{
  struct riscv_integer_op codes[RISCV_MAX_INTEGER_OPS];
  return MIN (riscv_build_integer (codes, val, VOIDmode),
	      riscv_split_integer_cost (val));
}

/* Try to split a 64b integer into 32b parts, then reassemble.  */

static rtx
riscv_split_integer (HOST_WIDE_INT val, enum machine_mode mode)
{
  int32_t loval = val, hival = (val - (int32_t)val) >> 32;
  rtx hi = gen_reg_rtx (mode), lo = gen_reg_rtx (mode);

  riscv_move_integer (hi, hi, hival);
  riscv_move_integer (lo, lo, loval);

  hi = gen_rtx_fmt_ee (ASHIFT, mode, hi, GEN_INT (32));
  hi = force_reg (mode, hi);

  return gen_rtx_fmt_ee (PLUS, mode, hi, lo);
}

/* Return true if X is a thread-local symbol.  */

static bool
riscv_tls_symbol_p (const_rtx x)
{
  return GET_CODE (x) == SYMBOL_REF && SYMBOL_REF_TLS_MODEL (x) != 0;
}

/* Return true if symbol X binds locally.  */

static bool
riscv_symbol_binds_local_p (const_rtx x)
{
  return (SYMBOL_REF_DECL (x)
	  ? targetm.binds_local_p (SYMBOL_REF_DECL (x))
	  : SYMBOL_REF_LOCAL_P (x));
}

/* Return the method that should be used to access SYMBOL_REF or
   LABEL_REF X.  */

static enum riscv_symbol_type
riscv_classify_symbol (const_rtx x)
{
  if (riscv_tls_symbol_p (x))
    return SYMBOL_TLS;

  switch (GET_CODE (x))
    {
    case LABEL_REF:
      if (LABEL_REF_NONLOCAL_P (x))
	return SYMBOL_GOT_DISP;
      break;

    case SYMBOL_REF:
      if (flag_pic && !riscv_symbol_binds_local_p (x))
	return SYMBOL_GOT_DISP;
      break;

    default:
      gcc_unreachable ();
    }

  return riscv_cmodel == CM_MEDLOW ? SYMBOL_ABSOLUTE : SYMBOL_PCREL;
}

/* Classify the base of symbolic expression X.  */

enum riscv_symbol_type
riscv_classify_symbolic_expression (rtx x)
{
  rtx offset;

  split_const (x, &x, &offset);
  if (UNSPEC_ADDRESS_P (x))
    return UNSPEC_ADDRESS_TYPE (x);

  return riscv_classify_symbol (x);
}

/* Return true if X is a symbolic constant.  If it is, store the type of
   the symbol in *SYMBOL_TYPE.  */

bool
riscv_symbolic_constant_p (rtx x, enum riscv_symbol_type *symbol_type)
{
  rtx offset;

  split_const (x, &x, &offset);
  if (UNSPEC_ADDRESS_P (x))
    {
      *symbol_type = UNSPEC_ADDRESS_TYPE (x);
      x = UNSPEC_ADDRESS (x);
    }
  else if (GET_CODE (x) == SYMBOL_REF || GET_CODE (x) == LABEL_REF)
    *symbol_type = riscv_classify_symbol (x);
  else
    return false;

  if (offset == const0_rtx)
    return true;

  /* Check whether a nonzero offset is valid for the underlying
     relocations.  */
  switch (*symbol_type)
    {
    case SYMBOL_ABSOLUTE:
    case SYMBOL_PCREL:
    case SYMBOL_TLS_LE:
      return (int32_t) INTVAL (offset) == INTVAL (offset);

    default:
      return false;
    }
  gcc_unreachable ();
}

/* Returns the number of instructions necessary to reference a symbol. */

static int riscv_symbol_insns (enum riscv_symbol_type type)
{
  switch (type)
    {
    case SYMBOL_TLS: return 0; /* Depends on the TLS model.  */
    case SYMBOL_ABSOLUTE: return 2; /* LUI + the reference.  */
    case SYMBOL_PCREL: return 2; /* AUIPC + the reference.  */
    case SYMBOL_TLS_LE: return 3; /* LUI + ADD TP + the reference.  */
    case SYMBOL_GOT_DISP: return 3; /* AUIPC + LD GOT + the reference.  */
    default: gcc_unreachable ();
    }
}

/* Implement TARGET_LEGITIMATE_CONSTANT_P.  */

static bool
riscv_legitimate_constant_p (enum machine_mode mode ATTRIBUTE_UNUSED, rtx x)
{
  return riscv_const_insns (x) > 0;
}

/* Implement TARGET_CANNOT_FORCE_CONST_MEM.  */

static bool
riscv_cannot_force_const_mem (enum machine_mode mode ATTRIBUTE_UNUSED, rtx x)
{
  enum riscv_symbol_type type;
  rtx base, offset;

  /* There is no assembler syntax for expressing an address-sized
     high part.  */
  if (GET_CODE (x) == HIGH)
    return true;

  split_const (x, &base, &offset);
  if (riscv_symbolic_constant_p (base, &type))
    {
      /* As an optimization, don't spill symbolic constants that are as
	 cheap to rematerialize as to access in the constant pool.  */
      if (SMALL_OPERAND (INTVAL (offset)) && riscv_symbol_insns (type) > 0)
	return true;

      /* As an optimization, avoid needlessly generate dynamic relocations.  */
      if (flag_pic)
	return true;
    }

  /* TLS symbols must be computed by riscv_legitimize_move.  */
  if (tls_referenced_p (x))
    return true;

  return false;
}

/* Return true if register REGNO is a valid base register for mode MODE.
   STRICT_P is true if REG_OK_STRICT is in effect.  */

int
riscv_regno_mode_ok_for_base_p (int regno,
				enum machine_mode mode ATTRIBUTE_UNUSED,
				bool strict_p)
{
  if (!HARD_REGISTER_NUM_P (regno))
    {
      if (!strict_p)
	return true;
      regno = reg_renumber[regno];
    }

  /* These fake registers will be eliminated to either the stack or
     hard frame pointer, both of which are usually valid base registers.
     Reload deals with the cases where the eliminated form isn't valid.  */
  if (regno == ARG_POINTER_REGNUM || regno == FRAME_POINTER_REGNUM)
    return true;

  return GP_REG_P (regno);
}

/* Return true if X is a valid base register for mode MODE.
   STRICT_P is true if REG_OK_STRICT is in effect.  */

static bool
riscv_valid_base_register_p (rtx x, enum machine_mode mode, bool strict_p)
{
  if (!strict_p && GET_CODE (x) == SUBREG)
    x = SUBREG_REG (x);

  return (REG_P (x)
	  && riscv_regno_mode_ok_for_base_p (REGNO (x), mode, strict_p));
}

/* Return true if, for every base register BASE_REG, (plus BASE_REG X)
   can address a value of mode MODE.  */

static bool
riscv_valid_offset_p (rtx x, enum machine_mode mode)
{
  /* Check that X is a signed 12-bit number.  */
  if (!const_arith_operand (x, Pmode))
    return false;

  /* We may need to split multiword moves, so make sure that every word
     is accessible.  */
  if (GET_MODE_SIZE (mode) > UNITS_PER_WORD
      && !SMALL_OPERAND (INTVAL (x) + GET_MODE_SIZE (mode) - UNITS_PER_WORD))
    return false;

  return true;
}

/* Should a symbol of type SYMBOL_TYPE should be split in two?  */

bool
riscv_split_symbol_type (enum riscv_symbol_type symbol_type)
{
  if (symbol_type == SYMBOL_TLS_LE)
    return true;

  if (!TARGET_EXPLICIT_RELOCS)
    return false;

  return symbol_type == SYMBOL_ABSOLUTE || symbol_type == SYMBOL_PCREL;
}

/* Return true if a LO_SUM can address a value of mode MODE when the
   LO_SUM symbol has type SYM_TYPE.  */

static bool
riscv_valid_lo_sum_p (enum riscv_symbol_type sym_type, enum machine_mode mode)
{
  /* Check that symbols of type SYMBOL_TYPE can be used to access values
     of mode MODE.  */
  if (riscv_symbol_insns (sym_type) == 0)
    return false;

  /* Check that there is a known low-part relocation.  */
  if (!riscv_split_symbol_type (sym_type))
    return false;

  /* We may need to split multiword moves, so make sure that each word
     can be accessed without inducing a carry.  */
  if (GET_MODE_SIZE (mode) > UNITS_PER_WORD
      && GET_MODE_BITSIZE (mode) > GET_MODE_ALIGNMENT (mode))
    return false;

  return true;
}

/* Return true if X is a valid address for machine mode MODE.  If it is,
   fill in INFO appropriately.  STRICT_P is true if REG_OK_STRICT is in
   effect.  */

static bool
riscv_classify_address (struct riscv_address_info *info, rtx x,
		       enum machine_mode mode, bool strict_p)
{
  switch (GET_CODE (x))
    {
    case REG:
    case SUBREG:
      info->type = ADDRESS_REG;
      info->reg = x;
      info->offset = const0_rtx;
      return riscv_valid_base_register_p (info->reg, mode, strict_p);

    case PLUS:
      info->type = ADDRESS_REG;
      info->reg = XEXP (x, 0);
      info->offset = XEXP (x, 1);
      return (riscv_valid_base_register_p (info->reg, mode, strict_p)
	      && riscv_valid_offset_p (info->offset, mode));

    case LO_SUM:
      info->type = ADDRESS_LO_SUM;
      info->reg = XEXP (x, 0);
      info->offset = XEXP (x, 1);
      /* We have to trust the creator of the LO_SUM to do something vaguely
	 sane.  Target-independent code that creates a LO_SUM should also
	 create and verify the matching HIGH.  Target-independent code that
	 adds an offset to a LO_SUM must prove that the offset will not
	 induce a carry.  Failure to do either of these things would be
	 a bug, and we are not required to check for it here.  The RISC-V
	 backend itself should only create LO_SUMs for valid symbolic
	 constants, with the high part being either a HIGH or a copy
	 of _gp. */
      info->symbol_type
	= riscv_classify_symbolic_expression (info->offset);
      return (riscv_valid_base_register_p (info->reg, mode, strict_p)
	      && riscv_valid_lo_sum_p (info->symbol_type, mode));

    case CONST_INT:
      /* Small-integer addresses don't occur very often, but they
	 are legitimate if $0 is a valid base register.  */
      info->type = ADDRESS_CONST_INT;
      return SMALL_OPERAND (INTVAL (x));

    default:
      return false;
    }
}

/* Implement TARGET_LEGITIMATE_ADDRESS_P.  */

static bool
riscv_legitimate_address_p (enum machine_mode mode, rtx x, bool strict_p)
{
  struct riscv_address_info addr;

  return riscv_classify_address (&addr, x, mode, strict_p);
}

/* Return the number of instructions needed to load or store a value
   of mode MODE at address X.  Return 0 if X isn't valid for MODE.
   Assume that multiword moves may need to be split into word moves
   if MIGHT_SPLIT_P, otherwise assume that a single load or store is
   enough. */

int
riscv_address_insns (rtx x, enum machine_mode mode, bool might_split_p)
{
  struct riscv_address_info addr;
  int n = 1;

  if (!riscv_classify_address (&addr, x, mode, false))
    return 0;

  /* BLKmode is used for single unaligned loads and stores and should
     not count as a multiword mode. */
  if (mode != BLKmode && might_split_p)
    n += (GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  if (addr.type == ADDRESS_LO_SUM)
    n += riscv_symbol_insns (addr.symbol_type) - 1;

  return n;
}

/* Return the number of instructions needed to load constant X.
   Return 0 if X isn't a valid constant.  */

int
riscv_const_insns (rtx x)
{
  enum riscv_symbol_type symbol_type;
  rtx offset;

  switch (GET_CODE (x))
    {
    case HIGH:
      if (!riscv_symbolic_constant_p (XEXP (x, 0), &symbol_type)
	  || !riscv_split_symbol_type (symbol_type))
	return 0;

      /* This is simply an LUI.  */
      return 1;

    case CONST_INT:
      {
	int cost = riscv_integer_cost (INTVAL (x));
	/* Force complicated constants to memory.  */
	return cost < 4 ? cost : 0;
      }

    case CONST_DOUBLE:
    case CONST_VECTOR:
      /* We can use x0 to load floating-point zero.  */
      return x == CONST0_RTX (GET_MODE (x)) ? 1 : 0;

    case CONST:
      /* See if we can refer to X directly.  */
      if (riscv_symbolic_constant_p (x, &symbol_type))
	return riscv_symbol_insns (symbol_type);

      /* Otherwise try splitting the constant into a base and offset.  */
      split_const (x, &x, &offset);
      if (offset != 0)
	{
	  int n = riscv_const_insns (x);
	  if (n != 0)
	    return n + riscv_integer_cost (INTVAL (offset));
	}
      return 0;

    case SYMBOL_REF:
    case LABEL_REF:
      return riscv_symbol_insns (riscv_classify_symbol (x));

    default:
      return 0;
    }
}

/* X is a doubleword constant that can be handled by splitting it into
   two words and loading each word separately.  Return the number of
   instructions required to do this.  */

int
riscv_split_const_insns (rtx x)
{
  unsigned int low, high;

  low = riscv_const_insns (riscv_subword (x, false));
  high = riscv_const_insns (riscv_subword (x, true));
  gcc_assert (low > 0 && high > 0);
  return low + high;
}

/* Return the number of instructions needed to implement INSN,
   given that it loads from or stores to MEM. */

int
riscv_load_store_insns (rtx mem, rtx_insn *insn)
{
  enum machine_mode mode;
  bool might_split_p;
  rtx set;

  gcc_assert (MEM_P (mem));
  mode = GET_MODE (mem);

  /* Try to prove that INSN does not need to be split.  */
  might_split_p = true;
  if (GET_MODE_BITSIZE (mode) <= 32)
    might_split_p = false;
  else if (GET_MODE_BITSIZE (mode) == 64)
    {
      set = single_set (insn);
      if (set && !riscv_split_64bit_move_p (SET_DEST (set), SET_SRC (set)))
	might_split_p = false;
    }

  return riscv_address_insns (XEXP (mem, 0), mode, might_split_p);
}

/* Emit a move from SRC to DEST.  Assume that the move expanders can
   handle all moves if !can_create_pseudo_p ().  The distinction is
   important because, unlike emit_move_insn, the move expanders know
   how to force Pmode objects into the constant pool even when the
   constant pool address is not itself legitimate.  */

rtx
riscv_emit_move (rtx dest, rtx src)
{
  return (can_create_pseudo_p ()
	  ? emit_move_insn (dest, src)
	  : emit_move_insn_1 (dest, src));
}

/* Emit an instruction of the form (set TARGET (CODE OP0 OP1)).  */

static void
riscv_emit_binary (enum rtx_code code, rtx target, rtx op0, rtx op1)
{
  emit_insn (gen_rtx_SET (target,
			  gen_rtx_fmt_ee (code, GET_MODE (target), op0, op1)));
}

/* Compute (CODE OP0 OP1) and store the result in a new register
   of mode MODE.  Return that new register.  */

static rtx
riscv_force_binary (enum machine_mode mode, enum rtx_code code, rtx op0, rtx op1)
{
  rtx reg;

  reg = gen_reg_rtx (mode);
  riscv_emit_binary (code, reg, op0, op1);
  return reg;
}

/* Copy VALUE to a register and return that register.  If new pseudos
   are allowed, copy it into a new register, otherwise use DEST.  */

static rtx
riscv_force_temporary (rtx dest, rtx value)
{
  if (can_create_pseudo_p ())
    return force_reg (Pmode, value);
  else
    {
      riscv_emit_move (dest, value);
      return dest;
    }
}

/* Wrap symbol or label BASE in an UNSPEC address of type SYMBOL_TYPE,
   then add CONST_INT OFFSET to the result.  */

static rtx
riscv_unspec_address_offset (rtx base, rtx offset,
			     enum riscv_symbol_type symbol_type)
{
  base = gen_rtx_UNSPEC (Pmode, gen_rtvec (1, base),
			 UNSPEC_ADDRESS_FIRST + symbol_type);
  if (offset != const0_rtx)
    base = gen_rtx_PLUS (Pmode, base, offset);
  return gen_rtx_CONST (Pmode, base);
}

/* Return an UNSPEC address with underlying address ADDRESS and symbol
   type SYMBOL_TYPE.  */

rtx
riscv_unspec_address (rtx address, enum riscv_symbol_type symbol_type)
{
  rtx base, offset;

  split_const (address, &base, &offset);
  return riscv_unspec_address_offset (base, offset, symbol_type);
}

/* If OP is an UNSPEC address, return the address to which it refers,
   otherwise return OP itself.  */

static rtx
riscv_strip_unspec_address (rtx op)
{
  rtx base, offset;

  split_const (op, &base, &offset);
  if (UNSPEC_ADDRESS_P (base))
    op = plus_constant (Pmode, UNSPEC_ADDRESS (base), INTVAL (offset));
  return op;
}

/* If riscv_unspec_address (ADDR, SYMBOL_TYPE) is a 32-bit value, add the
   high part to BASE and return the result.  Just return BASE otherwise.
   TEMP is as for riscv_force_temporary.

   The returned expression can be used as the first operand to a LO_SUM.  */

static rtx
riscv_unspec_offset_high (rtx temp, rtx addr, enum riscv_symbol_type symbol_type)
{
  addr = gen_rtx_HIGH (Pmode, riscv_unspec_address (addr, symbol_type));
  return riscv_force_temporary (temp, addr);
}

/* Load an entry from the GOT for a TLS GD access.  */

static rtx riscv_got_load_tls_gd (rtx dest, rtx sym)
{
  if (Pmode == DImode)
    return gen_got_load_tls_gddi (dest, sym);
  else
    return gen_got_load_tls_gdsi (dest, sym);
}

/* Load an entry from the GOT for a TLS IE access.  */

static rtx riscv_got_load_tls_ie (rtx dest, rtx sym)
{
  if (Pmode == DImode)
    return gen_got_load_tls_iedi (dest, sym);
  else
    return gen_got_load_tls_iesi (dest, sym);
}

/* Add in the thread pointer for a TLS LE access.  */

static rtx riscv_tls_add_tp_le (rtx dest, rtx base, rtx sym)
{
  rtx tp = gen_rtx_REG (Pmode, THREAD_POINTER_REGNUM);
  if (Pmode == DImode)
    return gen_tls_add_tp_ledi (dest, base, tp, sym);
  else
    return gen_tls_add_tp_lesi (dest, base, tp, sym);
}

/* If MODE is MAX_MACHINE_MODE, ADDR appears as a move operand, otherwise
   it appears in a MEM of that mode.  Return true if ADDR is a legitimate
   constant in that context and can be split into high and low parts.
   If so, and if LOW_OUT is nonnull, emit the high part and store the
   low part in *LOW_OUT.  Leave *LOW_OUT unchanged otherwise.

   TEMP is as for riscv_force_temporary and is used to load the high
   part into a register.

   When MODE is MAX_MACHINE_MODE, the low part is guaranteed to be
   a legitimize SET_SRC for an .md pattern, otherwise the low part
   is guaranteed to be a legitimate address for mode MODE.  */

bool
riscv_split_symbol (rtx temp, rtx addr, enum machine_mode mode, rtx *low_out)
{
  enum riscv_symbol_type symbol_type;

  if ((GET_CODE (addr) == HIGH && mode == MAX_MACHINE_MODE)
      || !riscv_symbolic_constant_p (addr, &symbol_type)
      || riscv_symbol_insns (symbol_type) == 0
      || !riscv_split_symbol_type (symbol_type))
    return false;

  if (low_out)
    switch (symbol_type)
      {
      case SYMBOL_ABSOLUTE:
	{
	  rtx high = gen_rtx_HIGH (Pmode, copy_rtx (addr));
	  high = riscv_force_temporary (temp, high);
	  *low_out = gen_rtx_LO_SUM (Pmode, high, addr);
	}
	break;

      case SYMBOL_PCREL:
	{
	  static unsigned seqno;
	  char buf[32];
	  rtx label;

	  ssize_t bytes = snprintf (buf, sizeof (buf), ".LA%u", seqno);
	  gcc_assert ((size_t) bytes < sizeof (buf));

	  label = gen_rtx_SYMBOL_REF (Pmode, ggc_strdup (buf));
	  SYMBOL_REF_FLAGS (label) |= SYMBOL_FLAG_LOCAL;

	  if (temp == NULL)
	    temp = gen_reg_rtx (Pmode);

	  if (Pmode == DImode)
	    emit_insn (gen_auipcdi (temp, copy_rtx (addr), GEN_INT (seqno)));
	  else
	    emit_insn (gen_auipcsi (temp, copy_rtx (addr), GEN_INT (seqno)));

	  *low_out = gen_rtx_LO_SUM (Pmode, temp, label);

	  seqno++;
	}
	break;

      default:
	gcc_unreachable ();
      }

  return true;
}

/* Return a legitimate address for REG + OFFSET.  TEMP is as for
   riscv_force_temporary; it is only needed when OFFSET is not a
   SMALL_OPERAND.  */

static rtx
riscv_add_offset (rtx temp, rtx reg, HOST_WIDE_INT offset)
{
  if (!SMALL_OPERAND (offset))
    {
      rtx high;

      /* Leave OFFSET as a 16-bit offset and put the excess in HIGH.
	 The addition inside the macro CONST_HIGH_PART may cause an
	 overflow, so we need to force a sign-extension check.  */
      high = gen_int_mode (CONST_HIGH_PART (offset), Pmode);
      offset = CONST_LOW_PART (offset);
      high = riscv_force_temporary (temp, high);
      reg = riscv_force_temporary (temp, gen_rtx_PLUS (Pmode, high, reg));
    }
  return plus_constant (Pmode, reg, offset);
}

/* The __tls_get_attr symbol.  */
static GTY(()) rtx riscv_tls_symbol;

/* Return an instruction sequence that calls __tls_get_addr.  SYM is
   the TLS symbol we are referencing and TYPE is the symbol type to use
   (either global dynamic or local dynamic).  RESULT is an RTX for the
   return value location.  */

static rtx_insn *
riscv_call_tls_get_addr (rtx sym, rtx result)
{
  rtx a0 = gen_rtx_REG (Pmode, GP_ARG_FIRST);
  rtx_insn *insn;

  if (!riscv_tls_symbol)
    riscv_tls_symbol = init_one_libfunc ("__tls_get_addr");

  start_sequence ();

  emit_insn (riscv_got_load_tls_gd (a0, sym));
  insn = riscv_expand_call (false, result, riscv_tls_symbol, const0_rtx);
  RTL_CONST_CALL_P (insn) = 1;
  use_reg (&CALL_INSN_FUNCTION_USAGE (insn), a0);
  insn = get_insns ();

  end_sequence ();

  return insn;
}

/* Generate the code to access LOC, a thread-local SYMBOL_REF, and return
   its address.  The return value will be both a valid address and a valid
   SET_SRC (either a REG or a LO_SUM).  */

static rtx
riscv_legitimize_tls_address (rtx loc)
{
  rtx dest, tp, tmp;
  enum tls_model model = SYMBOL_REF_TLS_MODEL (loc);

  /* Since we support TLS copy relocs, non-PIC TLS accesses may all use LE.  */
  if (!flag_pic)
    model = TLS_MODEL_LOCAL_EXEC;

  switch (model)
    {
    case TLS_MODEL_LOCAL_DYNAMIC:
      /* Rely on section anchors for the optimization that LDM TLS
	 provides.  The anchor's address is loaded with GD TLS. */
    case TLS_MODEL_GLOBAL_DYNAMIC:
      tmp = gen_rtx_REG (Pmode, GP_RETURN);
      dest = gen_reg_rtx (Pmode);
      emit_libcall_block (riscv_call_tls_get_addr (loc, tmp), dest, tmp, loc);
      break;

    case TLS_MODEL_INITIAL_EXEC:
      /* la.tls.ie; tp-relative add */
      tp = gen_rtx_REG (Pmode, THREAD_POINTER_REGNUM);
      tmp = gen_reg_rtx (Pmode);
      emit_insn (riscv_got_load_tls_ie (tmp, loc));
      dest = gen_reg_rtx (Pmode);
      emit_insn (gen_add3_insn (dest, tmp, tp));
      break;

    case TLS_MODEL_LOCAL_EXEC:
      tmp = riscv_unspec_offset_high (NULL, loc, SYMBOL_TLS_LE);
      dest = gen_reg_rtx (Pmode);
      emit_insn (riscv_tls_add_tp_le (dest, tmp, loc));
      dest = gen_rtx_LO_SUM (Pmode, dest,
			     riscv_unspec_address (loc, SYMBOL_TLS_LE));
      break;

    default:
      gcc_unreachable ();
    }
  return dest;
}

/* If X is not a valid address for mode MODE, force it into a register.  */

static rtx
riscv_force_address (rtx x, enum machine_mode mode)
{
  if (!riscv_legitimate_address_p (mode, x, false))
    x = force_reg (Pmode, x);
  return x;
}

/* This function is used to implement LEGITIMIZE_ADDRESS.  If X can
   be legitimized in a way that the generic machinery might not expect,
   return a new address, otherwise return NULL.  MODE is the mode of
   the memory being accessed.  */

static rtx
riscv_legitimize_address (rtx x, rtx oldx ATTRIBUTE_UNUSED,
			 enum machine_mode mode)
{
  rtx addr;

  if (riscv_tls_symbol_p (x))
    return riscv_legitimize_tls_address (x);

  /* See if the address can split into a high part and a LO_SUM.  */
  if (riscv_split_symbol (NULL, x, mode, &addr))
    return riscv_force_address (addr, mode);

  /* Handle BASE + OFFSET using riscv_add_offset.  */
  if (GET_CODE (x) == PLUS && CONST_INT_P (XEXP (x, 1))
      && INTVAL (XEXP (x, 1)) != 0)
    {
      rtx base = XEXP (x, 0);
      HOST_WIDE_INT offset = INTVAL (XEXP (x, 1));

      if (!riscv_valid_base_register_p (base, mode, false))
	base = copy_to_mode_reg (Pmode, base);
      addr = riscv_add_offset (NULL, base, offset);
      return riscv_force_address (addr, mode);
    }

  return x;
}

/* Load VALUE into DEST.  TEMP is as for riscv_force_temporary.  */

void
riscv_move_integer (rtx temp, rtx dest, HOST_WIDE_INT value)
{
  struct riscv_integer_op codes[RISCV_MAX_INTEGER_OPS];
  enum machine_mode mode;
  int i, num_ops;
  rtx x;

  mode = GET_MODE (dest);
  num_ops = riscv_build_integer (codes, value, mode);

  if (can_create_pseudo_p () && num_ops > 2 /* not a simple constant */
      && num_ops >= riscv_split_integer_cost (value))
    x = riscv_split_integer (value, mode);
  else
    {
      /* Apply each binary operation to X. */
      x = GEN_INT (codes[0].value);

      for (i = 1; i < num_ops; i++)
	{
	  if (!can_create_pseudo_p ())
	    {
	      emit_insn (gen_rtx_SET (temp, x));
	      x = temp;
	    }
	  else
	    x = force_reg (mode, x);

	  x = gen_rtx_fmt_ee (codes[i].code, mode, x, GEN_INT (codes[i].value));
	}
    }

  emit_insn (gen_rtx_SET (dest, x));
}

/* Subroutine of riscv_legitimize_move.  Move constant SRC into register
   DEST given that SRC satisfies immediate_operand but doesn't satisfy
   move_operand.  */

static void
riscv_legitimize_const_move (enum machine_mode mode, rtx dest, rtx src)
{
  rtx base, offset;

  /* Split moves of big integers into smaller pieces.  */
  if (splittable_const_int_operand (src, mode))
    {
      riscv_move_integer (dest, dest, INTVAL (src));
      return;
    }

  /* Split moves of symbolic constants into high/low pairs.  */
  if (riscv_split_symbol (dest, src, MAX_MACHINE_MODE, &src))
    {
      emit_insn (gen_rtx_SET (dest, src));
      return;
    }

  /* Generate the appropriate access sequences for TLS symbols.  */
  if (riscv_tls_symbol_p (src))
    {
      riscv_emit_move (dest, riscv_legitimize_tls_address (src));
      return;
    }

  /* If we have (const (plus symbol offset)), and that expression cannot
     be forced into memory, load the symbol first and add in the offset.  Also
     prefer to do this even if the constant _can_ be forced into memory, as it
     usually produces better code.  */
  split_const (src, &base, &offset);
  if (offset != const0_rtx
      && (targetm.cannot_force_const_mem (mode, src) || can_create_pseudo_p ()))
    {
      base = riscv_force_temporary (dest, base);
      riscv_emit_move (dest, riscv_add_offset (NULL, base, INTVAL (offset)));
      return;
    }

  src = force_const_mem (mode, src);

  /* When using explicit relocs, constant pool references are sometimes
     not legitimate addresses.  */
  riscv_split_symbol (dest, XEXP (src, 0), mode, &XEXP (src, 0));
  riscv_emit_move (dest, src);
}

/* If (set DEST SRC) is not a valid move instruction, emit an equivalent
   sequence that is valid.  */

bool
riscv_legitimize_move (enum machine_mode mode, rtx dest, rtx src)
{
  if (!register_operand (dest, mode) && !reg_or_0_operand (src, mode))
    {
      riscv_emit_move (dest, force_reg (mode, src));
      return true;
    }

  /* We need to deal with constants that would be legitimate
     immediate_operands but aren't legitimate move_operands.  */
  if (CONSTANT_P (src) && !move_operand (src, mode))
    {
      riscv_legitimize_const_move (mode, dest, src);
      set_unique_reg_note (get_last_insn (), REG_EQUAL, copy_rtx (src));
      return true;
    }

  return false;
}

/* Return true if there is an instruction that implements CODE and accepts
   X as an immediate operand. */

static int
riscv_immediate_operand_p (int code, HOST_WIDE_INT x)
{
  switch (code)
    {
    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      /* All shift counts are truncated to a valid constant.  */
      return true;

    case AND:
    case IOR:
    case XOR:
    case PLUS:
    case LT:
    case LTU:
      /* These instructions take 12-bit signed immediates.  */
      return SMALL_OPERAND (x);

    case LE:
      /* We add 1 to the immediate and use SLT.  */
      return SMALL_OPERAND (x + 1);

    case LEU:
      /* Likewise SLTU, but reject the always-true case.  */
      return SMALL_OPERAND (x + 1) && x + 1 != 0;

    case GE:
    case GEU:
      /* We can emulate an immediate of 1 by using GT/GTU against x0.  */
      return x == 1;

    default:
      /* By default assume that x0 can be used for 0.  */
      return x == 0;
    }
}

/* Return the cost of binary operation X, given that the instruction
   sequence for a word-sized or smaller operation takes SIGNLE_INSNS
   instructions and that the sequence of a double-word operation takes
   DOUBLE_INSNS instructions.  */

static int
riscv_binary_cost (rtx x, int single_insns, int double_insns)
{
  if (GET_MODE_SIZE (GET_MODE (x)) == UNITS_PER_WORD * 2)
    return COSTS_N_INSNS (double_insns);
  return COSTS_N_INSNS (single_insns);
}

/* Return the cost of sign-extending OP to mode MODE, not including the
   cost of OP itself.  */

static int
riscv_sign_extend_cost (enum machine_mode mode, rtx op)
{
  if (MEM_P (op))
    /* Extended loads are as cheap as unextended ones.  */
    return 0;

  if (TARGET_64BIT && mode == DImode && GET_MODE (op) == SImode)
    /* A sign extension from SImode to DImode in 64-bit mode is free.  */
    return 0;

  /* We need to use a shift left and a shift right.  */
  return COSTS_N_INSNS (2);
}

/* Return the cost of zero-extending OP to mode MODE, not including the
   cost of OP itself.  */

static int
riscv_zero_extend_cost (enum machine_mode mode, rtx op)
{
  if (MEM_P (op))
    /* Extended loads are as cheap as unextended ones.  */
    return 0;

  if ((TARGET_64BIT && mode == DImode && GET_MODE (op) == SImode) ||
      ((mode == DImode || mode == SImode) && GET_MODE (op) == HImode))
    /* We need a shift left by 32 bits and a shift right by 32 bits.  */
    return COSTS_N_INSNS (2);

  /* We can use ANDI.  */
  return COSTS_N_INSNS (1);
}

/* Implement TARGET_RTX_COSTS.  */

static bool
riscv_rtx_costs (rtx x, machine_mode mode, int outer_code, int opno ATTRIBUTE_UNUSED,
		 int *total, bool speed)
{
  bool float_mode_p = FLOAT_MODE_P (mode);
  int cost;

  switch (GET_CODE (x))
    {
    case CONST_INT:
      if (riscv_immediate_operand_p (outer_code, INTVAL (x)))
	{
	  *total = 0;
	  return true;
	}
      /* Fall through.  */

    case SYMBOL_REF:
    case LABEL_REF:
    case CONST_DOUBLE:
    case CONST:
      if (speed)
	*total = 1;
      else if ((cost = riscv_const_insns (x)) > 0)
	*total = COSTS_N_INSNS (cost);
      else /* The instruction will be fetched from the constant pool.  */
	*total = COSTS_N_INSNS (riscv_symbol_insns (SYMBOL_ABSOLUTE));
      return true;

    case MEM:
      /* If the address is legitimate, return the number of
	 instructions it needs.  */
      if ((cost = riscv_address_insns (XEXP (x, 0), mode, true)) > 0)
	{
	  *total = COSTS_N_INSNS (cost + tune_info->memory_cost);
	  return true;
	}
      /* Otherwise use the default handling.  */
      return false;

    case NOT:
      *total = COSTS_N_INSNS (GET_MODE_SIZE (mode) > UNITS_PER_WORD ? 2 : 1);
      return false;

    case AND:
    case IOR:
    case XOR:
      /* Double-word operations use two single-word operations.  */
      *total = riscv_binary_cost (x, 1, 2);
      return false;

    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      *total = riscv_binary_cost (x, 1, CONSTANT_P (XEXP (x, 1)) ? 4 : 9);
      return false;

    case ABS:
      *total = COSTS_N_INSNS (float_mode_p ? 1 : 3);
      return false;

    case LO_SUM:
      *total = set_src_cost (XEXP (x, 0), mode, speed);
      return true;

    case LT:
    case LTU:
    case LE:
    case LEU:
    case GT:
    case GTU:
    case GE:
    case GEU:
    case EQ:
    case NE:
    case UNORDERED:
    case LTGT:
      /* Branch comparisons have VOIDmode, so use the first operand's
	 mode instead.  */
      mode = GET_MODE (XEXP (x, 0));
      if (float_mode_p)
	*total = tune_info->fp_add[mode == DFmode];
      else
	*total = riscv_binary_cost (x, 1, 3);
      return false;

    case MINUS:
      if (float_mode_p
	  && !HONOR_NANS (mode)
	  && !HONOR_SIGNED_ZEROS (mode))
	{
	  /* See if we can use NMADD or NMSUB.  See riscv.md for the
	     associated patterns.  */
	  rtx op0 = XEXP (x, 0);
	  rtx op1 = XEXP (x, 1);
	  if (GET_CODE (op0) == MULT && GET_CODE (XEXP (op0, 0)) == NEG)
	    {
	      *total = (tune_info->fp_mul[mode == DFmode]
			+ set_src_cost (XEXP (XEXP (op0, 0), 0), mode, speed)
			+ set_src_cost (XEXP (op0, 1), mode, speed)
			+ set_src_cost (op1, mode, speed));
	      return true;
	    }
	  if (GET_CODE (op1) == MULT)
	    {
	      *total = (tune_info->fp_mul[mode == DFmode]
			+ set_src_cost (op0, mode, speed)
			+ set_src_cost (XEXP (op1, 0), mode, speed)
			+ set_src_cost (XEXP (op1, 1), mode, speed));
	      return true;
	    }
	}
      /* Fall through.  */

    case PLUS:
      if (float_mode_p)
	*total = tune_info->fp_add[mode == DFmode];
      else
	*total = riscv_binary_cost (x, 1, 4);
      return false;

    case NEG:
      if (float_mode_p
	  && !HONOR_NANS (mode)
	  && HONOR_SIGNED_ZEROS (mode))
	{
	  /* See if we can use NMADD or NMSUB.  See riscv.md for the
	     associated patterns.  */
	  rtx op = XEXP (x, 0);
	  if ((GET_CODE (op) == PLUS || GET_CODE (op) == MINUS)
	      && GET_CODE (XEXP (op, 0)) == MULT)
	    {
	      *total = (tune_info->fp_mul[mode == DFmode]
			+ set_src_cost (XEXP (XEXP (op, 0), 0), mode, speed)
			+ set_src_cost (XEXP (XEXP (op, 0), 1), mode, speed)
			+ set_src_cost (XEXP (op, 1), mode, speed));
	      return true;
	    }
	}

      if (float_mode_p)
	*total = tune_info->fp_add[mode == DFmode];
      else
	*total = COSTS_N_INSNS (GET_MODE_SIZE (mode) > UNITS_PER_WORD ? 4 : 1);
      return false;

    case MULT:
      if (float_mode_p)
	*total = tune_info->fp_mul[mode == DFmode];
      else if (GET_MODE_SIZE (mode) > UNITS_PER_WORD)
	*total = 3 * tune_info->int_mul[0] + COSTS_N_INSNS (2);
      else if (!speed)
	*total = COSTS_N_INSNS (1);
      else
	*total = tune_info->int_mul[mode == DImode];
      return false;

    case DIV:
    case SQRT:
    case MOD:
      if (float_mode_p)
	{
	  *total = tune_info->fp_div[mode == DFmode];
	  return false;
	}
      /* Fall through.  */

    case UDIV:
    case UMOD:
      if (speed)
	*total = tune_info->int_div[mode == DImode];
      else
	*total = COSTS_N_INSNS (1);
      return false;

    case SIGN_EXTEND:
      *total = riscv_sign_extend_cost (mode, XEXP (x, 0));
      return false;

    case ZERO_EXTEND:
      *total = riscv_zero_extend_cost (mode, XEXP (x, 0));
      return false;

    case FLOAT:
    case UNSIGNED_FLOAT:
    case FIX:
    case FLOAT_EXTEND:
    case FLOAT_TRUNCATE:
      *total = tune_info->fp_add[mode == DFmode];
      return false;

    case UNSPEC:
      if (XINT (x, 1) == UNSPEC_AUIPC)
	{
	  /* Make AUIPC cheap to avoid spilling its result to the stack.  */
	  *total = 1;
	  return true;
	}
      return false;

    default:
      return false;
    }
}

/* Implement TARGET_ADDRESS_COST.  */

static int
riscv_address_cost (rtx addr, enum machine_mode mode,
		    addr_space_t as ATTRIBUTE_UNUSED,
		    bool speed ATTRIBUTE_UNUSED)
{
  return riscv_address_insns (addr, mode, false);
}

/* Return one word of double-word value OP.  HIGH_P is true to select the
   high part or false to select the low part. */

rtx
riscv_subword (rtx op, bool high_p)
{
  unsigned int byte = high_p ? UNITS_PER_WORD : 0;
  enum machine_mode mode = GET_MODE (op);

  if (mode == VOIDmode)
    mode = TARGET_64BIT ? TImode : DImode;

  if (MEM_P (op))
    return adjust_address (op, word_mode, byte);

  if (REG_P (op))
    gcc_assert (!FP_REG_RTX_P (op));

  return simplify_gen_subreg (word_mode, op, mode, byte);
}

/* Return true if a 64-bit move from SRC to DEST should be split into two.  */

bool
riscv_split_64bit_move_p (rtx dest, rtx src)
{
  if (TARGET_64BIT)
    return false;

  /* Allow FPR <-> FPR and FPR <-> MEM moves, and permit the special case
     of zeroing an FPR with FCVT.D.W.  */
  if (TARGET_DOUBLE_FLOAT
      && ((FP_REG_RTX_P (src) && FP_REG_RTX_P (dest))
	  || (FP_REG_RTX_P (dest) && MEM_P (src))
	  || (FP_REG_RTX_P (src) && MEM_P (dest))
	  || (FP_REG_RTX_P (dest) && src == CONST0_RTX (GET_MODE (src)))))
    return false;

  return true;
}

/* Split a doubleword move from SRC to DEST.  On 32-bit targets,
   this function handles 64-bit moves for which riscv_split_64bit_move_p
   holds.  For 64-bit targets, this function handles 128-bit moves.  */

void
riscv_split_doubleword_move (rtx dest, rtx src)
{
  rtx low_dest;

   /* The operation can be split into two normal moves.  Decide in
      which order to do them.  */
   low_dest = riscv_subword (dest, false);
   if (REG_P (low_dest) && reg_overlap_mentioned_p (low_dest, src))
     {
       riscv_emit_move (riscv_subword (dest, true), riscv_subword (src, true));
       riscv_emit_move (low_dest, riscv_subword (src, false));
     }
   else
     {
       riscv_emit_move (low_dest, riscv_subword (src, false));
       riscv_emit_move (riscv_subword (dest, true), riscv_subword (src, true));
     }
}

/* Return the appropriate instructions to move SRC into DEST.  Assume
   that SRC is operand 1 and DEST is operand 0.  */

const char *
riscv_output_move (rtx dest, rtx src)
{
  enum rtx_code dest_code, src_code;
  enum machine_mode mode;
  bool dbl_p;

  dest_code = GET_CODE (dest);
  src_code = GET_CODE (src);
  mode = GET_MODE (dest);
  dbl_p = (GET_MODE_SIZE (mode) == 8);

  if (dbl_p && riscv_split_64bit_move_p (dest, src))
    return "#";

  if (dest_code == REG && GP_REG_P (REGNO (dest)))
    {
      if (src_code == REG && FP_REG_P (REGNO (src)))
	return dbl_p ? "fmv.x.d\t%0,%1" : "fmv.x.s\t%0,%1";

      if (src_code == MEM)
	switch (GET_MODE_SIZE (mode))
	  {
	  case 1: return "lbu\t%0,%1";
	  case 2: return "lhu\t%0,%1";
	  case 4: return "lw\t%0,%1";
	  case 8: return "ld\t%0,%1";
	  }

      if (src_code == CONST_INT)
	return "li\t%0,%1";

      if (src_code == HIGH)
	return "lui\t%0,%h1";

      if (symbolic_operand (src, VOIDmode))
	switch (riscv_classify_symbolic_expression (src))
	  {
	  case SYMBOL_GOT_DISP: return "la\t%0,%1";
	  case SYMBOL_ABSOLUTE: return "lla\t%0,%1";
	  case SYMBOL_PCREL: return "lla\t%0,%1";
	  default: gcc_unreachable ();
	  }
    }
  if ((src_code == REG && GP_REG_P (REGNO (src)))
      || (src == CONST0_RTX (mode)))
    {
      if (dest_code == REG)
	{
	  if (GP_REG_P (REGNO (dest)))
	    return "mv\t%0,%z1";

	  if (FP_REG_P (REGNO (dest)))
	    {
	      if (!dbl_p)
		return "fmv.s.x\t%0,%z1";
	      if (TARGET_64BIT)
		return "fmv.d.x\t%0,%z1";
	      /* in RV32, we can emulate fmv.d.x %0, x0 using fcvt.d.w */
	      gcc_assert (src == CONST0_RTX (mode));
	      return "fcvt.d.w\t%0,x0";
	    }
	}
      if (dest_code == MEM)
	switch (GET_MODE_SIZE (mode))
	  {
	  case 1: return "sb\t%z1,%0";
	  case 2: return "sh\t%z1,%0";
	  case 4: return "sw\t%z1,%0";
	  case 8: return "sd\t%z1,%0";
	  }
    }
  if (src_code == REG && FP_REG_P (REGNO (src)))
    {
      if (dest_code == REG && FP_REG_P (REGNO (dest)))
	return dbl_p ? "fmv.d\t%0,%1" : "fmv.s\t%0,%1";

      if (dest_code == MEM)
	return dbl_p ? "fsd\t%1,%0" : "fsw\t%1,%0";
    }
  if (dest_code == REG && FP_REG_P (REGNO (dest)))
    {
      if (src_code == MEM)
	return dbl_p ? "fld\t%0,%1" : "flw\t%0,%1";
    }
  gcc_unreachable ();
}

/* Return true if CMP1 is a suitable second operand for integer ordering
   test CODE.  See also the *sCC patterns in riscv.md.  */

static bool
riscv_int_order_operand_ok_p (enum rtx_code code, rtx cmp1)
{
  switch (code)
    {
    case GT:
    case GTU:
      return reg_or_0_operand (cmp1, VOIDmode);

    case GE:
    case GEU:
      return cmp1 == const1_rtx;

    case LT:
    case LTU:
      return arith_operand (cmp1, VOIDmode);

    case LE:
      return sle_operand (cmp1, VOIDmode);

    case LEU:
      return sleu_operand (cmp1, VOIDmode);

    default:
      gcc_unreachable ();
    }
}

/* Return true if *CMP1 (of mode MODE) is a valid second operand for
   integer ordering test *CODE, or if an equivalent combination can
   be formed by adjusting *CODE and *CMP1.  When returning true, update
   *CODE and *CMP1 with the chosen code and operand, otherwise leave
   them alone.  */

static bool
riscv_canonicalize_int_order_test (enum rtx_code *code, rtx *cmp1,
				  enum machine_mode mode)
{
  HOST_WIDE_INT plus_one;

  if (riscv_int_order_operand_ok_p (*code, *cmp1))
    return true;

  if (CONST_INT_P (*cmp1))
    switch (*code)
      {
      case LE:
	plus_one = trunc_int_for_mode (UINTVAL (*cmp1) + 1, mode);
	if (INTVAL (*cmp1) < plus_one)
	  {
	    *code = LT;
	    *cmp1 = force_reg (mode, GEN_INT (plus_one));
	    return true;
	  }
	break;

      case LEU:
	plus_one = trunc_int_for_mode (UINTVAL (*cmp1) + 1, mode);
	if (plus_one != 0)
	  {
	    *code = LTU;
	    *cmp1 = force_reg (mode, GEN_INT (plus_one));
	    return true;
	  }
	break;

      default:
	break;
      }
  return false;
}

/* Compare CMP0 and CMP1 using ordering test CODE and store the result
   in TARGET.  CMP0 and TARGET are register_operands.  If INVERT_PTR
   is nonnull, it's OK to set TARGET to the inverse of the result and
   flip *INVERT_PTR instead.  */

static void
riscv_emit_int_order_test (enum rtx_code code, bool *invert_ptr,
			  rtx target, rtx cmp0, rtx cmp1)
{
  enum machine_mode mode;

  /* First see if there is a RISCV instruction that can do this operation.
     If not, try doing the same for the inverse operation.  If that also
     fails, force CMP1 into a register and try again.  */
  mode = GET_MODE (cmp0);
  if (riscv_canonicalize_int_order_test (&code, &cmp1, mode))
    riscv_emit_binary (code, target, cmp0, cmp1);
  else
    {
      enum rtx_code inv_code = reverse_condition (code);
      if (!riscv_canonicalize_int_order_test (&inv_code, &cmp1, mode))
	{
	  cmp1 = force_reg (mode, cmp1);
	  riscv_emit_int_order_test (code, invert_ptr, target, cmp0, cmp1);
	}
      else if (invert_ptr == 0)
	{
	  rtx inv_target;

	  inv_target = riscv_force_binary (GET_MODE (target),
					  inv_code, cmp0, cmp1);
	  riscv_emit_binary (XOR, target, inv_target, const1_rtx);
	}
      else
	{
	  *invert_ptr = !*invert_ptr;
	  riscv_emit_binary (inv_code, target, cmp0, cmp1);
	}
    }
}

/* Return a register that is zero iff CMP0 and CMP1 are equal.
   The register will have the same mode as CMP0.  */

static rtx
riscv_zero_if_equal (rtx cmp0, rtx cmp1)
{
  if (cmp1 == const0_rtx)
    return cmp0;

  return expand_binop (GET_MODE (cmp0), sub_optab,
		       cmp0, cmp1, 0, 0, OPTAB_DIRECT);
}

/* Convert a comparison into something that can be used in a branch or
   conditional move.  On entry, *OP0 and *OP1 are the values being
   compared and *CODE is the code used to compare them.

   Update *CODE, *OP0 and *OP1 so that they describe the final comparison. */

static void
riscv_emit_compare (enum rtx_code *code, rtx *op0, rtx *op1)
{
  rtx cmp_op0 = *op0;
  rtx cmp_op1 = *op1;

  if (GET_MODE_CLASS (GET_MODE (*op0)) == MODE_INT)
    {
      if (splittable_const_int_operand (cmp_op1, VOIDmode))
	{
	  HOST_WIDE_INT rhs = INTVAL (cmp_op1);

	  if (*code == EQ || *code == NE)
	    {
	      /* Convert e.g. OP0 == 2048 into OP0 - 2048 == 0.  */
	      if (SMALL_OPERAND (-rhs))
		{
		  *op0 = gen_reg_rtx (GET_MODE (cmp_op0));
		  riscv_emit_binary (PLUS, *op0, cmp_op0, GEN_INT (-rhs));
		  *op1 = const0_rtx;
		}
	    }
	  else
	    {
	      static const enum rtx_code mag_comparisons[][2] = {
		{LEU, LTU}, {GTU, GEU}, {LE, LT}, {GT, GE}
	      };

	      /* Convert e.g. (OP0 <= 0xFFF) into (OP0 < 0x1000).  */
	      for (size_t i = 0; i < ARRAY_SIZE (mag_comparisons); i++)
		{
		  HOST_WIDE_INT new_rhs;
		  bool increment = *code == mag_comparisons[i][0];
		  bool decrement = *code == mag_comparisons[i][1];
		  if (!increment && !decrement)
		    continue;

		  new_rhs = rhs + (increment ? 1 : -1);
		  if (riscv_integer_cost (new_rhs) < riscv_integer_cost (rhs)
		      && (rhs < 0) == (new_rhs < 0))
		    {
		      *op1 = GEN_INT (new_rhs);
		      *code = mag_comparisons[i][increment];
		    }
		  break;
		}
	    }
	}

      if (*op1 != const0_rtx)
	*op1 = force_reg (GET_MODE (cmp_op0), *op1);
    }
  else
    {
      /* For FP comparisons, set an integer register with the result of the
	 comparison, then branch on it. */
      rtx tmp0, tmp1;
      enum rtx_code fp_code = *code;
      *code = NE;
      *op1 = const0_rtx;

      switch (fp_code)
	{
	case UNORDERED:
	  *code = EQ;
	  /* Fall through.  */

	case ORDERED:
	  /* a == a && b == b */
	  tmp0 = gen_reg_rtx (SImode);
	  riscv_emit_binary (EQ, tmp0, cmp_op0, cmp_op0);
	  tmp1 = gen_reg_rtx (SImode);
	  riscv_emit_binary (EQ, tmp1, cmp_op1, cmp_op1);
	  *op0 = gen_reg_rtx (SImode);
	  riscv_emit_binary (AND, *op0, tmp0, tmp1);
	  break;

	case UNEQ:
	  *code = EQ;
	  /* Fall through.  */

	case LTGT:
	  /* ordered(a, b) != (a == b) */
	  tmp0 = gen_reg_rtx (SImode);
	  riscv_emit_binary (EQ, tmp0, cmp_op0, cmp_op0);
	  tmp1 = gen_reg_rtx (SImode);
	  riscv_emit_binary (EQ, tmp1, cmp_op1, cmp_op1);
	  *op0 = gen_reg_rtx (SImode);
	  riscv_emit_binary (AND, *op0, tmp0, tmp1);
	  *op1 = gen_reg_rtx (SImode);
	  riscv_emit_binary (EQ, *op1, cmp_op0, cmp_op1);
	  break;

#define UNORDERED_COMPARISON(CODE, CMP)					  \
	case CODE:							  \
	  *code = EQ;							  \
	  *op0 = gen_reg_rtx (SImode);					  \
	  if (GET_MODE (cmp_op0) == SFmode)				  \
	    emit_insn (gen_f##CMP##_guardedsf4 (*op0, cmp_op0, cmp_op1)); \
	  else								  \
	    emit_insn (gen_f##CMP##_guardeddf4 (*op0, cmp_op0, cmp_op1)); \
	  break;

	UNORDERED_COMPARISON(UNLT, ge)
	UNORDERED_COMPARISON(UNLE, gt)
	UNORDERED_COMPARISON(UNGT, le)
	UNORDERED_COMPARISON(UNGE, lt)
#undef UNORDERED_COMPARISON

	case NE:
	  fp_code = EQ;
	  *code = EQ;
	  /* Fall through.  */

	case EQ:
	case LE:
	case LT:
	case GE:
	case GT:
	  /* We have instructions for these cases.  */
	  *op0 = gen_reg_rtx (SImode);
	  riscv_emit_binary (fp_code, *op0, cmp_op0, cmp_op1);
	  break;

	default:
	  gcc_unreachable ();
	}
    }
}

/* Try performing the comparison in OPERANDS[1], whose arms are OPERANDS[2]
   and OPERAND[3].  Store the result in OPERANDS[0].

   On 64-bit targets, the mode of the comparison and target will always be
   SImode, thus possibly narrower than that of the comparison's operands.  */

void
riscv_expand_scc (rtx operands[])
{
  rtx target = operands[0];
  enum rtx_code code = GET_CODE (operands[1]);
  rtx op0 = operands[2];
  rtx op1 = operands[3];

  gcc_assert (GET_MODE_CLASS (GET_MODE (op0)) == MODE_INT);

  if (code == EQ || code == NE)
    {
      rtx zie = riscv_zero_if_equal (op0, op1);
      riscv_emit_binary (code, target, zie, const0_rtx);
    }
  else
    riscv_emit_int_order_test (code, 0, target, op0, op1);
}

/* Compare OPERANDS[1] with OPERANDS[2] using comparison code
   CODE and jump to OPERANDS[3] if the condition holds.  */

void
riscv_expand_conditional_branch (rtx *operands)
{
  enum rtx_code code = GET_CODE (operands[0]);
  rtx op0 = operands[1];
  rtx op1 = operands[2];
  rtx condition;

  riscv_emit_compare (&code, &op0, &op1);
  condition = gen_rtx_fmt_ee (code, VOIDmode, op0, op1);
  emit_jump_insn (gen_condjump (condition, operands[3]));
}

/* Implement TARGET_FUNCTION_ARG_BOUNDARY.  Every parameter gets at
   least PARM_BOUNDARY bits of alignment, but will be given anything up
   to STACK_BOUNDARY bits if the type requires it.  */

static unsigned int
riscv_function_arg_boundary (enum machine_mode mode, const_tree type)
{
  unsigned int alignment;

  /* Use natural alignment if the type is not aggregate data.  */
  if (type && !AGGREGATE_TYPE_P (type))
    alignment = TYPE_ALIGN (TYPE_MAIN_VARIANT (type));
  else
    alignment = type ? TYPE_ALIGN (type) : GET_MODE_ALIGNMENT (mode);

  if (alignment < PARM_BOUNDARY)
    alignment = PARM_BOUNDARY;
  if (alignment > STACK_BOUNDARY)
    alignment = STACK_BOUNDARY;
  return alignment;
}

/* If MODE represents an argument that can be passed or returned in
   floating-point registers, return the number of registers, else 0.  */

static unsigned
riscv_pass_mode_in_fpr_p (enum machine_mode mode)
{
  if (GET_MODE_UNIT_SIZE (mode) <= UNITS_PER_FP_ARG)
    {
      if (GET_MODE_CLASS (mode) == MODE_FLOAT)
	return 1;

      if (GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT)
	return 2;
    }

  return 0;
}

typedef struct {
  const_tree type;
  HOST_WIDE_INT offset;
} riscv_aggregate_field;

/* Identify subfields of aggregates that are candidates for passing in
   floating-point registers.  */

static int
riscv_flatten_aggregate_field (const_tree type,
			       riscv_aggregate_field fields[2],
			       int n, HOST_WIDE_INT offset)
{
  switch (TREE_CODE (type))
    {
    case RECORD_TYPE:
      for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
	if (TREE_CODE (f) == FIELD_DECL)
	  {
	    if (!TYPE_P (TREE_TYPE (f)))
	      return -1;

	    n = riscv_flatten_aggregate_field (TREE_TYPE (f), fields, n,
					       offset + int_byte_position (f));
	    if (n < 0)
	        return -1;
	  }
      return n;

    case ARRAY_TYPE:
      {
	HOST_WIDE_INT n_elts;
	riscv_aggregate_field subfields[2];
	tree index = TYPE_DOMAIN (type);
	tree elt_size = TYPE_SIZE_UNIT (TREE_TYPE (type));
	int n_subfields = riscv_flatten_aggregate_field (TREE_TYPE (type),
							 subfields, 0, offset);

	/* Can't handle incomplete types nor sizes that are not fixed.  */
	if (n_subfields <= 0
	    || !COMPLETE_TYPE_P (type)
	    || TREE_CODE (TYPE_SIZE (type)) != INTEGER_CST
	    || !index
	    || !TYPE_MAX_VALUE (index)
	    || !tree_fits_uhwi_p (TYPE_MAX_VALUE (index))
	    || !TYPE_MIN_VALUE (index)
	    || !tree_fits_uhwi_p (TYPE_MIN_VALUE (index))
	    || !tree_fits_uhwi_p (elt_size))
	  return -1;

	n_elts = 1 + tree_to_uhwi (TYPE_MAX_VALUE (index))
		   - tree_to_uhwi (TYPE_MIN_VALUE (index));
	gcc_assert (n_elts >= 0);

	for (HOST_WIDE_INT i = 0; i < n_elts; i++)
	  for (int j = 0; j < n_subfields; j++)
	    {
	      if (n >= 2)
		return -1;

	      fields[n] = subfields[j];
	      fields[n++].offset += i * tree_to_uhwi (elt_size);
	    }

	return n;
      }

    default:
      if (n < 2
	  && ((SCALAR_FLOAT_TYPE_P (type)
	       && GET_MODE_SIZE (TYPE_MODE (type)) <= UNITS_PER_FP_ARG)
	      || (INTEGRAL_TYPE_P (type)
		  && GET_MODE_SIZE (TYPE_MODE (type)) <= UNITS_PER_WORD)))
	{
	  fields[n].type = type;
	  fields[n].offset = offset;
	  return n + 1;
	}
      else
	return -1;
    }
}

/* Identify candidate aggregates for passing in floating-point registers.
   Candidates have at most two fields after flattening.  */

static int
riscv_flatten_aggregate_argument (const_tree type,
				  riscv_aggregate_field fields[2])
{
  if (!type || TREE_CODE (type) != RECORD_TYPE)
    return -1;

  return riscv_flatten_aggregate_field (type, fields, 0, 0);
}

/* See whether TYPE is a record whose fields should be returned in one or
   two floating-point registers.  If so, populate FIELDS accordingly.  */

static unsigned
riscv_pass_aggregate_in_fpr_pair_p (const_tree type,
				    riscv_aggregate_field fields[2])
{
  int n = riscv_flatten_aggregate_argument (type, fields);

  for (int i = 0; i < n; i++)
    if (!SCALAR_FLOAT_TYPE_P (fields[i].type))
      return 0;

  return n > 0 ? n : 0;
}

/* See whether TYPE is a record whose fields should be returned in one or
   floating-point register and one integer register.  If so, populate
   FIELDS accordingly.  */

static bool
riscv_pass_aggregate_in_fpr_and_gpr_p (const_tree type,
				       riscv_aggregate_field fields[2])
{
  unsigned num_int = 0, num_float = 0;
  int n = riscv_flatten_aggregate_argument (type, fields);

  for (int i = 0; i < n; i++)
    {
      num_float += SCALAR_FLOAT_TYPE_P (fields[i].type);
      num_int += INTEGRAL_TYPE_P (fields[i].type);
    }

  return num_int == 1 && num_float == 1;
}

/* Return the representation of an argument passed or returned in an FPR
   when the value has mode VALUE_MODE and the type has TYPE_MODE.  The
   two modes may be different for structures like:

       struct __attribute__((packed)) foo { float f; }

  where the SFmode value "f" is passed in REGNO but the struct itself
  has mode BLKmode.  */

static rtx
riscv_pass_fpr_single (enum machine_mode type_mode, unsigned regno,
		       enum machine_mode value_mode)
{
  rtx x = gen_rtx_REG (value_mode, regno);

  if (type_mode != value_mode)
    {
      x = gen_rtx_EXPR_LIST (VOIDmode, x, const0_rtx);
      x = gen_rtx_PARALLEL (type_mode, gen_rtvec (1, x));
    }
  return x;
}

/* Pass or return a composite value in the FPR pair REGNO and REGNO + 1.
   MODE is the mode of the composite.  MODE1 and OFFSET1 are the mode and
   byte offset for the first value, likewise MODE2 and OFFSET2 for the
   second value.  */

static rtx
riscv_pass_fpr_pair (enum machine_mode mode, unsigned regno1,
		     enum machine_mode mode1, HOST_WIDE_INT offset1,
		     unsigned regno2, enum machine_mode mode2,
		     HOST_WIDE_INT offset2)
{
  return gen_rtx_PARALLEL
    (mode,
     gen_rtvec (2,
		gen_rtx_EXPR_LIST (VOIDmode,
				   gen_rtx_REG (mode1, regno1),
				   GEN_INT (offset1)),
		gen_rtx_EXPR_LIST (VOIDmode,
				   gen_rtx_REG (mode2, regno2),
				   GEN_INT (offset2))));
}

/* Fill INFO with information about a single argument, and return an
   RTL pattern to pass or return the argument.  CUM is the cumulative
   state for earlier arguments.  MODE is the mode of this argument and
   TYPE is its type (if known).  NAMED is true if this is a named
   (fixed) argument rather than a variable one.  RETURN_P is true if
   returning the argument, or false if passing the argument.  */

static rtx
riscv_get_arg_info (struct riscv_arg_info *info, const CUMULATIVE_ARGS *cum,
		    enum machine_mode mode, const_tree type, bool named,
		    bool return_p)
{
  unsigned num_bytes, num_words;
  unsigned fpr_base = return_p ? FP_RETURN : FP_ARG_FIRST;
  unsigned gpr_base = return_p ? GP_RETURN : GP_ARG_FIRST;

  memset (info, 0, sizeof (*info));
  info->gpr_offset = cum->num_gprs;
  info->fpr_offset = cum->num_fprs;

  if (named)
    {
      riscv_aggregate_field fields[2];
      unsigned fregno = fpr_base + info->fpr_offset;
      unsigned gregno = gpr_base + info->gpr_offset;

      /* Pass one- or two-element floating-point aggregates in FPRs.  */
      if ((info->num_fprs = riscv_pass_aggregate_in_fpr_pair_p (type, fields))
	  && info->fpr_offset + info->num_fprs <= MAX_ARGS_IN_REGISTERS)
	switch (info->num_fprs)
	  {
	  case 1:
	    return riscv_pass_fpr_single (mode, fregno,
					  TYPE_MODE (fields[0].type));

	  case 2:
	    return riscv_pass_fpr_pair (mode, fregno,
					TYPE_MODE (fields[0].type),
					fields[0].offset,
					fregno + 1,
					TYPE_MODE (fields[1].type),
					fields[1].offset);

	  default:
	    gcc_unreachable ();
	  }

      /* Pass real and complex floating-point numbers in FPRs.  */
      if ((info->num_fprs = riscv_pass_mode_in_fpr_p (mode))
	  && info->fpr_offset + info->num_fprs <= MAX_ARGS_IN_REGISTERS)
	switch (GET_MODE_CLASS (mode))
	  {
	  case MODE_FLOAT:
	    return gen_rtx_REG (mode, fregno);

	  case MODE_COMPLEX_FLOAT:
	    return riscv_pass_fpr_pair (mode, fregno, GET_MODE_INNER (mode), 0,
					fregno + 1, GET_MODE_INNER (mode),
					GET_MODE_UNIT_SIZE (mode));

	  default:
	    gcc_unreachable ();
	  }

      /* Pass structs with one float and one integer in an FPR and a GPR.  */
      if (riscv_pass_aggregate_in_fpr_and_gpr_p (type, fields)
	  && info->gpr_offset < MAX_ARGS_IN_REGISTERS
	  && info->fpr_offset < MAX_ARGS_IN_REGISTERS)
	{
	  info->num_gprs = 1;
	  info->num_fprs = 1;

	  if (!SCALAR_FLOAT_TYPE_P (fields[0].type))
	    std::swap (fregno, gregno);

	  return riscv_pass_fpr_pair (mode, fregno, TYPE_MODE (fields[0].type),
				      fields[0].offset,
				      gregno, TYPE_MODE (fields[1].type),
				      fields[1].offset);
	}
    }

  /* Work out the size of the argument.  */
  num_bytes = type ? int_size_in_bytes (type) : GET_MODE_SIZE (mode);
  num_words = (num_bytes + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  /* Doubleword-aligned arguments start on an even register boundary.  */
  if (num_bytes && riscv_function_arg_boundary (mode, type) > BITS_PER_WORD)
    info->gpr_offset += info->gpr_offset & 1;

  /* Partition the argument between registers and stack.  */
  info->num_fprs = 0;
  info->num_gprs = MIN (num_words, MAX_ARGS_IN_REGISTERS - info->gpr_offset);
  info->stack_p = (num_words - info->num_gprs) != 0;

  if (info->num_gprs || return_p)
    return gen_rtx_REG (mode, gpr_base + info->gpr_offset);

  return NULL_RTX;
}

/* Implement TARGET_FUNCTION_ARG.  */

static rtx
riscv_function_arg (cumulative_args_t cum_v, enum machine_mode mode,
		    const_tree type, bool named)
{
  rtx ret = NULL;

  CUMULATIVE_ARGS *cum = get_cumulative_args (cum_v);
  struct riscv_arg_info info;

  if (mode == VOIDmode)
    return NULL;

  ret = riscv_get_arg_info (&info, cum, mode, type, named, false);
  return ret;
}

/* Implement TARGET_FUNCTION_ARG_ADVANCE.  */

static void
riscv_function_arg_advance (cumulative_args_t cum_v, enum machine_mode mode,
			    const_tree type, bool named)
{
  CUMULATIVE_ARGS *cum = get_cumulative_args (cum_v);
  struct riscv_arg_info info;

  riscv_get_arg_info (&info, cum, mode, type, named, false);

  /* Advance the register count.  This has the effect of setting
     num_gprs to MAX_ARGS_IN_REGISTERS if a doubleword-aligned
     argument required us to skip the final GPR and pass the whole
     argument on the stack.  */
  cum->num_fprs = info.fpr_offset + info.num_fprs;
  cum->num_gprs = info.gpr_offset + info.num_gprs;
}

/* Implement TARGET_ARG_PARTIAL_BYTES.  */

static int
riscv_arg_partial_bytes (cumulative_args_t cum,
			 enum machine_mode mode, tree type, bool named)
{
  struct riscv_arg_info arg;

  riscv_get_arg_info (&arg, get_cumulative_args (cum), mode, type, named, false);
  return arg.stack_p ? arg.num_gprs * UNITS_PER_WORD : 0;
}

/* Implement FUNCTION_VALUE and LIBCALL_VALUE.  For normal calls,
   VALTYPE is the return type and MODE is VOIDmode.  For libcalls,
   VALTYPE is null and MODE is the mode of the return value.  */

rtx
riscv_target_function_value (const_tree ret_type, const_tree fn_decl_or_type, bool outgoing) {
  return riscv_function_value (ret_type, fn_decl_or_type, TYPE_MODE(ret_type), outgoing);
}

rtx riscv_libcall_value (enum machine_mode mode) {
  struct riscv_arg_info info;
  CUMULATIVE_ARGS args;
  memset (&args, 0, sizeof args);
  return riscv_get_arg_info (&info, &args, mode, NULL_TREE, true, true);
}

rtx
riscv_function_value (const_tree ret_type, const_tree fn_decl_or_type, enum machine_mode mode, bool outgoing)
{
  struct riscv_arg_info info;
  CUMULATIVE_ARGS args;
  const_tree type = ret_type;
  const_tree func = NULL_TREE;
  rtx ret;

  if (!mode && ret_type) {
    mode = TYPE_MODE (ret_type);
  }

  if (type)
    {
      int unsigned_p = TYPE_UNSIGNED (type);

      mode = TYPE_MODE (type);

      /* Since TARGET_PROMOTE_FUNCTION_MODE unconditionally promotes,
	 return values, promote the mode here too.  */
      mode = promote_function_mode (type, mode, &unsigned_p, func, 1);
      //mode = TARGET_PROMOTE_FUNCTION_MODE (type, mode, &unsigned_p, NULL, 1);
    }

  memset (&args, 0, sizeof args);
  ret = riscv_get_arg_info (&info, &args, mode, type, true, true);
//  if (outgoing && cfun)
//    gcc_assert(cfun->machine->returns_pointer ^ POINTER_TYPE_P(type));
  if (TARGET_XSCEN_ENABLED && cfun && cfun->machine->returns_pointer && outgoing) {
    gcc_assert(POINTER_TYPE_P(type));
    if (TARGET_XSCEN_DEBUG)
      fprintf(stderr, "riscv_function_value: returning a pointer in %s\n", current_function_name());
    cfun->machine->return_register = rhs_regno(ret);
  }
  return ret;
}

/* Implement TARGET_PASS_BY_REFERENCE. */

static bool
riscv_pass_by_reference (cumulative_args_t cum_v, enum machine_mode mode,
			 const_tree type, bool named)
{
  HOST_WIDE_INT size = type ? int_size_in_bytes (type) : GET_MODE_SIZE (mode);
  struct riscv_arg_info info;
  CUMULATIVE_ARGS *cum = get_cumulative_args (cum_v);

  /* ??? std_gimplify_va_arg_expr passes NULL for cum.  Fortunately, we
     never pass variadic arguments in floating-point registers, so we can
     avoid the call to riscv_get_arg_info in this case.  */
  if (cum != NULL)
    {
      /* Don't pass by reference if we can use a floating-point register.  */
      riscv_get_arg_info (&info, cum, mode, type, named, false);
      if (info.num_fprs)
	return false;
    }

  /* Pass by reference if the data do not fit in two integer registers.  */
  return !IN_RANGE (size, 0, 2 * UNITS_PER_WORD);
}

/* Implement TARGET_RETURN_IN_MEMORY.  */

static bool
riscv_return_in_memory (const_tree type, const_tree fndecl ATTRIBUTE_UNUSED)
{
  CUMULATIVE_ARGS args;
  cumulative_args_t cum = pack_cumulative_args (&args);

  /* The rules for returning in memory are the same as for passing the
     first named argument by reference.  */
  memset (&args, 0, sizeof args);
  return riscv_pass_by_reference (cum, TYPE_MODE (type), type, true);
}

/* Implement TARGET_SETUP_INCOMING_VARARGS.  */

static void
riscv_setup_incoming_varargs (cumulative_args_t cum, enum machine_mode mode,
			     tree type, int *pretend_size ATTRIBUTE_UNUSED,
			     int no_rtl)
{
  CUMULATIVE_ARGS local_cum;
  int gp_saved;

  /* The caller has advanced CUM up to, but not beyond, the last named
     argument.  Advance a local copy of CUM past the last "real" named
     argument, to find out how many registers are left over.  */
  local_cum = *get_cumulative_args (cum);
  riscv_function_arg_advance (pack_cumulative_args (&local_cum), mode, type, 1);

  /* Found out how many registers we need to save.  */
  gp_saved = MAX_ARGS_IN_REGISTERS - local_cum.num_gprs;

  if (!no_rtl && gp_saved > 0)
    {
      rtx ptr = plus_constant (Pmode, virtual_incoming_args_rtx,
			       REG_PARM_STACK_SPACE (cfun->decl)
			       - gp_saved * UNITS_PER_WORD);
      rtx mem = gen_frame_mem (BLKmode, ptr);
      set_mem_alias_set (mem, get_varargs_alias_set ());

      move_block_from_reg (local_cum.num_gprs + GP_ARG_FIRST,
			   mem, gp_saved);
    }
  if (REG_PARM_STACK_SPACE (cfun->decl) == 0)
    cfun->machine->varargs_size = gp_saved * UNITS_PER_WORD;
}

/* Implement TARGET_EXPAND_BUILTIN_VA_START.  */

static void
riscv_va_start (tree valist, rtx nextarg)
{
  nextarg = plus_constant (Pmode, nextarg, -cfun->machine->varargs_size);
  std_expand_builtin_va_start (valist, nextarg);
}

//#undef TARGET_INIT_BUILTINS
//#define TARGET_INIT_BUILTINS riscv_builtin_init
//static void
//riscv_builtin_init()
//{
//  tree type;
//  int code;
//
//  tree args = void_list_node;
//  tree arg1 = NULL_TREE;
//  arg1 = make_tree();
//  TREE_CODE(arg1) = POINTER_TYPE;
//  args = tree_cons(NULL, arg1, args);
//  type = build_function_type(VOID_TYPE, args);
//  code = 1;
//  
//  add_builtin_function ("__builtin_scen_storage_region", type, code, BUILT_IN_NORMAL, NULL, NULL_TREE);
//}

void
riscv_xscen_new_storage_region2(rtx reg, HOST_WIDE_INT size)
{
  gcc_assert(size > 0);
  HOST_WIDE_INT lastaddr = size-1;
  rtx insn;
  rtx temp2;
  if (TARGET_XSCEN_DEBUG)
    fprintf(stderr, "riscv_xscen_new_storage_region2: size %ld\n", size);
  if (!SMALL_OPERAND(lastaddr)) {
    if (TARGET_XSCEN_DEBUG)
      fprintf(stderr, "    not a small operand\n");
    //TODO: using this register not sure to be safe, requires some rewrite anyway
    temp2 = gen_rtx_REG (Pmode, GP_TEMP_FIRST+2);
    gcc_assert(temp2);

    riscv_add_offset(temp2, reg, lastaddr);
    insn = gen_xscen_storage_region_add(reg, temp2, GEN_INT(0));
    emit_insn(insn);
  } else {
    insn = gen_xscen_new_small_storage_region(reg, GEN_INT(lastaddr));
    emit_insn(insn);
  }
}

void
riscv_xscen_new_stack_storage_region (HOST_WIDE_INT size, HOST_WIDE_INT args)
{
  gcc_assert(size > 0);
  if (TARGET_XSCEN_DEBUG)
    fprintf(stderr, "riscv_xscen_new_stack_storage_region called size: %ld (%ssmall), args: %ld\n", size, (SMALL_OPERAND(size)?"":"not "), args);
  rtx insn;
  if (!SMALL_OPERAND(size)) {
    rtx temp = RISCV_PROLOGUE_TEMP (Pmode);
    gcc_assert(temp);
    riscv_emit_move (RISCV_PROLOGUE_TEMP (Pmode), GEN_INT(-size)); //t0 = -size
    temp = RISCV_PROLOGUE_TEMP (Pmode);
    insn = gen_add3_insn(temp, stack_pointer_rtx, temp); //t0 = sp + t0
    emit_insn(insn);
    if (args == 0) {
      insn = gen_xscen_storage_region_dda(temp, stack_pointer_rtx, GEN_INT(0)); //srdda t0, sp
      emit_insn(insn);
    } else {
      insn = gen_xscen_storage_region_add(temp, stack_pointer_rtx, GEN_INT(-1+args)); //sradd t0, sp+args-1
      emit_insn(insn);
    }
  } else {
    if (args == 0) {
      insn = gen_xscen_storage_region_dda(stack_pointer_rtx, stack_pointer_rtx, GEN_INT(-size)); 
      emit_insn(insn);
    } else {
      rtx temp = RISCV_PROLOGUE_TEMP (Pmode);
      gcc_assert(temp);
      insn = gen_add3_insn(temp, stack_pointer_rtx, GEN_INT(args-1)); //t0 = sp + args-1
      emit_insn(insn);
      insn = gen_xscen_storage_region_dda(stack_pointer_rtx, temp, GEN_INT(-size)); //srdda -size(sp), t0
      emit_insn(insn);
    }
  }
}

static void
riscv_xscen_prologue_globals()
{
  bool ret;
  rtx insn;
  gcc_assert(cfun->machine->global_count <= XSCEN_MAX_GLOBALS);
  for (int i=0; i<cfun->machine->global_count; i++) {
    HOST_WIDE_INT size = cfun->machine->global_sizes[i];
    tree g = cfun->machine->globals[i];
    rtx dest = RISCV_PROLOGUE_TEMP (Pmode);
    rtx src;

    if (TREE_CODE(g) == VAR_DECL) {
      src = XEXP(DECL_RTL(g), 0);
    } else if (TREE_CODE(g) == STRING_CST) {
      src = XEXP(output_constant_def(g, 1), 0);
    } else if (TREE_CODE(g) == ADDR_EXPR) {
      gcc_assert(TREE_CODE(TREE_OPERAND(g, 0)) == STRING_CST);
      //get first operand of ADDR_EXPR which is STRING_CST or other constant
      //then use output_constant_def to gen the rtl or get the pre-generated mem
      //then get first operand from that which is symbol_ref
      src = XEXP(output_constant_def(TREE_OPERAND(g, 0), 1), 0);
    } else {
      //expected VAR_DECL or ADDR_EXPR or STRING_CST
      gcc_unreachable();
    }
    if (TARGET_XSCEN_DEBUG)
      fprintf(stderr, "   handling global %d: dest %s src %s\n", i, GET_RTX_NAME(GET_CODE(dest)), GET_RTX_NAME(GET_CODE(src)));
    ret = riscv_legitimize_move(Pmode, dest, src);
    gcc_assert(ret);
    insn = gen_xscen_new_storage_region2(dest, GEN_INT(size));
    emit_insn(insn);
  }
}

void
riscv_xscen_register_jumptable (rtx l, rtx_insn* insn)
{
  unsigned i = cfun->machine->jtcount;
  cfun->machine->jumptables[i] = l;
  cfun->machine->jtcount++;
  gcc_assert(cfun->machine->jtcount <= XSCEN_MAX_SWITCHES);
}

static void
riscv_xscen_prologue_jumptables ()
{
  for (unsigned i=0; i<cfun->machine->jtcount; i++) {
    bool ret;
    HOST_WIDE_INT size, modesize;
    unsigned cases;
    rtx dest, src, insn;
    rtx_insn* table;
    rtvec rv;
    rtx_jump_table_data *jtd;

    src = cfun->machine->jumptables[i];
    gcc_assert(src);

    table = NEXT_INSN(safe_as_a <rtx_insn *> (src));
    if (table == NULL_RTX || !JUMP_TABLE_DATA_P(table))
      gcc_unreachable();

    modesize = GET_MODE_SIZE(Pmode);
    jtd = as_a <rtx_jump_table_data *> (table);
    rv = jtd->get_labels();
    cases = GET_NUM_ELEM(rv);
    size = cases*modesize;

    gcc_assert(size);
    dest = RISCV_PROLOGUE_TEMP (Pmode);
    src = gen_rtx_LABEL_REF(Pmode, src);
    ++LABEL_NUSES(src);
    ret = riscv_legitimize_move(Pmode, dest, src);
    gcc_assert(ret);
    insn = gen_xscen_new_storage_region2(dest, GEN_INT(size));
    emit_insn(insn);
  }
}

void
riscv_xscen_prepare_prologue()
{
  rtx insn;

  //enforcement starts from main
  if (!strcmp(current_function_name(), "main")) {
    insn = gen_xscen_enter_scope();
    emit_insn(insn);
  }

  insn = gen_xscen_stack_prologue();
  emit_insn(insn);

  riscv_xscen_prologue_globals();

  riscv_xscen_prologue_jumptables();
}

static void
riscv_xscen_epilogue()
{
  if (TARGET_XSCEN_DEBUG)
    fprintf(stderr, "riscv_xscen_epilogue\n");
  rtx insn;

  if (cfun->machine->allocator) {
  }

  if (cfun->machine->returns_pointer) {
    insn = gen_xscen_delegate(gen_rtx_REG(Pmode, cfun->machine->return_register), GEN_INT(0));
    emit_insn(insn);
  }
  insn = gen_xscen_epilogue();
  emit_insn(insn);
}

/* Expand a call of type TYPE.  RESULT is where the result will go (null
   for "call"s and "sibcall"s), ADDR is the address of the function,
   ARGS_SIZE is the size of the arguments and AUX is the value passed
   to us by riscv_function_arg.  Return the call itself.  */

rtx_insn *
riscv_expand_call (bool sibcall_p, rtx result, rtx addr, rtx args_size)
{
  rtx pattern;

  if (!call_insn_operand (addr, VOIDmode))
    {
      rtx reg = RISCV_PROLOGUE_TEMP (Pmode);
      riscv_emit_move (reg, addr);
      addr = reg;
    }

  if (result == 0)
    {
      rtx (*fn) (rtx, rtx);

      if (sibcall_p)
	fn = gen_sibcall_internal;
      else
	fn = gen_call_internal;

      pattern = fn (addr, args_size);
    }
  else if (GET_CODE (result) == PARALLEL && XVECLEN (result, 0) == 2)
    {
      /* Handle return values created by riscv_pass_fpr_pair.  */
      rtx (*fn) (rtx, rtx, rtx, rtx);
      rtx reg1, reg2;

      if (sibcall_p)
	fn = gen_sibcall_value_multiple_internal;
      else
	fn = gen_call_value_multiple_internal;

      reg1 = XEXP (XVECEXP (result, 0, 0), 0);
      reg2 = XEXP (XVECEXP (result, 0, 1), 0);
      pattern = fn (reg1, addr, args_size, reg2);
    }
  else
    {
      rtx (*fn) (rtx, rtx, rtx);

      if (sibcall_p)
	fn = gen_sibcall_value_internal;
      else
	fn = gen_call_value_internal;

      /* Handle return values created by riscv_pass_fpr_single.  */
      if (GET_CODE (result) == PARALLEL && XVECLEN (result, 0) == 1)
	result = XEXP (XVECEXP (result, 0, 0), 0);
      pattern = fn (result, addr, args_size);
    }

  if (TARGET_XSCEN_ENABLED) {
    if (TARGET_XSCEN_DEBUG)
      fprintf(stderr, "riscv_expand_call: xscen part starting\n");
    tree calleet = XTREE(addr, 1);
    unsigned uid = 0;
    if (calleet != NULL_TREE) {
      uid = DECL_UID(FUNCTION_DECL_CHECK(calleet));
    } else {
      uid = cfun->machine->unknown_callee++;
      if (TARGET_XSCEN_DEBUG)
        fprintf(stderr, "   no FUNC_DECL: anonymous function\n");
    }
    emit_insn(gen_xscen_call_site_delegate(GEN_INT(uid)));
    emit_insn(gen_xscen_enter_scope());
  }

  return emit_call_insn (pattern);
}

/* Emit straight-line code to move LENGTH bytes from SRC to DEST.
   Assume that the areas do not overlap.  */

static void
riscv_block_move_straight (rtx dest, rtx src, HOST_WIDE_INT length)
{
  HOST_WIDE_INT offset, delta;
  unsigned HOST_WIDE_INT bits;
  int i;
  enum machine_mode mode;
  rtx *regs;

  bits = MAX (BITS_PER_UNIT,
	      MIN (BITS_PER_WORD, MIN (MEM_ALIGN (src), MEM_ALIGN (dest))));

  mode = mode_for_size (bits, MODE_INT, 0);
  delta = bits / BITS_PER_UNIT;

  /* Allocate a buffer for the temporary registers.  */
  regs = XALLOCAVEC (rtx, length / delta);

  /* Load as many BITS-sized chunks as possible.  Use a normal load if
     the source has enough alignment, otherwise use left/right pairs.  */
  for (offset = 0, i = 0; offset + delta <= length; offset += delta, i++)
    {
      regs[i] = gen_reg_rtx (mode);
      riscv_emit_move (regs[i], adjust_address (src, mode, offset));
    }

  /* Copy the chunks to the destination.  */
  for (offset = 0, i = 0; offset + delta <= length; offset += delta, i++)
    riscv_emit_move (adjust_address (dest, mode, offset), regs[i]);

  /* Mop up any left-over bytes.  */
  if (offset < length)
    {
      src = adjust_address (src, BLKmode, offset);
      dest = adjust_address (dest, BLKmode, offset);
      move_by_pieces (dest, src, length - offset,
		      MIN (MEM_ALIGN (src), MEM_ALIGN (dest)), 0);
    }
}

/* Helper function for doing a loop-based block operation on memory
   reference MEM.  Each iteration of the loop will operate on LENGTH
   bytes of MEM.

   Create a new base register for use within the loop and point it to
   the start of MEM.  Create a new memory reference that uses this
   register.  Store them in *LOOP_REG and *LOOP_MEM respectively.  */

static void
riscv_adjust_block_mem (rtx mem, HOST_WIDE_INT length,
		       rtx *loop_reg, rtx *loop_mem)
{
  *loop_reg = copy_addr_to_reg (XEXP (mem, 0));

  /* Although the new mem does not refer to a known location,
     it does keep up to LENGTH bytes of alignment.  */
  *loop_mem = change_address (mem, BLKmode, *loop_reg);
  set_mem_align (*loop_mem, MIN (MEM_ALIGN (mem), length * BITS_PER_UNIT));
}

/* Move LENGTH bytes from SRC to DEST using a loop that moves BYTES_PER_ITER
   bytes at a time.  LENGTH must be at least BYTES_PER_ITER.  Assume that
   the memory regions do not overlap.  */

static void
riscv_block_move_loop (rtx dest, rtx src, HOST_WIDE_INT length,
		      HOST_WIDE_INT bytes_per_iter)
{
  rtx label, src_reg, dest_reg, final_src, test;
  HOST_WIDE_INT leftover;

  leftover = length % bytes_per_iter;
  length -= leftover;

  /* Create registers and memory references for use within the loop.  */
  riscv_adjust_block_mem (src, bytes_per_iter, &src_reg, &src);
  riscv_adjust_block_mem (dest, bytes_per_iter, &dest_reg, &dest);

  /* Calculate the value that SRC_REG should have after the last iteration
     of the loop.  */
  final_src = expand_simple_binop (Pmode, PLUS, src_reg, GEN_INT (length),
				   0, 0, OPTAB_WIDEN);

  /* Emit the start of the loop.  */
  label = gen_label_rtx ();
  emit_label (label);

  /* Emit the loop body.  */
  riscv_block_move_straight (dest, src, bytes_per_iter);

  /* Move on to the next block.  */
  riscv_emit_move (src_reg, plus_constant (Pmode, src_reg, bytes_per_iter));
  riscv_emit_move (dest_reg, plus_constant (Pmode, dest_reg, bytes_per_iter));

  /* Emit the loop condition.  */
  test = gen_rtx_NE (VOIDmode, src_reg, final_src);
  if (Pmode == DImode)
    emit_jump_insn (gen_cbranchdi4 (test, src_reg, final_src, label));
  else
    emit_jump_insn (gen_cbranchsi4 (test, src_reg, final_src, label));

  /* Mop up any left-over bytes.  */
  if (leftover)
    riscv_block_move_straight (dest, src, leftover);
}

/* Expand a movmemsi instruction, which copies LENGTH bytes from
   memory reference SRC to memory reference DEST.  */

bool
riscv_expand_block_move (rtx dest, rtx src, rtx length)
{
  if (CONST_INT_P (length))
    {
      HOST_WIDE_INT factor, align;

      align = MIN (MIN (MEM_ALIGN (src), MEM_ALIGN (dest)), BITS_PER_WORD);
      factor = BITS_PER_WORD / align;

      if (INTVAL (length) <= RISCV_MAX_MOVE_BYTES_STRAIGHT / factor)
	{
	  riscv_block_move_straight (dest, src, INTVAL (length));
	  return true;
	}
      else if (optimize && align >= BITS_PER_WORD)
	{
	  riscv_block_move_loop (dest, src, INTVAL (length),
				RISCV_MAX_MOVE_BYTES_PER_LOOP_ITER / factor);
	  return true;
	}
    }
  return false;
}

/* Print symbolic operand OP, which is part of a HIGH or LO_SUM
   in context CONTEXT.  HI_RELOC indicates a high-part reloc.  */

static void
riscv_print_operand_reloc (FILE *file, rtx op, bool hi_reloc)
{
  const char *reloc;

  switch (riscv_classify_symbolic_expression (op))
    {
      case SYMBOL_ABSOLUTE:
	reloc = hi_reloc ? "%hi" : "%lo";
	break;

      case SYMBOL_PCREL:
	reloc = hi_reloc ? "%pcrel_hi" : "%pcrel_lo";
	break;

      case SYMBOL_TLS_LE:
	reloc = hi_reloc ? "%tprel_hi" : "%tprel_lo";
	break;

      default:
	gcc_unreachable ();
    }

  fprintf (file, "%s(", reloc);
  output_addr_const (file, riscv_strip_unspec_address (op));
  fputc (')', file);
}

/* Return true if the .AQ suffix should be added to an AMO to implement the
   acquire portion of memory model MODEL.  */

static bool
riscv_memmodel_needs_amo_acquire (enum memmodel model)
{
  switch (model)
    {
      case MEMMODEL_ACQ_REL:
      case MEMMODEL_SEQ_CST:
      case MEMMODEL_SYNC_SEQ_CST:
      case MEMMODEL_ACQUIRE:
      case MEMMODEL_CONSUME:
      case MEMMODEL_SYNC_ACQUIRE:
	return true;

      case MEMMODEL_RELEASE:
      case MEMMODEL_SYNC_RELEASE:
      case MEMMODEL_RELAXED:
	return false;

      default:
	gcc_unreachable ();
    }
}

/* Return true if a FENCE should be emitted to before a memory access to
   implement the release portion of memory model MODEL.  */

static bool
riscv_memmodel_needs_release_fence (enum memmodel model)
{
  switch (model)
    {
      case MEMMODEL_ACQ_REL:
      case MEMMODEL_SEQ_CST:
      case MEMMODEL_SYNC_SEQ_CST:
      case MEMMODEL_RELEASE:
      case MEMMODEL_SYNC_RELEASE:
	return true;

      case MEMMODEL_ACQUIRE:
      case MEMMODEL_CONSUME:
      case MEMMODEL_SYNC_ACQUIRE:
      case MEMMODEL_RELAXED:
	return false;

      default:
	gcc_unreachable ();
    }
}

/* Implement TARGET_PRINT_OPERAND.  The RISCV-specific operand codes are:

   'h'	Print the high-part relocation associated with OP, after stripping
	  any outermost HIGH.
   'R'	Print the low-part relocation associated with OP.
   'C'	Print the integer branch condition for comparison OP.
   'A'	Print the atomic operation suffix for memory model OP.
   'F'	Print a FENCE if the memory model requires a release.
   'z'	Print $0 if OP is zero, otherwise print OP normally.  */

static void
riscv_print_operand (FILE *file, rtx op, int letter)
{
  enum machine_mode mode = GET_MODE (op);
  enum rtx_code code = GET_CODE (op);

  switch (letter)
    {
    case 'h':
      if (code == HIGH)
	op = XEXP (op, 0);
      riscv_print_operand_reloc (file, op, true);
      break;

    case 'R':
      riscv_print_operand_reloc (file, op, false);
      break;

    case 'C':
      /* The RTL names match the instruction names. */
      fputs (GET_RTX_NAME (code), file);
      break;

    case 'A':
      if (riscv_memmodel_needs_amo_acquire ((enum memmodel) INTVAL (op)))
	fputs (".aq", file);
      break;

    case 'F':
      if (riscv_memmodel_needs_release_fence ((enum memmodel) INTVAL (op)))
	fputs ("fence rw,w; ", file);
      break;

    default:
      switch (code)
	{
	case REG:
	  if (letter && letter != 'z')
	    output_operand_lossage ("invalid use of '%%%c'", letter);
	  fprintf (file, "%s", reg_names[REGNO (op)]);
	  break;

	case MEM:
	  if (letter && letter != 'z')
	    output_operand_lossage ("invalid use of '%%%c'", letter);
	  else
	    output_address (mode, XEXP (op, 0));
	  break;

	default:
	  if (letter == 'z' && op == CONST0_RTX (GET_MODE (op)))
	    fputs (reg_names[GP_REG_FIRST], file);
	  else if (letter && letter != 'z')
	    output_operand_lossage ("invalid use of '%%%c'", letter);
	  else
	    output_addr_const (file, riscv_strip_unspec_address (op));
	  break;
	}
    }
}

/* Implement TARGET_PRINT_OPERAND_ADDRESS.  */

static void
riscv_print_operand_address (FILE *file, machine_mode mode ATTRIBUTE_UNUSED, rtx x)
{
  struct riscv_address_info addr;

  if (riscv_classify_address (&addr, x, word_mode, true))
    switch (addr.type)
      {
      case ADDRESS_REG:
	riscv_print_operand (file, addr.offset, 0);
	fprintf (file, "(%s)", reg_names[REGNO (addr.reg)]);
	return;

      case ADDRESS_LO_SUM:
	riscv_print_operand_reloc (file, addr.offset, false);
	fprintf (file, "(%s)", reg_names[REGNO (addr.reg)]);
	return;

      case ADDRESS_CONST_INT:
	output_addr_const (file, x);
	fprintf (file, "(%s)", reg_names[GP_REG_FIRST]);
	return;

      case ADDRESS_SYMBOLIC:
	output_addr_const (file, riscv_strip_unspec_address (x));
	return;
      }
  gcc_unreachable ();
}

static bool
riscv_size_ok_for_small_data_p (int size)
{
  return g_switch_value && IN_RANGE (size, 1, g_switch_value);
}

/* Return true if EXP should be placed in the small data section. */

static bool
riscv_in_small_data_p (const_tree x)
{
  if (TREE_CODE (x) == STRING_CST || TREE_CODE (x) == FUNCTION_DECL)
    return false;

  if (TREE_CODE (x) == VAR_DECL && DECL_SECTION_NAME (x))
    {
      const char *sec = DECL_SECTION_NAME (x);
      return strcmp (sec, ".sdata") == 0 || strcmp (sec, ".sbss") == 0;
    }

  return riscv_size_ok_for_small_data_p (int_size_in_bytes (TREE_TYPE (x)));
}

/* Return a section for X, handling small data. */

static section *
riscv_elf_select_rtx_section (enum machine_mode mode, rtx x,
			      unsigned HOST_WIDE_INT align)
{
  section *s = default_elf_select_rtx_section (mode, x, align);

  if (riscv_size_ok_for_small_data_p (GET_MODE_SIZE (mode)))
    {
      if (strncmp (s->named.name, ".rodata.cst", strlen (".rodata.cst")) == 0)
	{
	  /* Rename .rodata.cst* to .srodata.cst*. */
	  char *name = (char *) alloca (strlen (s->named.name) + 2);
	  sprintf (name, ".s%s", s->named.name + 1);
	  return get_section (name, s->named.common.flags, NULL);
	}

      if (s == data_section)
	return sdata_section;
    }

  return s;
}

/* Implement TARGET_ASM_OUTPUT_DWARF_DTPREL.  */

static void ATTRIBUTE_UNUSED
riscv_output_dwarf_dtprel (FILE *file, int size, rtx x)
{
  switch (size)
    {
    case 4:
      fputs ("\t.dtprelword\t", file);
      break;

    case 8:
      fputs ("\t.dtpreldword\t", file);
      break;

    default:
      gcc_unreachable ();
    }
  output_addr_const (file, x);
  fputs ("+0x800", file);
}

/* Make the last instruction frame-related and note that it performs
   the operation described by FRAME_PATTERN.  */

static void
riscv_set_frame_expr (rtx frame_pattern)
{
  rtx insn;

  insn = get_last_insn ();
  RTX_FRAME_RELATED_P (insn) = 1;
  REG_NOTES (insn) = alloc_EXPR_LIST (REG_FRAME_RELATED_EXPR,
				      frame_pattern,
				      REG_NOTES (insn));
}

/* Return a frame-related rtx that stores REG at MEM.
   REG must be a single register.  */

static rtx
riscv_frame_set (rtx mem, rtx reg)
{
  rtx set = gen_rtx_SET (mem, reg);
  RTX_FRAME_RELATED_P (set) = 1;
  return set;
}

/* Return true if the current function must save register REGNO.  */

static bool
riscv_save_reg_p (unsigned int regno)
{
  bool call_saved = !global_regs[regno] && !call_really_used_regs[regno];
  bool might_clobber = crtl->saves_all_registers
		       || df_regs_ever_live_p (regno);

  if (call_saved && might_clobber)
    return true;

  if (regno == HARD_FRAME_POINTER_REGNUM && frame_pointer_needed)
    return true;

  if (regno == RETURN_ADDR_REGNUM && crtl->calls_eh_return)
    return true;

  return false;
}

/* Determine whether to call GPR save/restore routines.  */
static bool
riscv_use_save_libcall (const struct riscv_frame_info *frame)
{
  if (!TARGET_SAVE_RESTORE || crtl->calls_eh_return || frame_pointer_needed)
    return false;

  /* The save/restore libcalls don't contain DWARF information at this time,
     so do not emit them if we are emitting DWARF frame info.  */
  if (dwarf2out_do_frame ())
    return false;

  return frame->save_libcall_adjustment != 0;
}

/* Determine which GPR save/restore routine to call.  */

static unsigned
riscv_save_libcall_count (unsigned mask)
{
  for (unsigned n = GP_REG_LAST; n > GP_REG_FIRST; n--)
    if (BITSET_P (mask, n))
      return CALLEE_SAVED_REG_NUMBER (n) + 1;
  abort ();
}

/* Populate the current function's riscv_frame_info structure.

   RISC-V stack frames grown downward.  High addresses are at the top.

	+-------------------------------+
	|                               |
	|  incoming stack arguments     |
	|                               |
	+-------------------------------+ <-- incoming stack pointer
	|                               |
	|  callee-allocated save area   |
	|  for arguments that are       |
	|  split between registers and  |
	|  the stack                    |
	|                               |
	+-------------------------------+ <-- arg_pointer_rtx
	|                               |
	|  callee-allocated save area   |
	|  for register varargs         |
	|                               |
	+-------------------------------+ <-- hard_frame_pointer_rtx;
	|                               |     stack_pointer_rtx + gp_sp_offset
	|  GPR save area                |       + UNITS_PER_WORD
	|                               |
	+-------------------------------+ <-- stack_pointer_rtx + fp_sp_offset
	|                               |       + UNITS_PER_HWVALUE
	|  FPR save area                |
	|                               |
	+-------------------------------+ <-- frame_pointer_rtx (virtual)
	|                               |
	|  local variables              |
	|                               |
      P +-------------------------------+
	|                               |
	|  outgoing stack arguments     |
	|                               |
	+-------------------------------+ <-- stack_pointer_rtx

   Dynamic stack allocations such as alloca insert data at point P.
   They decrease stack_pointer_rtx but leave frame_pointer_rtx and
   hard_frame_pointer_rtx unchanged.  */

static void
riscv_compute_frame_info (void)
{
  struct riscv_frame_info *frame;
  HOST_WIDE_INT offset;
  unsigned int regno, i, num_x_saved = 0, num_f_saved = 0;

  frame = &cfun->machine->frame;
  memset (frame, 0, sizeof (*frame));

  /* Find out which GPRs we need to save.  */
  for (regno = GP_REG_FIRST; regno <= GP_REG_LAST; regno++)
    if (riscv_save_reg_p (regno))
      frame->mask |= 1 << (regno - GP_REG_FIRST), num_x_saved++;

  /* If this function calls eh_return, we must also save and restore the
     EH data registers.  */
  if (crtl->calls_eh_return)
    for (i = 0; (regno = EH_RETURN_DATA_REGNO (i)) != INVALID_REGNUM; i++)
      frame->mask |= 1 << (regno - GP_REG_FIRST), num_x_saved++;

  /* Find out which FPRs we need to save.  This loop must iterate over
     the same space as its companion in riscv_for_each_saved_reg.  */
  if (TARGET_HARD_FLOAT)
    for (regno = FP_REG_FIRST; regno <= FP_REG_LAST; regno++)
      if (riscv_save_reg_p (regno))
	frame->fmask |= 1 << (regno - FP_REG_FIRST), num_f_saved++;

  /* At the bottom of the frame are any outgoing stack arguments. */
  offset = crtl->outgoing_args_size;
  /* Next are local stack variables. */
  offset += RISCV_STACK_ALIGN (get_frame_size ());
  /* The virtual frame pointer points above the local variables. */
  frame->frame_pointer_offset = offset;
  /* Next are the callee-saved FPRs. */
  if (frame->fmask)
    offset += RISCV_STACK_ALIGN (num_f_saved * UNITS_PER_FP_REG);
  frame->fp_sp_offset = offset - UNITS_PER_FP_REG;
  /* Next are the callee-saved GPRs. */
  if (frame->mask)
    {
      unsigned x_save_size = RISCV_STACK_ALIGN (num_x_saved * UNITS_PER_WORD);
      unsigned num_save_restore = 1 + riscv_save_libcall_count (frame->mask);

      /* Only use save/restore routines if they don't alter the stack size.  */
      if (RISCV_STACK_ALIGN (num_save_restore * UNITS_PER_WORD) == x_save_size)
	frame->save_libcall_adjustment = x_save_size;

      offset += x_save_size;
    }
  frame->gp_sp_offset = offset - UNITS_PER_WORD;
  /* The hard frame pointer points above the callee-saved GPRs. */
  frame->hard_frame_pointer_offset = offset;
  /* Above the hard frame pointer is the callee-allocated varags save area. */
  offset += RISCV_STACK_ALIGN (cfun->machine->varargs_size);
  frame->arg_pointer_offset = offset;
  /* Next is the callee-allocated area for pretend stack arguments.  */
  offset += crtl->args.pretend_args_size;
  frame->total_size = offset;
  /* Next points the incoming stack pointer and any incoming arguments. */

  /* Only use save/restore routines when the GPRs are atop the frame.  */
  if (frame->hard_frame_pointer_offset != frame->total_size)
    frame->save_libcall_adjustment = 0;
}

/* Make sure that we're not trying to eliminate to the wrong hard frame
   pointer.  */

static bool
riscv_can_eliminate (const int from ATTRIBUTE_UNUSED, const int to)
{
  return (to == HARD_FRAME_POINTER_REGNUM || to == STACK_POINTER_REGNUM);
}

/* Implement INITIAL_ELIMINATION_OFFSET.  FROM is either the frame pointer
   or argument pointer.  TO is either the stack pointer or hard frame
   pointer.  */

HOST_WIDE_INT
riscv_initial_elimination_offset (int from, int to)
{
  HOST_WIDE_INT src, dest;

  riscv_compute_frame_info ();

  if (to == HARD_FRAME_POINTER_REGNUM)
    dest = cfun->machine->frame.hard_frame_pointer_offset;
  else if (to == STACK_POINTER_REGNUM)
    dest = 0; /* The stack pointer is the base of all offsets, hence 0.  */
  else
    gcc_unreachable ();

  if (from == FRAME_POINTER_REGNUM)
    src = cfun->machine->frame.frame_pointer_offset;
  else if (from == ARG_POINTER_REGNUM)
    src = cfun->machine->frame.arg_pointer_offset;
  else
    gcc_unreachable ();

  return src - dest;
}

/* Implement RETURN_ADDR_RTX.  We do not support moving back to a
   previous frame.  */

rtx
riscv_return_addr (int count, rtx frame ATTRIBUTE_UNUSED)
{
  if (count != 0)
    return const0_rtx;

  return get_hard_reg_initial_val (Pmode, RETURN_ADDR_REGNUM);
}

/* Emit code to change the current function's return address to
   ADDRESS.  SCRATCH is available as a scratch register, if needed.
   ADDRESS and SCRATCH are both word-mode GPRs.  */

void
riscv_set_return_address (rtx address, rtx scratch)
{
  rtx slot_address;

  gcc_assert (BITSET_P (cfun->machine->frame.mask, RETURN_ADDR_REGNUM));
  slot_address = riscv_add_offset (scratch, stack_pointer_rtx,
				  cfun->machine->frame.gp_sp_offset);
  riscv_emit_move (gen_frame_mem (GET_MODE (address), slot_address), address);
}

/* A function to save or store a register.  The first argument is the
   register and the second is the stack slot.  */
typedef void (*riscv_save_restore_fn) (rtx, rtx);

/* Use FN to save or restore register REGNO.  MODE is the register's
   mode and OFFSET is the offset of its save slot from the current
   stack pointer.  */

static void
riscv_save_restore_reg (enum machine_mode mode, int regno,
		       HOST_WIDE_INT offset, riscv_save_restore_fn fn)
{
  rtx mem;

  mem = gen_frame_mem (mode, plus_constant (Pmode, stack_pointer_rtx, offset));
  fn (gen_rtx_REG (mode, regno), mem);
}

/* Call FN for each register that is saved by the current function.
   SP_OFFSET is the offset of the current stack pointer from the start
   of the frame.  */

static void
riscv_for_each_saved_reg (HOST_WIDE_INT sp_offset, riscv_save_restore_fn fn)
{
  HOST_WIDE_INT offset;

  /* Save the link register and s-registers. */
  offset = cfun->machine->frame.gp_sp_offset - sp_offset;
  for (int regno = GP_REG_FIRST; regno <= GP_REG_LAST-1; regno++)
    if (BITSET_P (cfun->machine->frame.mask, regno - GP_REG_FIRST))
      {
	riscv_save_restore_reg (word_mode, regno, offset, fn);
	offset -= UNITS_PER_WORD;
      }

  /* This loop must iterate over the same space as its companion in
     riscv_compute_frame_info.  */
  offset = cfun->machine->frame.fp_sp_offset - sp_offset;
  for (int regno = FP_REG_FIRST; regno <= FP_REG_LAST; regno++)
    if (BITSET_P (cfun->machine->frame.fmask, regno - FP_REG_FIRST))
      {
	enum machine_mode mode = TARGET_DOUBLE_FLOAT ? DFmode : SFmode;

	riscv_save_restore_reg (mode, regno, offset, fn);
	offset -= GET_MODE_SIZE (mode);
      }
}

/* Save register REG to MEM.  Make the instruction frame-related.  */

static void
riscv_save_reg (rtx reg, rtx mem)
{
  riscv_emit_move (mem, reg);
  riscv_set_frame_expr (riscv_frame_set (mem, reg));
}

/* Restore register REG from MEM.  */

static void
riscv_restore_reg (rtx reg, rtx mem)
{
  rtx insn = riscv_emit_move (reg, mem);
  rtx dwarf = NULL_RTX;
  dwarf = alloc_reg_note (REG_CFA_RESTORE, reg, dwarf);
  REG_NOTES (insn) = dwarf;

  RTX_FRAME_RELATED_P (insn) = 1;
}

/* Return the code to invoke the GPR save routine.  */

const char *
riscv_output_gpr_save (unsigned mask)
{
  static char s[32];
  unsigned n = riscv_save_libcall_count (mask);

  ssize_t bytes = snprintf (s, sizeof (s), "call\tt0,__riscv_save_%u", n);
  gcc_assert ((size_t) bytes < sizeof (s));

  return s;
}

void
riscv_xscen_stack_prologue (void)
{
  struct riscv_frame_info *frame = &cfun->machine->frame;
  if (TARGET_XSCEN_DEBUG)
    fprintf(stderr, "riscv_xscen_stack_prologue called %d\n", crtl->args.size);
  HOST_WIDE_INT size = frame->total_size;
  emit_insn(gen_xscen_new_stack_storage_region(GEN_INT(size), GEN_INT(crtl->args.size)));
}

void
riscv_xscen_call_site_delegate (unsigned int uid)
{
  const char *name = current_function_name();
  if (TARGET_XSCEN_DEBUG) fprintf(stderr, "riscv_xscen_call_site_delegate: staring for function %s with uid %d\n", name, uid);

  struct xscen_delegation **dlg = NULL;
  //find the function declaration for UID
  int i;
  for (i = 0; i < cfun->machine->total_callees; i++) {
    if (cfun->machine->callee_uids[i] == uid) {
      dlg = cfun->machine->callee_pointer_args[i];
      break;
    }
  }

  if (!dlg) return;

  for (int i = 0, j = 0; i < XSCEN_MAX_ARGS; i++,j++) {
    if (!dlg[i] || dlg[i]->pointer_depth == 0) continue;

    int regno = GP_ARG_FIRST+j;
    rtx test, label, insn, tmp, reg, zero;

    if (TARGET_XSCEN_DEBUG) fprintf(stderr, "param %d in function call to %d in %s will be delegated\n", i, uid, name);

    reg = gen_rtx_REG (Pmode, regno);
    label = gen_label_rtx();
    zero = GEN_INT(0);
    test = gen_rtx_EQ(VOIDmode, reg, zero);
    emit_jump_insn(gen_cbranchsi4(test, reg, zero, label));

    if (dlg[i]->subregion) {
      HOST_WIDE_INT subsize = dlg[i]->subsize;
      if (TARGET_XSCEN_DEBUG) fprintf(stderr, "   marked as subregion of size %ld\n", subsize);

      insn = gen_xscen_delegate_subregion(reg, reg, GEN_INT(subsize-1));
      emit_insn(insn);
      //insn = gen_xscen_delegate_move(reg, GEN_INT(0));
      //emit_insn(insn);
    } else {
      if (TARGET_XSCEN_DEBUG) fprintf(stderr, "   not a subregion, normal delegate\n");

      insn = gen_xscen_delegate(reg, GEN_INT(0));
      emit_insn(insn);
    }

    if (dlg[i]->pointer_depth == 2) {
      if (TARGET_XSCEN_DEBUG) fprintf(stderr, "   pointer depth was 2\n");

      tmp = gen_rtx_REG (Pmode, GP_TEMP_FIRST+1);
      insn = riscv_emit_move(tmp, gen_rtx_MEM(Pmode, reg));
      insn = gen_xscen_delegate(tmp, GEN_INT(0));
      emit_insn(insn);
    }

    for (unsigned int k=0; k < dlg[i]->struct_pointers; k++) {
      unsigned int offset = dlg[i]->offsets[k];
      if (TARGET_XSCEN_DEBUG) fprintf(stderr, "   doing struct pointer %d offset=%d\n", k, offset);

      rtx addr;
      tmp = gen_rtx_REG (Pmode, GP_TEMP_FIRST+1);
      addr = riscv_add_offset(tmp, reg, offset);
      riscv_emit_move(tmp, gen_rtx_MEM(Pmode, addr));
      insn = gen_xscen_delegate(tmp, GEN_INT(0));
      emit_insn(insn);
    }
    emit_label (label);
  }
}

/* For stack frames that can't be allocated with a single ADDI instruction,
   compute the best value to initially allocate.  It must at a minimum
   allocate enough space to spill the callee-saved registers.  */

static HOST_WIDE_INT
riscv_first_stack_step (struct riscv_frame_info *frame)
{
  HOST_WIDE_INT min_first_step = frame->total_size - frame->fp_sp_offset;
  HOST_WIDE_INT max_first_step = IMM_REACH / 2 - STACK_BOUNDARY / 8;

  if (SMALL_OPERAND (frame->total_size))
    return frame->total_size;

  /* As an optimization, use the least-significant bits of the total frame
     size, so that the second adjustment step is just LUI + ADD.  */
  if (!SMALL_OPERAND (frame->total_size - max_first_step)
      && frame->total_size % IMM_REACH < IMM_REACH / 2
      && frame->total_size % IMM_REACH >= min_first_step)
    return frame->total_size % IMM_REACH;

  gcc_assert (min_first_step <= max_first_step);
  return max_first_step;
}

/* Expand the "prologue" pattern.  */

void
riscv_expand_prologue (void)
{
  struct riscv_frame_info *frame = &cfun->machine->frame;
  HOST_WIDE_INT size = frame->total_size;
  unsigned mask = frame->mask;
  rtx insn;

  //xscen
  if (TARGET_XSCEN_ENABLED) {
    riscv_xscen_prepare_prologue();
  }

  if (flag_stack_usage_info)
    current_function_static_stack_size = size;

  /* When optimizing for size, call a subroutine to save the registers.  */
  if (riscv_use_save_libcall (frame))
    {
      frame->mask = 0; /* Temporarily fib that we need not save GPRs.  */
      size -= frame->save_libcall_adjustment;
      emit_insn (gen_gpr_save (GEN_INT (mask)));
    }

  /* Save the registers.  */
  if ((frame->mask | frame->fmask) != 0)
    {
      HOST_WIDE_INT step1 = riscv_first_stack_step (frame);

      insn = gen_add3_insn (stack_pointer_rtx,
			    stack_pointer_rtx,
			    GEN_INT (-step1));
      RTX_FRAME_RELATED_P (emit_insn (insn)) = 1;
      size -= step1;
      riscv_for_each_saved_reg (size, riscv_save_reg);
    }

  frame->mask = mask; /* Undo the above fib.  */

  /* Set up the frame pointer, if we're using one.  */
  if (frame_pointer_needed)
    {
      insn = gen_add3_insn (hard_frame_pointer_rtx, stack_pointer_rtx,
			    GEN_INT (frame->hard_frame_pointer_offset - size));
      RTX_FRAME_RELATED_P (emit_insn (insn)) = 1;
    }

  /* Allocate the rest of the frame.  */
  if (size > 0)
    {
      if (SMALL_OPERAND (-size))
	{
	  insn = gen_add3_insn (stack_pointer_rtx, stack_pointer_rtx,
				GEN_INT (-size));
	  RTX_FRAME_RELATED_P (emit_insn (insn)) = 1;
	}
      else
	{
	  riscv_emit_move (RISCV_PROLOGUE_TEMP (Pmode), GEN_INT (-size));
	  emit_insn (gen_add3_insn (stack_pointer_rtx,
				    stack_pointer_rtx,
				    RISCV_PROLOGUE_TEMP (Pmode)));

	  /* Describe the effect of the previous instructions.  */
	  insn = plus_constant (Pmode, stack_pointer_rtx, -size);
	  insn = gen_rtx_SET (stack_pointer_rtx, insn);
	  riscv_set_frame_expr (insn);
	}
    }
}

/* Expand an "epilogue" or "sibcall_epilogue" pattern; SIBCALL_P
   says which.  */

void
riscv_expand_epilogue (bool sibcall_p)
{
  /* Split the frame into two.  STEP1 is the amount of stack we should
     deallocate before restoring the registers.  STEP2 is the amount we
     should deallocate afterwards.

     Start off by assuming that no registers need to be restored.  */
  struct riscv_frame_info *frame = &cfun->machine->frame;
  unsigned mask = frame->mask;
  HOST_WIDE_INT step1 = frame->total_size;
  HOST_WIDE_INT step2 = 0;
  bool use_restore_libcall = !sibcall_p && riscv_use_save_libcall (frame);
  rtx ra = gen_rtx_REG (Pmode, RETURN_ADDR_REGNUM);
  rtx insn;

  if (!sibcall_p && riscv_can_use_return_insn ())
    {
      if (TARGET_XSCEN_ENABLED) {
        //xscen
        riscv_xscen_epilogue();
      }

      emit_jump_insn (gen_return ());
      return;
    }

  /* Move past any dynamic stack allocations.  */
  if (cfun->calls_alloca)
    {
      rtx adjust = GEN_INT (-frame->hard_frame_pointer_offset);
      if (!SMALL_OPERAND (INTVAL (adjust)))
	{
	  riscv_emit_move (RISCV_PROLOGUE_TEMP (Pmode), adjust);
	  adjust = RISCV_PROLOGUE_TEMP (Pmode);
	}

      emit_insn (gen_add3_insn (stack_pointer_rtx, hard_frame_pointer_rtx,
				adjust));
    }

  /* If we need to restore registers, deallocate as much stack as
     possible in the second step without going out of range.  */
  if ((frame->mask | frame->fmask) != 0)
    {
      step2 = riscv_first_stack_step (frame);
      step1 -= step2;
    }

  /* Set TARGET to BASE + STEP1.  */
  if (step1 > 0)
    {
      /* Get an rtx for STEP1 that we can add to BASE.  */
      rtx adjust = GEN_INT (step1);
      if (!SMALL_OPERAND (step1))
	{
	  riscv_emit_move (RISCV_PROLOGUE_TEMP (Pmode), adjust);
	  adjust = RISCV_PROLOGUE_TEMP (Pmode);
	}

      insn = emit_insn (
	       gen_add3_insn (stack_pointer_rtx, stack_pointer_rtx, adjust));

      rtx dwarf = NULL_RTX;
      rtx cfa_adjust_rtx = gen_rtx_PLUS (Pmode, stack_pointer_rtx,
				         const0_rtx);
      dwarf = alloc_reg_note (REG_CFA_DEF_CFA, cfa_adjust_rtx, dwarf);
      RTX_FRAME_RELATED_P (insn) = 1;

      REG_NOTES (insn) = dwarf;
    }

  if (use_restore_libcall)
    frame->mask = 0; /* Temporarily fib that we need not save GPRs.  */

  /* Restore the registers.  */
  riscv_for_each_saved_reg (frame->total_size - step2, riscv_restore_reg);

  if (use_restore_libcall)
    {
      frame->mask = mask; /* Undo the above fib.  */
      gcc_assert (step2 >= frame->save_libcall_adjustment);
      step2 -= frame->save_libcall_adjustment;
    }

  /* Deallocate the final bit of the frame.  */
  if (step2 > 0)
    {
      insn = emit_insn (gen_add3_insn (stack_pointer_rtx, stack_pointer_rtx,
				       GEN_INT (step2)));

      rtx dwarf = NULL_RTX;
      rtx cfa_adjust_rtx = gen_rtx_PLUS (Pmode, stack_pointer_rtx,
				         const0_rtx);
      dwarf = alloc_reg_note (REG_CFA_DEF_CFA, cfa_adjust_rtx, dwarf);
      RTX_FRAME_RELATED_P (insn) = 1;

      REG_NOTES (insn) = dwarf;
    }

  if (use_restore_libcall)
    {
      emit_insn (gen_gpr_restore (GEN_INT (riscv_save_libcall_count (mask))));

      if (TARGET_XSCEN_ENABLED) {
        //xscen
        riscv_xscen_epilogue();
      }

      emit_jump_insn (gen_gpr_restore_return (ra));
      return;
    }

  /* Add in the __builtin_eh_return stack adjustment. */
  if (crtl->calls_eh_return)
    emit_insn (gen_add3_insn (stack_pointer_rtx, stack_pointer_rtx,
			      EH_RETURN_STACKADJ_RTX));

  if (!sibcall_p) {
    if (TARGET_XSCEN_ENABLED) {
      //xscen
      riscv_xscen_epilogue();
    }

    emit_jump_insn (gen_simple_return_internal (ra));
  }
}

/* Return nonzero if this function is known to have a null epilogue.
   This allows the optimizer to omit jumps to jumps if no stack
   was created.  */

bool
riscv_can_use_return_insn (void)
{
  return reload_completed && cfun->machine->frame.total_size == 0;
}

/* Implement TARGET_REGISTER_MOVE_COST.  */

static int
riscv_register_move_cost (enum machine_mode mode,
			  reg_class_t from, reg_class_t to)
{
  return SECONDARY_MEMORY_NEEDED (from, to, mode) ? 8 : 1;
}

/* Return true if register REGNO can store a value of mode MODE.  */

bool
riscv_hard_regno_mode_ok_p (unsigned int regno, enum machine_mode mode)
{
  unsigned int size = GET_MODE_SIZE (mode);

  if (GP_REG_P (regno))
    {
      if (size <= UNITS_PER_WORD)
	return true;

      /* Double-word values must be even-register-aligned.  */
      if (size <= 2 * UNITS_PER_WORD)
	return regno % 2 == 0;

      /* For RV32, treat CDImode like two doublewords, but forbid allocating
	 them in x30, since they need 4 consecutive registers.  */
      if (size <= 4 * UNITS_PER_WORD)
	return regno % 2 == 0 && GP_REG_P (regno + 2);
    }

  if (FP_REG_P (regno))
    {
      unsigned max_size = UNITS_PER_FP_REG;

      /* Only use callee-saved registers if a potential callee is guaranteed
	 to spill the requisite width.  */
      if (UNITS_PER_FP_ARG < UNITS_PER_FP_REG && !call_used_regs[regno])
	max_size = UNITS_PER_FP_ARG;

      if (GET_MODE_CLASS (mode) == MODE_FLOAT
	  || GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT)
	return GET_MODE_UNIT_SIZE (mode) <= max_size;
    }

  return false;
}

/* Implement HARD_REGNO_NREGS.  */

unsigned int
riscv_hard_regno_nregs (int regno, enum machine_mode mode)
{
  if (FP_REG_P (regno))
    return (GET_MODE_SIZE (mode) + UNITS_PER_FP_REG - 1) / UNITS_PER_FP_REG;

  /* All other registers are word-sized.  */
  return (GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
}

/* Implement CLASS_MAX_NREGS.  */

static unsigned char
riscv_class_max_nregs (reg_class_t rclass, enum machine_mode mode)
{
  if (reg_class_subset_p (FP_REGS, rclass))
    return riscv_hard_regno_nregs (FP_REG_FIRST, mode);

  if (reg_class_subset_p (GR_REGS, rclass))
    return riscv_hard_regno_nregs (GP_REG_FIRST, mode);

  return 0;
}

/* Implement TARGET_PREFERRED_RELOAD_CLASS.  */

static reg_class_t
riscv_preferred_reload_class (rtx x ATTRIBUTE_UNUSED, reg_class_t rclass)
{
  return reg_class_subset_p (FP_REGS, rclass) ? FP_REGS :
	 reg_class_subset_p (GR_REGS, rclass) ? GR_REGS :
	 rclass;
}

/* Implement TARGET_MEMORY_MOVE_COST.  */

static int
riscv_memory_move_cost (enum machine_mode mode, reg_class_t rclass, bool in)
{
  return (tune_info->memory_cost
	  + memory_move_secondary_cost (mode, rclass, in));
}

/* Implement TARGET_MODE_REP_EXTENDED.  */

static int
riscv_mode_rep_extended (enum machine_mode mode, enum machine_mode mode_rep)
{
  /* On 64-bit targets, SImode register values are sign-extended to DImode.  */
  if (TARGET_64BIT && mode == SImode && mode_rep == DImode)
    return SIGN_EXTEND;

  return UNKNOWN;
}

/* Implement TARGET_SCALAR_MODE_SUPPORTED_P.  */

static bool
riscv_scalar_mode_supported_p (enum machine_mode mode)
{
  if (ALL_FIXED_POINT_MODE_P (mode)
      && GET_MODE_PRECISION (mode) <= 2 * BITS_PER_WORD)
    return true;

  return default_scalar_mode_supported_p (mode);
}

/* Return the number of instructions that can be issued per cycle.  */

static int
riscv_issue_rate (void)
{
  return tune_info->issue_rate;
}

/* Implement TARGET_ASM_FILE_START.  */

static void
riscv_file_start (void)
{
  default_file_start ();

  /* Instruct GAS to generate position-[in]dependent code.  */
  fprintf (asm_out_file, "\t.option %spic\n", (flag_pic ? "" : "no"));
}

/* Implement TARGET_ASM_OUTPUT_MI_THUNK.  Generate rtl rather than asm text
   in order to avoid duplicating too much logic from elsewhere.  */

static void
riscv_output_mi_thunk (FILE *file, tree thunk_fndecl ATTRIBUTE_UNUSED,
		      HOST_WIDE_INT delta, HOST_WIDE_INT vcall_offset,
		      tree function)
{
  rtx this_rtx, temp1, temp2, fnaddr;
  rtx_insn *insn;
  bool use_sibcall_p;

  /* Pretend to be a post-reload pass while generating rtl.  */
  reload_completed = 1;

  /* Mark the end of the (empty) prologue.  */
  emit_note (NOTE_INSN_PROLOGUE_END);

  /* Determine if we can use a sibcall to call FUNCTION directly.  */
  fnaddr = XEXP (DECL_RTL (function), 0);
  use_sibcall_p = absolute_symbolic_operand (fnaddr, Pmode);

  /* We need two temporary registers in some cases.  */
  temp1 = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP_REGNUM);
  temp2 = gen_rtx_REG (Pmode, STATIC_CHAIN_REGNUM);

  /* Find out which register contains the "this" pointer.  */
  if (aggregate_value_p (TREE_TYPE (TREE_TYPE (function)), function))
    this_rtx = gen_rtx_REG (Pmode, GP_ARG_FIRST + 1);
  else
    this_rtx = gen_rtx_REG (Pmode, GP_ARG_FIRST);

  /* Add DELTA to THIS_RTX.  */
  if (delta != 0)
    {
      rtx offset = GEN_INT (delta);
      if (!SMALL_OPERAND (delta))
	{
	  riscv_emit_move (temp1, offset);
	  offset = temp1;
	}
      emit_insn (gen_add3_insn (this_rtx, this_rtx, offset));
    }

  /* If needed, add *(*THIS_RTX + VCALL_OFFSET) to THIS_RTX.  */
  if (vcall_offset != 0)
    {
      rtx addr;

      /* Set TEMP1 to *THIS_RTX.  */
      riscv_emit_move (temp1, gen_rtx_MEM (Pmode, this_rtx));

      /* Set ADDR to a legitimate address for *THIS_RTX + VCALL_OFFSET.  */
      addr = riscv_add_offset (temp2, temp1, vcall_offset);

      /* Load the offset and add it to THIS_RTX.  */
      riscv_emit_move (temp1, gen_rtx_MEM (Pmode, addr));
      emit_insn (gen_add3_insn (this_rtx, this_rtx, temp1));
    }

  /* Jump to the target function.  Use a sibcall if direct jumps are
     allowed, otherwise load the address into a register first.  */
  if (use_sibcall_p)
    {
      insn = emit_call_insn (gen_sibcall_internal (fnaddr, const0_rtx));
      SIBLING_CALL_P (insn) = 1;
    }
  else
    {
      riscv_emit_move (temp1, fnaddr);
      emit_jump_insn (gen_indirect_jump (temp1));
    }

  /* Run just enough of rest_of_compilation.  This sequence was
     "borrowed" from alpha.c.  */
  insn = get_insns ();
  split_all_insns_noflow ();
  shorten_branches (insn);
  final_start_function (insn, file, 1);
  final (insn, file, 1);
  final_end_function ();

  /* Clean up the vars set above.  Note that final_end_function resets
     the global pointer for us.  */
  reload_completed = 0;
}

/* Allocate a chunk of memory for per-function machine-dependent data.  */

static struct machine_function *
riscv_init_machine_status (void)
{
  struct machine_function *mf = ggc_cleared_alloc<machine_function> ();
  mf->unknown_callee = 1;
  mf->returns_pointer = 0;
  return mf;
}

/* Implement TARGET_OPTION_OVERRIDE.  */

static void
riscv_option_override (void)
{
  const struct riscv_cpu_info *cpu;

#ifdef SUBTARGET_OVERRIDE_OPTIONS
  SUBTARGET_OVERRIDE_OPTIONS;
#endif

  flag_pcc_struct_return = 0;

  if (flag_pic)
    g_switch_value = 0;

  /* The presence of the M extension implies that division instructions
     are present, so include them unless explicitly disabled.  */
  if (TARGET_MUL && (target_flags_explicit & MASK_DIV) == 0)
    target_flags |= MASK_DIV;
  else if (!TARGET_MUL && TARGET_DIV)
    error ("-mdiv requires -march to subsume the `M' extension");

  /* Likewise floating-point division and square root.  */
  if (TARGET_HARD_FLOAT && (target_flags_explicit & MASK_FDIV) == 0)
    target_flags |= MASK_FDIV;

  /* Prefer a call to memcpy over inline code when optimizing for size,
     though see MOVE_RATIO in riscv.h.  */
  if (optimize_size && (target_flags_explicit & MASK_MEMCPY) == 0)
    target_flags |= MASK_MEMCPY;

  /* Handle -mtune.  */
  cpu = riscv_parse_cpu (riscv_tune_string ? riscv_tune_string :
			 RISCV_TUNE_STRING_DEFAULT);
  tune_info = optimize_size ? &optimize_size_tune_info : cpu->tune_info;

  /* If the user hasn't specified a branch cost, use the processor's
     default.  */
  if (riscv_branch_cost == 0)
    riscv_branch_cost = tune_info->branch_cost;

  /* Function to allocate machine-dependent function status.  */
  init_machine_status = &riscv_init_machine_status;

  if (flag_pic)
    riscv_cmodel = CM_PIC;

  /* We get better code with explicit relocs for CM_MEDLOW, but
     worse code for the others (for now).  Pick the best default.  */
  if ((target_flags_explicit & MASK_EXPLICIT_RELOCS) == 0)
    if (riscv_cmodel == CM_MEDLOW)
      target_flags |= MASK_EXPLICIT_RELOCS;

  /* Require that the ISA supports the requested floating-point ABI.  */
  if (UNITS_PER_FP_ARG > (TARGET_HARD_FLOAT ? UNITS_PER_FP_REG : 0))
    error ("requested ABI requires -march to subsume the `%c' extension",
	   UNITS_PER_FP_ARG > 8 ? 'Q' : (UNITS_PER_FP_ARG > 4 ? 'D' : 'F'));

  /* We do not yet support ILP32 on RV64.  */
  if (BITS_PER_WORD != POINTER_SIZE)
    error ("ABI requires -march=rv%d", POINTER_SIZE);
}

/* Implement TARGET_CONDITIONAL_REGISTER_USAGE.  */

static void
riscv_conditional_register_usage (void)
{
  if (!TARGET_HARD_FLOAT)
    {
      for (int regno = FP_REG_FIRST; regno <= FP_REG_LAST; regno++)
	fixed_regs[regno] = call_used_regs[regno] = 1;
    }
}

/* Return a register priority for hard reg REGNO.  */

static int
riscv_register_priority (int regno)
{
  /* Favor x8-x15/f8-f15 to improve the odds of RVC instruction selection.  */
  if (TARGET_RVC && (IN_RANGE (regno, GP_REG_FIRST + 8, GP_REG_FIRST + 15)
		     || IN_RANGE (regno, FP_REG_FIRST + 8, FP_REG_FIRST + 15)))
    return 1;

  return 0;
}

/* Implement TARGET_TRAMPOLINE_INIT.  */

static void
riscv_trampoline_init (rtx m_tramp, tree fndecl, rtx chain_value)
{
  rtx addr, end_addr, mem;
  uint32_t trampoline[4];
  unsigned int i;
  HOST_WIDE_INT static_chain_offset, target_function_offset;

  /* Work out the offsets of the pointers from the start of the
     trampoline code.  */
  gcc_assert (ARRAY_SIZE (trampoline) * 4 == TRAMPOLINE_CODE_SIZE);
  static_chain_offset = TRAMPOLINE_CODE_SIZE;
  target_function_offset = static_chain_offset + GET_MODE_SIZE (ptr_mode);

  /* Get pointers to the beginning and end of the code block.  */
  addr = force_reg (Pmode, XEXP (m_tramp, 0));
  end_addr = riscv_force_binary (Pmode, PLUS, addr, GEN_INT (TRAMPOLINE_CODE_SIZE));

  /* auipc   t0, 0
     l[wd]   t1, target_function_offset(t0)
     l[wd]   t0, static_chain_offset(t0)
     jr      t1
  */
  trampoline[0] = OPCODE_AUIPC | (STATIC_CHAIN_REGNUM << SHIFT_RD);
  trampoline[1] = (Pmode == DImode ? OPCODE_LD : OPCODE_LW)
		  | (RISCV_PROLOGUE_TEMP_REGNUM << SHIFT_RD)
		  | (STATIC_CHAIN_REGNUM << SHIFT_RS1)
		  | (target_function_offset << SHIFT_IMM);
  trampoline[2] = (Pmode == DImode ? OPCODE_LD : OPCODE_LW)
		  | (STATIC_CHAIN_REGNUM << SHIFT_RD)
		  | (STATIC_CHAIN_REGNUM << SHIFT_RS1)
		  | (static_chain_offset << SHIFT_IMM);
  trampoline[3] = OPCODE_JALR | (RISCV_PROLOGUE_TEMP_REGNUM << SHIFT_RS1);

  /* Copy the trampoline code.  */
  for (i = 0; i < ARRAY_SIZE (trampoline); i++)
    {
      mem = adjust_address (m_tramp, SImode, i * GET_MODE_SIZE (SImode));
      riscv_emit_move (mem, gen_int_mode (trampoline[i], SImode));
    }

  /* Set up the static chain pointer field.  */
  mem = adjust_address (m_tramp, ptr_mode, static_chain_offset);
  riscv_emit_move (mem, chain_value);

  /* Set up the target function field.  */
  mem = adjust_address (m_tramp, ptr_mode, target_function_offset);
  riscv_emit_move (mem, XEXP (DECL_RTL (fndecl), 0));

  /* Flush the code part of the trampoline.  */
  emit_insn (gen_add3_insn (end_addr, addr, GEN_INT (TRAMPOLINE_SIZE)));
  emit_insn (gen_clear_cache (addr, end_addr));
}

/* Return leaf_function_p () and memoize the result.  */

static bool
riscv_leaf_function_p (void)
{
  if (cfun->machine->is_leaf == 0)
    cfun->machine->is_leaf = leaf_function_p () ? 1 : -1;

  return cfun->machine->is_leaf > 0;
}

/* Implement TARGET_FUNCTION_OK_FOR_SIBCALL.  */

static bool
riscv_function_ok_for_sibcall (tree decl ATTRIBUTE_UNUSED,
			       tree exp ATTRIBUTE_UNUSED)
{
  /* When optimzing for size, don't use sibcalls in non-leaf routines */
  if (TARGET_SAVE_RESTORE)
    return riscv_leaf_function_p ();

  return true;
}

/* Return true if INSN should not be copied.  */

static bool
riscv_cannot_copy_insn_p (rtx_insn *insn)
{
  return recog_memoized (insn) >= 0 && get_attr_cannot_copy (insn);
}

/* Initialize the GCC target structure.  */
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.half\t"
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\t.word\t"
#undef TARGET_ASM_ALIGNED_DI_OP
#define TARGET_ASM_ALIGNED_DI_OP "\t.dword\t"

#undef TARGET_OPTION_OVERRIDE
#define TARGET_OPTION_OVERRIDE riscv_option_override

#undef TARGET_LEGITIMIZE_ADDRESS
#define TARGET_LEGITIMIZE_ADDRESS riscv_legitimize_address

#undef TARGET_SCHED_ISSUE_RATE
#define TARGET_SCHED_ISSUE_RATE riscv_issue_rate

#undef TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL riscv_function_ok_for_sibcall

#undef TARGET_REGISTER_MOVE_COST
#define TARGET_REGISTER_MOVE_COST riscv_register_move_cost
#undef TARGET_MEMORY_MOVE_COST
#define TARGET_MEMORY_MOVE_COST riscv_memory_move_cost
#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS riscv_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST riscv_address_cost

#undef  TARGET_PREFERRED_RELOAD_CLASS
#define TARGET_PREFERRED_RELOAD_CLASS riscv_preferred_reload_class

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START riscv_file_start
#undef TARGET_ASM_FILE_START_FILE_DIRECTIVE
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true

#undef TARGET_EXPAND_BUILTIN_VA_START
#define TARGET_EXPAND_BUILTIN_VA_START riscv_va_start

#undef  TARGET_PROMOTE_FUNCTION_MODE
#define TARGET_PROMOTE_FUNCTION_MODE default_promote_function_mode_always_promote

#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY riscv_return_in_memory

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK riscv_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK hook_bool_const_tree_hwi_hwi_const_tree_true

#undef TARGET_PRINT_OPERAND
#define TARGET_PRINT_OPERAND riscv_print_operand
#undef TARGET_PRINT_OPERAND_ADDRESS
#define TARGET_PRINT_OPERAND_ADDRESS riscv_print_operand_address

#undef TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS riscv_setup_incoming_varargs
#undef TARGET_STRICT_ARGUMENT_NAMING
#define TARGET_STRICT_ARGUMENT_NAMING hook_bool_CUMULATIVE_ARGS_true
#undef TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK must_pass_in_stack_var_size
#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE riscv_pass_by_reference
#undef TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES riscv_arg_partial_bytes
#undef TARGET_FUNCTION_ARG
#define TARGET_FUNCTION_ARG riscv_function_arg
#undef TARGET_FUNCTION_ARG_ADVANCE
#define TARGET_FUNCTION_ARG_ADVANCE riscv_function_arg_advance
#undef TARGET_FUNCTION_ARG_BOUNDARY
#define TARGET_FUNCTION_ARG_BOUNDARY riscv_function_arg_boundary

#undef TARGET_MODE_REP_EXTENDED
#define TARGET_MODE_REP_EXTENDED riscv_mode_rep_extended

#undef TARGET_SCALAR_MODE_SUPPORTED_P
#define TARGET_SCALAR_MODE_SUPPORTED_P riscv_scalar_mode_supported_p

#ifdef HAVE_AS_TLS
#undef TARGET_HAVE_TLS
#define TARGET_HAVE_TLS true
#endif

#undef TARGET_CANNOT_FORCE_CONST_MEM
#define TARGET_CANNOT_FORCE_CONST_MEM riscv_cannot_force_const_mem

#undef TARGET_LEGITIMATE_CONSTANT_P
#define TARGET_LEGITIMATE_CONSTANT_P riscv_legitimate_constant_p

#undef TARGET_USE_BLOCKS_FOR_CONSTANT_P
#define TARGET_USE_BLOCKS_FOR_CONSTANT_P hook_bool_mode_const_rtx_true

#ifdef HAVE_AS_DTPRELWORD
#undef TARGET_ASM_OUTPUT_DWARF_DTPREL
#define TARGET_ASM_OUTPUT_DWARF_DTPREL riscv_output_dwarf_dtprel
#endif

#undef TARGET_LEGITIMATE_ADDRESS_P
#define TARGET_LEGITIMATE_ADDRESS_P	riscv_legitimate_address_p

#undef TARGET_CAN_ELIMINATE
#define TARGET_CAN_ELIMINATE riscv_can_eliminate

#undef TARGET_CONDITIONAL_REGISTER_USAGE
#define TARGET_CONDITIONAL_REGISTER_USAGE riscv_conditional_register_usage

#undef TARGET_CLASS_MAX_NREGS
#define TARGET_CLASS_MAX_NREGS riscv_class_max_nregs

#undef TARGET_TRAMPOLINE_INIT
#define TARGET_TRAMPOLINE_INIT riscv_trampoline_init

#undef TARGET_IN_SMALL_DATA_P
#define TARGET_IN_SMALL_DATA_P riscv_in_small_data_p

#undef TARGET_ASM_SELECT_RTX_SECTION
#define TARGET_ASM_SELECT_RTX_SECTION  riscv_elf_select_rtx_section

#undef TARGET_MIN_ANCHOR_OFFSET
#define TARGET_MIN_ANCHOR_OFFSET (-IMM_REACH/2)

#undef TARGET_MAX_ANCHOR_OFFSET
#define TARGET_MAX_ANCHOR_OFFSET (IMM_REACH/2-1)

#undef TARGET_LRA_P
#define TARGET_LRA_P hook_bool_void_true

#undef TARGET_REGISTER_PRIORITY
#define TARGET_REGISTER_PRIORITY riscv_register_priority

#undef TARGET_CANNOT_COPY_INSN_P
#define TARGET_CANNOT_COPY_INSN_P riscv_cannot_copy_insn_p

#undef TARGET_FUNCTION_VALUE
#define TARGET_FUNCTION_VALUE riscv_target_function_value


struct gcc_target targetm = TARGET_INITIALIZER;

#include "gt-riscv.h"
