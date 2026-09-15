;; SPDX-License-Identifier: GPL-3.0-or-later
;; ABI version 1: fixed memory, alloc(i32)->i32, transform(i32,i32)->i64.
;; Packed result: unsigned pointer in the high 32 bits, byte length in the low 32.
(module
  (memory (export "memory") 4 4)
  (func (export "alloc") (param $length i32) (result i32)
    (if (i32.gt_u (local.get $length) (i32.const 262144))
      (then unreachable))
    (i32.const 0))
  (func (export "transform") (param $ptr i32) (param $length i32) (result i64)
    (local $offset i32)
    (local $byte i32)
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $offset) (local.get $length)))
        (local.set $byte
          (i32.load8_u (i32.add (local.get $ptr) (local.get $offset))))
        (if
          (i32.and
            (i32.ge_u (local.get $byte) (i32.const 97))
            (i32.le_u (local.get $byte) (i32.const 122)))
          (then
            (i32.store8
              (i32.add (local.get $ptr) (local.get $offset))
              (i32.sub (local.get $byte) (i32.const 32)))))
        (local.set $offset (i32.add (local.get $offset) (i32.const 1)))
        (br $next)))
    (i64.or
      (i64.shl (i64.extend_i32_u (local.get $ptr)) (i64.const 32))
      (i64.extend_i32_u (local.get $length)))))
