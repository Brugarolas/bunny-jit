# Bunny JIT

This is a tiny optimising SSA-based JIT backend, currently targeting x64, but
designed to be (somewhat) portable. The Makefile expects a Unix-like system,
but the code itself should (hopefully) work on Windows as well.

This is work in relatively early progress. It sort of works, but some things like
function calls not done robustly yet. Please don't use it for production yet.

Features:
  * small and simple, yet tries to avoid being naive
  * portable C++11 without dependencies (other than STL; uses `std::vector` to manage memory)
  * uses low-level portable bytecode that models common architectures
  * supports integers and double-floats (other types in the future)
  * end-to-end SSA, with consistency checking and simple interface to generate valid SSA
  * performs DCE, global CSE, constant folding and register allocation (at this point)
  * assembles to native binary code (ready to be copied to executable memory)
  * keeps `valgrind` happy
  
It is intended for situations where it is desirable to create some native code
on the fly (eg. for performance reasons), but including something like LLVM would
be a total overkill.

It comes with some sort of simple front-end language, but this is intended more
for testing (and I guess example) than as a serious programming language.

The test-driver currently parses this simple language from `stdin` and compiles
it into native code, which is written to `out.bin` for disassembly purposes with
something like:
```
gobjdump --insn-width=16 -mi386:x86-64:intel -d -D -b binary out.bin
```

You can certainly run it too, but you'll have to copy it to executable memory.

## License?

I should paste this into every file, but for the time being:

```
/****************************************************************************\
* Bunny-JIT is (c) Copyright pihlaja@signaldust.com 2021                     *
*----------------------------------------------------------------------------*
* You can use and/or redistribute this for whatever purpose, free of charge, *
* provided that the above copyright notice and this permission notice appear *
* in all copies of the software or it's associated documentation.            *
*                                                                            *
* THIS SOFTWARE IS PROVIDED "AS-IS" WITHOUT ANY WARRANTY. USE AT YOUR OWN    *
* RISK. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE HELD LIABLE FOR ANYTHING.  *
\****************************************************************************/
```

## Contributing?

I would recommend opening an issue before working on anything too significant,
because this will greatly increase the chance that your work is still useful
by the time you're ready to send a pull-request.

## Instructions?

The first step is to create a `bjit::Proc` which takes a stack allocation size
and a string representing arguments (`i` for integer, `f` for double).
This will initialize `env[0]` with an SSA value for the pointer to a block
of the requested size on the stack (in practice, it represents stack
pointer) and `env[1..]` as the SSA values of the arguments. More on `env`
below. Pass `0` and `""` if you don't care about allocations or arguments.

To generate instructions, you call the instruction methods on `Proc`.
When done, `Proc::opt()` will optimize and `Proc::compile()` generate code.
Compile always does a few passes of DCE, but otherwise optimization is optional.

Most instructions take their parameters as SSA values. The exceptions are
`lci`/`lcf` which take immediate constants and jump-labels which should be
the block-indexes returned by `Proc::newLabel()`. For instructions
with output values, the methods return the new SSA values and other
instructions return `void`.

`Proc` has a public `std::vector` member `env` which stores the "environment".
When a new label is create with `Proc::newLabel()` the number and types of
incoming arguments to the block are fixed to those contained in `env` and when
jumps are emitted, we check that the contents of `env` are compatible (same
number of values of same types). When `Proc::emitLabel()` is called to generate
code for the label, we replace the contents of `env` with fresh phi-values.
So even though we only handle SSA values, elements of `env` behave essentially
like regular variables (eg. "assignments" can simply store a new SSA value
into `env`). Note that you can adjust the size of `env` as you please as long
as constraints match for jump-sites, but keep in mind that `emitLabel()` will
resize `env` back to what it was at the time of `newLabel()`.

Instructions expect their parameter types to be correct. Passing floating-point
values to instructions that expect integer values or vice versa will result
in undefined behaviour (ie. invalid code or `assert`).
The compiler should never fail with valid data, so we
do not provide error reporting other than `assert`. This is a conscious design
decision, as error checking should be done at higher levels.

The type system is very primitive though and mostly exists for the purpose of
tracking which registers we can use to store values. In particular, anything
stored in general purpose registers is called `_ptr` (or simply integers).

Instructions starting `i` are for integers, `u` are unsigned variants when
there is a distinction and `f` is floating point (though we might change the
double-precision variants to `d` if we add single-precision versions). Note
that floating-point comparisons return integers, even though they expect
`_f64` parameters.

### The compiler currently exposes the following instructions:

`lci i64` and `lcf f64` specify constants, `jmp label` is unconditional jump
and `jz a then else` will branch to `then` if `a` is zero or `else` otherwise,
`iret a` returns from the function with integer value and `fret a` returns with
a floating point value.

`ieq a b` and `ine a b` compare two integers for equality or inequality and
produce `0` or `1`.

`ilt a b`, `ile a b`, `ige a b` and `igt a b` compare signed integers
for less, less-or-equal, greater-or-equal and greater respectively

`ult a b`, `ule a b`, `uge a b` and `ugt a b` perform unsigned comparisons

`feq a b`, `fne a b`, `flt a b`, `fle a b`, `fge a b` and `fgt a b` are
floating point version of the same (still produce integer `0` or `1`).

`iadd a b`, `isub a b` and `imul a b` perform (signed or unsigned) integer
addition, subtraction and multiplication, while `ineg a` negates an integer

`idiv a b` and `imod a b` perform signed division and modulo

`udiv a b` and `umod a b` perform unsigned division and modulo

`inot a`, `iand a b`, `ior a b` and `ixor a b` perform bitwise logical operations

`ishr a b` and `ushr a b` are signed and unsigned right-shift while 
left-shift (signed or unsigned) is `ishl a b` and we specify that the number
of bits to shift is modulo the bitsize of integers (eg. 64 on x64 which does
this natively, but it's easy enough to mask on hardware that might not)

`fadd a b`, `fsub a b`, `fmul a b`, `fdiv a b` and `fneg a` are floating point
versions of arithmetic operations

`cf2i a` converts doubles to integers while `ci2f` converts integers to doubles

`bcf2i a` and `bci2f a` bit-cast (ie. reinterpret) without conversion

`i8 a`, `i16 a` and `i32 a` can be used to sign-extend the low 8/16/32 bits

`u8 a`, `u16 a` and `u32 a` can be used to zero-extend the low 8/16/32 bits

Loads follow the form `lXX ptr imm32` where `ptr` is integer SSA value and `imm32`
is an immediate offset (eg. for field offsets). The variants defined are 
`li8/16/32/64`, `lu8/16/32` and `lf64`. The integer `i` variants sign-extend
while the `u` variants zero-extend.

Stores follow the form `sXX ptr imm32 value` where `ptr` and `imm32` are like loads
while `value` is the SSA value to store. Variants are like loads, but without
the unsigned versions.

Internal we have additional instructions that the fold-engine will use (in the
future the exact set might vary between platforms, so we rely on fold), but they
should be fairly obvious when seen in debug, eg. `jugeI`is a conditional jump on
`uge` comparison with the second operand converted to an `imm32` field.

## What it does?

Well, here's a simple example (already slightly out of data, but it still
serves it's purpose), with full output broken into parts:
```
$ echo '{ x := 0;  while(x < 10) { x = x + 1; } return x; }' | ./bjit
(block 
  (def:0x7fa7f2c04be0:x/0 @1:4 : i64
    i:0 @1:7 : i64)
  (while @1:11 : void
      (c:lt @1:19 : i64
        sym:0x7fa7f2c04be0:x/0 @1:17 : i64
        i:10 @1:21 : i64)
    (block 
      (set @1:29 : i64
        sym:0x7fa7f2c04be0:x/0 @1:27 : i64
        (add @1:33 : i64
          sym:0x7fa7f2c04be0:x/0 @1:31 : i64
          i:1 @1:35 : i64))))
  (return @1:40 : i64
    sym:0x7fa7f2c04be0:x/0 @1:47 : i64)
```
This is the AST dump from the test front-end, which we won't worry about.
The types here are only for the purposes of the front-end.

Next, it dumps the generated bytecode, see "Instructions?" for what
the different operations do and how the `phi` operations are generated.
This debug-format is designed to present a lot of information in a
convenient form, although at this point most of it is still placeholders.
Later compiler passes will fill things in.
```
;----
L0:
 (0000)  0000    ---      lci   0  ptr  i64:0
         0001    ---      jmp           L1
L1:
 (0000)  0002    ---      phi   0  ptr  L0:[0000]:0000 L2:[0000]:0008
 (0000)  0003    ---      lci   0  ptr  i64:10
 (0000)  0004    ---      ilt   0  ptr  ---:0002 ---:0003
         0005    ---       jz           ---:0004 L3 L2
L2:
 (0000)  0006    ---      phi   0  ptr  L1:[0000]:0002
 (0000)  0007    ---      lci   0  ptr  i64:1
 (0000)  0008    ---     iadd   0  ptr  ---:0006 ---:0007
         0009    ---      jmp           L1
L3:
 (0000)  000a    ---      phi   0  ptr  L1:[0000]:0002
         000b    ---     iret           ---:000a
 (0000)  000c    ---      lci   0  ptr  i64:0
         000d    ---     iret           ---:000c
;----
```

Next it iterates DCE and Fold (which also does CSE). When this has converged
we find "Live" variables to basic blocks, solve SCCs (see end of this page)
and then allocate registers. `RA:BB` does basic blocks, `RA:JMP` does shuffles.
```
-- Optimizing:
 DCE DCE DCE Fold:2
 DCE DCE Fold:1
 DCE DCE Live:2
 RA:SCC DCE Live:2
 RA:BB DCE RA:JMP DONE
```

The result then looks like below. `SLOT` is the allocate stack slot
with `ffff` standing for "don't need." If we had any spills, those would
be shown as `=[0012]=` in this field as we don't add instructions for those.

Why is it claiming to have allocated a slot? Because the native calling
convention requires alignment, so the assembler bumps it up to an odd number.

Rest of it should be fairly obvious. The registers in arguments are not
stored with the operations, but the debug-dump pulls them from the 
original definition sites for convenience, just like the assembler will do.
The code is SSA so the definition site always knows the register, or there
is an explicit `rename` (or `reload`) to provide a new definition site.
```
;---- Slots: 1
L0:
; Regs:
; SLOT  VALUE    REG       OP USE TYPE  ARGS
 (ffff)  0000    rax      lci   1  ptr  i64:0
         0001    ---      jmp           L1
; Out: rax:0000

L1: <L2 <L0
; Regs: rax:0002
; SLOT  VALUE    REG       OP USE TYPE  ARGS
 (ffff)  0002    rax      phi   3  ptr  L0:[ffff]:0000 L2:[ffff]:000e
         0005    ---    jiltI           rax:0002 +10 L2 L3
; Out: rax:0002

L3: <L1
; In:  [ffff]:0002
; Regs: rax:0002
; SLOT  VALUE    REG       OP USE TYPE  ARGS
         000b    ---     iret           rax:0002
; Out:

L2: <L1
; In:  [ffff]:0002
; Regs: rax:0002
; SLOT  VALUE    REG       OP USE TYPE  ARGS
 (ffff)  0008    rax    iaddI   1  ptr  rax:0002 +1
 (ffff)  000e    rax       -    1  ptr  rax:0008
         0009    ---      jmp           L1
; Out: rax:000e

;----
 - Wrote out.bin
```

As we can see, we have got rid of most of the silly stuff. Our loop now
simply increments the variable directly (`rax = iaddI rax +1`). 

So what does `out.bin` look like?
```
$ gobjdump --insn-width=16 -mi386:x86-64:intel -d -D -b binary out.bin

out.bin:     file format binary

Disassembly of section .data:

0000000000000000 <.data>:
   0:	48 83 ec 08                                     	sub    rsp,0x8
   4:	33 c0                                           	xor    eax,eax
   6:	48 83 f8 0a                                     	cmp    rax,0xa
   a:	0f 8c 05 00 00 00                               	jl     0x15
  10:	48 83 c4 08                                     	add    rsp,0x8
  14:	c3                                              	ret    
  15:	48 8d 40 01                                     	lea    rax,[rax+0x1]
  19:	e9 e8 ff ff ff                                  	jmp    0x6
  1e:	90                                              	nop
  1f:	90                                              	nop
```

We could certainly do better (eg. unroll the loop and fold it into a
constant return) and the `lea` should probably be `inc` instead (by the
time you are reading this, it probably is; at this point it's still
easy to improve this quite rapidly and I probably won't update this
page all the time), but overall this isn't too bad given the simplicity
of the compiler.

This is obviously a simple example, but the quality of code is generally
roughly similar for more complicated functions as well: not necessarily
the best possible, but still somewhat reasonable. Hope this gives you
and idea what I'm going for with this project.

## SSA?

The backend keeps the code in SSA form from the beginning to the end. The interface
is designed to make emitting SSA directly relatively simple for block-structured
languages by tracking the "environment" that must be passed form one block to
another. When a new label is created, we create phis for all the values in the
environment. When a jump to a label is emitted, we take the current environment
and add the values to the target block phi-alternatives. When a label is emitted
we replace the values in the current environment with the phi-values.

Essentially for a block structured language, whenever a new variable is defined
one pushes the SSA value into the environment. When looking up variables, one
takes the values form the environment. On control-flow constructs, one needs to
match the environment size of the label and the jump-site, but the interface
will take care of the rest.

While there is no need to add temporaries to the environment, always adding phis
for any actual local variables still creates more phis than necessary. We choose
to let DCE clean this up, by simplifying those phis with only one real source.

We keep the SSA structure all the way. The register allocator is currently a bit
lazy with regards to rewriting phi-sources for shuffle-blocks properly, but
theoretically the code is valid SSA even after register allocation. We handle
phis by simply making sure that the phi-functions are no-ops: all jump-sites
place the correct values in either the same registers or stack-slots, depending
on what the phi expects. Two-way jumps always generate shuffle blocks, which are
then jump-threaded if the edge is not actually critical.

The register allocator itself runs locally, using "furthest next use" to choose
which values to throw out of the register file. We don't ever explicitly spill,
rather we flag the source operation with a spill-flag when we emit a reload.
This is always valid in SSA, because we have no variables, only values.
The assembler will then generate stores after any operations marked for spill.

# SCC?

To choose stack locations, we compute what I like to call "stack congruence
classes" (SCCs) to find which values can and/or should be placed into the same
slot. Essentially if two values are live at the same time, then they must have
different SCCs. On the other hand, if a value is argument to a phi, then we
would like to place it into the same class to avoid having to move spilled
values from one stack slot to another. For other values, we would like to try
and find a (nearly) minimal set of SCCs that can be used to hold them, in order
to keep the stack frames as small as possible.

As pointed out earlier, sometimes we have cycles. Eg. if two values are swapped
in a loop every iteration, then we can't allocate the same SCC to these without
potentially forcing a swap in memory. We solve SCCs (but not slots) before
register allocation, so at this point we don't know which variables live in
memory. To make sure the register allocator doesn't need worry about cycles (in
memory; it *can* deal with cycles in actual registers) the SCC computation adds
additional renames to temporary SCCs. This way the register allocator can mark
the rename (rather than the original value) for spill if necessary, otherwise
the renames typically compile to nops. The register allocator itself never adds
any additional SCCs: at that point we already have enough to allow every shuffle
to be trivial.

Only after register allocation is done, do we actually allocate stack slots. At
this point, we find all the SCCs with at least one value actually spilled and
allocate slots for these (and only these), but since we know (by definition)
that no two variables in the same SCC are live at the same time, at this point
we don't need to do anything else. We just rewrite all the SCCs to the slots
allocated (or "don't need" for classes without spills) and pass the total number
of slots to the assembler.

The beauty of this design is that it completely decouples the concerns of stack
layout and register allocation: the latter can pretend that every value has a
stack location, that any value can be reloaded at any time (as long as it's then
marked for "spill" at the time the reload is done) yet we can trivially collapse
the layout afterwards.
