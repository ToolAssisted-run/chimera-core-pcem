/* hdd-probe.S - a 512-byte boot sector that proves a hard-disk write survived.
 *
 * The gate needs to see bytes SURVIVE: written by the guest, taken out of the
 * machine, put back in, and found there. A leg that only proves a write did
 * not crash proves nothing. This is the smallest guest that can do it - no
 * operating system, no disk image anybody has to supply, just INT 13h.
 *
 * Assembled twice, from the same source:
 *
 *   MODE=1 (writer)  fill a sector with MAGIC and write it to LBA 1 of the
 *                    first hard disk. Prints 'W' when the BIOS says it worked.
 *   MODE=2 (reader)  read LBA 1 back, and ONLY if it holds MAGIC, copy it to
 *                    LBA 2. Prints 'R' on a match, 'X' on a mismatch.
 *   MODE=3 (marker)  touch no disk at all and fill the screen with '#'. This
 *                    is the "did the machine get past POST" probe: an AT that
 *                    stops at "162-System Options Not Set" never boots the
 *                    floppy, so the screen keeps its three lines of text and
 *                    a lit-pixel count tells the two apart without reading
 *                    anything off the screen.
 *
 * So MAGIC at LBA 2 of an exported image can only have got there by a machine
 * READING it from the disk it was seeded with. That is the round trip, and it
 * is checked on the bytes rather than on the screen.
 *
 * The sectors are addressed as CHS cylinder 0, head 0, sector 2 and 3 - which
 * is LBA 1 and 2 - because INT 13h AH=02/03 is CHS and every geometry this
 * core derives has at least three sectors on its first track.
 */
	.code16
	.text
	.globl _start
_start:
	cli
	xorw	%ax, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movw	$0x7c00, %sp
	sti

#if MODE == 3
	movw	$0x0e23, %ax		/* '#' */
	movw	$0x0007, %bx
mloop:
	int	$0x10
	movw	$0x0e23, %ax
	movw	$0x0007, %bx
	jmp	mloop

#elif MODE == 1
	/* fill 0x7e00..0x7fff with MAGIC, over and over */
	movw	$0x7e00, %di
	movw	$32, %bp
1:	movw	$magic, %si
	movw	$16, %cx
	rep	movsb
	decw	%bp
	jnz	1b

	movw	$0x0301, %ax		/* write one sector */
	movw	$0x7e00, %bx
	movw	$0x0002, %cx		/* cylinder 0, sector 2 = LBA 1 */
	movw	$0x0080, %dx		/* head 0, drive 0x80 */
	int	$0x13
	jc	bad

	/* And then keep writing: a counter in the byte after the magic, over
	 * and over into the same sector. A disk that only ever changes once
	 * cannot tell an overlay that rewinds from one whose INDEX rewinds
	 * while its blocks do not - the state has to be taken between two
	 * writes to the SAME block for that difference to show. */
	movb	$'W', %al
	movb	$0x0e, %ah
	movw	$0x0007, %bx
	int	$0x10
wloop:
	incb	0x7e10
	movw	$0x0301, %ax
	movw	$0x7e00, %bx
	movw	$0x0002, %cx
	movw	$0x0080, %dx
	int	$0x13
	movw	$0x0e2e, %ax		/* '.', so the picture keeps moving */
	movw	$0x0007, %bx
	int	$0x10
	movw	$0x2000, %cx
1:	loop	1b
	jmp	wloop
#else
	movw	$0x0201, %ax		/* read one sector */
	movw	$0x7e00, %bx
	movw	$0x0002, %cx		/* LBA 1 */
	movw	$0x0080, %dx
	int	$0x13
	jc	bad

	movw	$0x7e00, %si
	movw	$magic, %di
	movw	$16, %cx
	repe	cmpsb
	jne	mismatch

	movw	$0x0301, %ax		/* write it on to LBA 2 */
	movw	$0x7e00, %bx
	movw	$0x0003, %cx		/* cylinder 0, sector 3 = LBA 2 */
	movw	$0x0080, %dx
	int	$0x13
	jc	bad
	movb	$'R', %al
	jmp	say
mismatch:
	movb	$'X', %al
	jmp	say
#endif

bad:
	movb	$'F', %al
say:
	movb	$0x0e, %ah
	movw	$0x0007, %bx
	int	$0x10
	/* Say it again, forever: a still picture is a stalled machine and the
	 * gate's liveness leg wants to watch something move, not compare a
	 * digest against itself (docs/gates.md, and four times in this repo). */
	movw	$0x0e2e, %ax		/* '.' */
	movw	$0x0007, %bx
spin:
	int	$0x10
	movw	$0xffff, %cx
1:	loop	1b
	jmp	spin

magic:
	.ascii	"CHIMERA-PCEM-HD!"

	.org	510
	.byte	0x55, 0xaa
