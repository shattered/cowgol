Cowgol 2.0
==========


What?
-----

Cowgol is an experimental, Ada-inspired language for very small systems
(6502, Z80, etc). It's different because it's intended to be self-hosted on
these devices: the end goal is to be able to rebuild the entire compiler on
an 8-bit micro, although we're not there yet.

Here's the bullet point list of features:

  - a properly type safe, modern language inspired by Ada

  - the compiler is written in itself and is fully bootstrapped

  - a table-driven, easy to port backend (the 80386 backend is 1.2kloc with
    no other compiler changes needed)

  - tiny: the 80386 Linux compiler binary is 65kB (including ELF overhead).
    The 8080 CP/M compiler 49kB.

  - fast: on my PC it'll compile itself in 360ms (I actually need to look
    into why it's so slow)

  - global analysis: dead code removal and static variable allocation,
    leading to small and efficient binaries

### About the compiler

Right now it's in a state where you can build the cross-compiler on a PC,
then use it to compile the compiler for your selected device, and if it's
small enough to fit use *that* to (slowly and theoretically) compile and
run real programs. Realistically you'll be cross-compiling on a PC.

The following targets are supported. Adding more is easy.

  - Z80 and 8080, on CP/M.

  - 6502 and 65c02, on the BBC Micro with Tube second processor.

  - 80386, on Linux.

  - Generic and terrible C. This produces very big and slow binaries which
    are used for bootstrapping the compiler if you don't have a Cowgol
	compiler.

(It _used_ to [support the Apollo Guidance
Computer](http://cowlark.com/2019-07-20-cowgol-agc/index.html) used in the
Apollo spacecraft, but I had to remove the code generator while rewriting the
compiler and I haven't reworked the AGC backend.)

### About the language

Here's a randomly chosen example pulled from the compiler source.

```
# Free up the node tree rooted in the parameter. This is more exciting than it
# should be because we don't have recursion.
sub Discard(node: [Node])
        var pending := node;
        while pending != (0 as [Node]) loop
                node := pending;
                pending := node.dlink;

                # Unlink and push any children.
                if node.left != (0 as [Node]) then
                        node.left.dlink := pending;
                        pending := node.left;
                end if;
                if node.right != (0 as [Node]) then
                        node.right.dlink := pending;
                        pending := node.right;
                end if;

                # Now free this node.
                Free(node as [uint8]);
        end loop;
end sub;
```

The bullet list set of features is:

  - strongly typed --- no implicit casting (not even between integers of
	different widths of signedness)

  - records, pointers etc

  - subroutines with multiple input and output arguments

  - arbitrarily nested subroutines, with access to variables defined in an
	outer subroutine

  - no recursion and limited stack use (most of the platforms I'm targeting
	don't really support stack frames)

  - byte, word and quad arithmetic for efficient implementation on small
	systems

  - simple type inference of variables if they're assigned during a declaration

There's more about the language in the links below.



Why?
----

I've always been interested in compilers, and have had various other
compiler projects: [the Amsterdam Compiler Kit](http://tack.sourceforge.net/)
and [Cowbel](http://cowlark.com/cowbel/), to name two. (The
[languages](http://cowlark.com/index/languages.html) section of my website
contains a fair number of entries. The oldest compiler which still exists
dates from about 1998.)

Cowgol is based on what I've learnt from all this. It's supposed to be
_useful_, not just a toy. I'm pleasantly surprised by how good the generated
code is; not that it's anything up to that of, say, gcc, but the main code
generation binary of gcc is 23552kB, and Cowgol's is 65kB...



Where?
------

- [Check out the GitHub repository](http://github.com/davidgiven/cowgol) and
  build from source. (Alternatively, you can download a source snapshot from
  [the latest release](https://github.com/davidgiven/cowgol/releases/latest),
  but I suggect the GitHub repositories better because I don't really intend to
  make formal releases often.) [Build instructions are on their own
  page.](doc/building.md)

- [Ask a question by creating a GitHub
  issue](https://github.com/davidgiven/cowgol/issues/new), or just email me
  directly at [dg@cowlark.com](mailto:dg@cowlark.com). (But I'd prefer you
  opened an issue, so other people can see them.)



How?
----

We have documentation! Admittedly, not much of it.

- [How to build and use the compiler](doc/building.md); tl;dr: **read this**.

- [Everything you want to know about Cowgol, the language](doc/language.md);
  tl;dr: very strongly typed; Ada-like syntax; multiple return parameters; no
  recursion; limited aliasing; nested functions.

- [An overview of Cowgol, the toolchain](doc/toolchain.md); tl;dr: single-pass
  compiler frontend; global analyser and linker feeding into a third-party
  assembler; written in pure Cowgol.

- [Frequently Asked Questions](doc/faq.md); tl;dr: random.



Why not?
--------

It's new, it's buggy, it's underdeveloped, and so far only one actual program
is written in Cowgol, which is the Cowgol compiler. (And cowlink and cowwrap.)

Apart from actual bugs, there are some unimplemented parts of the language.

  - no forward declarations of subroutines yet; the compiler doesn't use them.
    (I know how this will work, I just haven't done it.)

  - no seperate compilation yet. cowlink supports it, but cowcom can't define
    external subroutines. This is dependent on the forward declaration support
	above.

  - no subroutine pointers. This one's tricky; because subroutines can be
	nested it's important to be sure that a subroutine can only be called if
	the outer scopes still exist. Ada has a trick for this, but it would
	require quite a lot of compiler work and it's too big as it is.

  - no `null`. This one's semantic, but right now you have to cast `0` to
	pointer types to use `null`. (I _do_ know about languages which don't have
	`null` but they're all for larger machines than Cowgol's aimed at.)

  - no debugging. Well... there's `print()`.

  - no stable standard library. I hack stuff in as I need it.

Your mileage (or kilometreage, depending) may very. You Have Been Warned.



Who?
----

Cowgol was written, entirely so far, by me, David Given. Feel free to send me
email at [dg@cowlark.com](mailto:dg@cowlark.com). You may also [like to visit
my website](http://cowlark.com); there may or may not be something
interesting there.



License?
--------

Cowgol is open source software available [under the 2-clause BSD
license](https://github.com/davidgiven/cowgol/blob/master/COPYING).  Simplified
summary: do what you like with it, just don't claim you wrote it.

The exceptions are the contents of the `third_party` directory, which were
written by other people and are not covered by this license.

`third_party/lib6502` contains a hacked copy of the lib6502 library, which is ©
2005 Ian Plumarta and is available under the terms of the MIT license. See
`third_party/lib6502/COPYING.lib6502` for the full text.

`third_party/zmac` contains a copy of the venerable zmac 8080 and Z80
assembler. It's in the public domain.

`third_party/lemon` contains a copy of the lemon parser generator. It's in the
public domain.

