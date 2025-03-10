;; Peephole optimizations for RISC-V for GNU compiler.
;; Copyright (C) 2011-2016 Free Software Foundation, Inc.
;; Contributed by Andrew Waterman (andrew@sifive.com).

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 3, or (at your option)
;; any later version.

;; GCC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING3.  If not see
;; <http://www.gnu.org/licenses/>.


;;........................
;; DI -> SI optimizations
;;........................

;; Simplify (int)(a + 1), etc.
(define_peephole2
  [(set (match_operand:DI 0 "register_operand")
	(match_operator:DI 4 "modular_operator"
	  [(match_operand:DI 1 "register_operand")
	   (match_operand:DI 2 "arith_operand")]))
   (set (match_operand:SI 3 "register_operand")
	(truncate:SI (match_dup 0)))]
  "TARGET_64BIT && (REGNO (operands[0]) == REGNO (operands[3]) || peep2_reg_dead_p (2, operands[0]))
   && (GET_CODE (operands[4]) != ASHIFT || (CONST_INT_P (operands[2]) && INTVAL (operands[2]) < 32))"
  [(set (match_dup 3)
	  (truncate:SI
	     (match_op_dup:DI 4
	       [(match_operand:DI 1 "register_operand")
		(match_operand:DI 2 "arith_operand")])))])

;; Simplify (int)a + 1, etc.
(define_peephole2
  [(set (match_operand:SI 0 "register_operand")
	(truncate:SI (match_operand:DI 1 "register_operand")))
   (set (match_operand:SI 3 "register_operand")
	(match_operator:SI 4 "modular_operator"
	  [(match_dup 0)
	   (match_operand:SI 2 "arith_operand")]))]
  "TARGET_64BIT && (REGNO (operands[0]) == REGNO (operands[3]) || peep2_reg_dead_p (2, operands[0]))"
  [(set (match_dup 3)
	(match_op_dup:SI 4 [(match_dup 1) (match_dup 2)]))])

;; Simplify -(int)a, etc.
(define_peephole2
  [(set (match_operand:SI 0 "register_operand")
	(truncate:SI (match_operand:DI 2 "register_operand")))
   (set (match_operand:SI 3 "register_operand")
	(match_operator:SI 4 "modular_operator"
	  [(match_operand:SI 1 "reg_or_0_operand")
	   (match_dup 0)]))]
  "TARGET_64BIT && (REGNO (operands[0]) == REGNO (operands[3]) || peep2_reg_dead_p (2, operands[0]))"
  [(set (match_dup 3)
	(match_op_dup:SI 4 [(match_dup 1) (match_dup 2)]))])

;; Simplify (unsigned long)(unsigned int)a << const
(define_peephole2
  [(set (match_operand:DI 0 "register_operand")
	(ashift:DI (match_operand:DI 1 "register_operand")
		   (match_operand 2 "const_int_operand")))
   (set (match_operand:DI 3 "register_operand")
	(lshiftrt:DI (match_dup 0) (match_dup 2)))
   (set (match_operand:DI 4 "register_operand")
	(ashift:DI (match_dup 3) (match_operand 5 "const_int_operand")))]
  "TARGET_64BIT
   && INTVAL (operands[5]) < INTVAL (operands[2])
   && (REGNO (operands[3]) == REGNO (operands[4])
       || peep2_reg_dead_p (3, operands[3]))"
  [(set (match_dup 0)
	(ashift:DI (match_dup 1) (match_dup 2)))
   (set (match_dup 4)
	(lshiftrt:DI (match_dup 0) (match_operand 5)))]
{
  operands[5] = GEN_INT (INTVAL (operands[2]) - INTVAL (operands[5]));
})
