;
; Copyright (C) 2009-2010 David McPaul
;
; All rights reserved. Distributed under the terms of the MIT License.
;

; A rather unoptimised set of yuv to rgb converters
; does 8 pixels at a time

; inputer:
; reads 128bits of yuv 8 bit data and puts
; the y values converted to 16 bit in xmm0
; the u values converted to 16 bit and duplicated into xmm1
; the v values converted to 16 bit and duplicated into xmm2

; conversion:
; does the yuv to rgb conversion using 16 bit fixed point and the
; results are placed into the following registers as 8 bit clamped values
; r values in xmm3
; g values in xmm4
; b values in xmm5

; outputer:
; writes out the rgba pixels as 8 bit values with 0 for alpha

; xmm6 used for scratch
; xmm7 used for scratch

%macro  cglobal 1
	global  _%1
	%define %1 _%1
	align 16
%1:
%endmacro

; conversion code 
%macro yuv2rgbsse2 0
; u = u - 128
; v = v - 128
; r = y + v + v >> 2 + v >> 3 + v >> 5 
; g = y - (u >> 2 + u >> 4 + u >> 5) - (v >> 1 + v >> 3 + v >> 4 + v >> 5)
; b = y + u + u >> 1 + u >> 2 + u >> 6
; subtract 16 from y
	movdqa xmm7, [Const16]			; loads a constant using data cache (slower on first fetch but then cached)
	psubsw xmm0,xmm7				; y = y - 16
; subtract 128 from u and v
	movdqa xmm7, [Const128]			; loads a constant using data cache (slower on first fetch but then cached)
	psubsw xmm1,xmm7				; u = u - 128
	psubsw xmm2,xmm7				; v = v - 128
; load r,b with y 
	movdqa xmm3,xmm0				; r = y 
	pshufd xmm5,xmm0, 0xE4			; b = y 

; r = y + v + v >> 2 + v >> 3 + v >> 5
	paddsw xmm3, xmm2				; add v to r
	movdqa xmm7, xmm1				; move u to scratch
	pshufd xmm6, xmm2, 0xE4			; move v to scratch
	
	psraw  xmm6,2					; divide v by 4
	paddsw xmm3, xmm6				; and add to r
	psraw  xmm6,1					; divide v by 2
	paddsw xmm3, xmm6				; and add to r
	psraw  xmm6,2					; divide v by 4
	paddsw xmm3, xmm6				; and add to r

; b = y + u + u >> 1 + u >> 2 + u >> 6
	paddsw xmm5, xmm1				; add u to b
	psraw  xmm7,1					; divide u by 2
	paddsw xmm5, xmm7				; and add to b
	psraw  xmm7,1					; divide u by 2
	paddsw xmm5, xmm7				; and add to b
	psraw  xmm7,4					; divide u by 32
	paddsw xmm5, xmm7				; and add to b
	
; g = y - u >> 2 - u >> 4 - u >> 5 - v >> 1 - v >> 3 - v >> 4 - v >> 5
	movdqa xmm7,xmm2				; move v to scratch
	pshufd xmm6,xmm1, 0xE4			; move u to scratch
	movdqa xmm4,xmm0				; g = y 
	
	psraw  xmm6,2					; divide u by 4
	psubsw xmm4,xmm6				; subtract from g
	psraw  xmm6,2					; divide u by 4
	psubsw xmm4,xmm6				; subtract from g
	psraw  xmm6,1					; divide u by 2
	psubsw xmm4,xmm6				; subtract from g

	psraw  xmm7,1					; divide v by 2
	psubsw xmm4,xmm7				; subtract from g
	psraw  xmm7,2					; divide v by 4
	psubsw xmm4,xmm7				; subtract from g
	psraw  xmm7,1					; divide v by 2
	psubsw xmm4,xmm7				; subtract from g
	psraw  xmm7,1					; divide v by 2
	psubsw xmm4,xmm7				; subtract from g
%endmacro

; conversion code 
%macro yuv2rgbsse 0
; u = u - 128
; v = v - 128
; r = y + v + v >> 2 + v >> 3 + v >> 5 
; g = y - (u >> 2 + u >> 4 + u >> 5) - (v >> 1 + v >> 3 + v >> 4 + v >> 5)
; b = y + u + u >> 1 + u >> 2 + u >> 6
; subtract 16 from y
	movq mm7, [Const16]				; loads a constant using data cache (slower on first fetch but then cached)
	psubsw mm0,mm7					; y = y - 16
; subtract 128 from u and v
	movq mm7, [Const128]			; loads a constant using data cache (slower on first fetch but then cached)
	psubsw mm1,mm7					; u = u - 128
	psubsw mm2,mm7					; v = v - 128
; load r,g,b with y 
	movq mm3,mm0					; r = y 
	pshufw mm5,mm0, 0xE4			; b = y 

; r = r + v + v >> 2 + v >> 3 + v >> 5
	paddsw mm3, mm2					; add v to r
	movq mm7, mm1					; move u to scratch
	pshufw mm6, mm2, 0xE4			; move v to scratch
	
	psraw  mm6,2					; divide v by 4
	paddsw mm3, mm6					; and add to r
	psraw  mm6,1					; divide v by 2
	paddsw mm3, mm6					; and add to r
	psraw  mm6,2					; divide v by 4
	paddsw mm3, mm6					; and add to r

; b = y + u + u >> 1 + u >> 2 + u >> 6
	paddsw mm5, mm1					; add u to b
	psraw  mm7,1					; divide u by 2
	paddsw mm5, mm7					; and add to b
	psraw  mm7,1					; divide u by 2
	paddsw mm5, mm7					; and add to b
	psraw  mm7,4					; divide u by 32
	paddsw mm5, mm7					; and add to b
	
; g = y - u >> 2 - u >> 4 - u >> 5 - v >> 1 - v >> 3 - v >> 4 - v >> 5
	movq mm7,mm2					; move v to scratch
	pshufw mm6,mm1, 0xE4			; move u to scratch
	movq mm4,mm0					; g = y 
	
	psraw  mm6,2					; divide u by 4
	psubsw mm4,mm6					; subtract from g
	psraw  mm6,2					; divide u by 4
	psubsw mm4,mm6					; subtract from g
	psraw  mm6,1					; divide u by 2
	psubsw mm4,mm6					; subtract from g

	psraw  mm7,1					; divide v by 2
	psubsw mm4,mm7					; subtract from g
	psraw  mm7,2					; divide v by 4
	psubsw mm4,mm7					; subtract from g
	psraw  mm7,1					; divide v by 2
	psubsw mm4,mm7					; subtract from g
	psraw  mm7,1					; divide v by 2
	psubsw mm4,mm7					; subtract from g
%endmacro

; outputer
%macro rgba32sse2output 0
; clamp values
	pxor xmm7,xmm7
	packuswb xmm3,xmm7				; clamp to 0,255 and pack R to 8 bit per pixel
	packuswb xmm4,xmm7				; clamp to 0,255 and pack G to 8 bit per pixel
	packuswb xmm5,xmm7				; clamp to 0,255 and pack B to 8 bit per pixel
; convert to bgra32 packed
	punpcklbw xmm5,xmm4				; bgbgbgbgbgbgbgbg
	movdqa xmm0, xmm5				; save bg values
	punpcklbw xmm3,xmm7				; r0r0r0r0r0r0r0r0
	punpcklwd xmm5,xmm3				; lower half bgr0bgr0bgr0bgr0
	punpckhwd xmm0,xmm3				; upper half bgr0bgr0bgr0bgr0
; write to output ptr
	movntdq [edi], xmm5				; output first 4 pixels bypassing cache
	movntdq [edi+16], xmm0			; output second 4 pixels bypassing cache
%endmacro

; outputer
%macro rgba32sseoutput 0
; clamp values
	pxor mm7,mm7
	packuswb mm3,mm7				; clamp to 0,255 and pack R to 8 bit per pixel
	packuswb mm4,mm7				; clamp to 0,255 and pack G to 8 bit per pixel
	packuswb mm5,mm7				; clamp to 0,255 and pack B to 8 bit per pixel
; convert to bgra32 packed
	punpcklbw mm5,mm4				; bgbgbgbgbgbgbgbg
	movq mm0, mm5					; save bg values
	punpcklbw mm3,mm7				; r0r0r0r0
	punpcklwd mm5,mm3				; lower half bgr0bgr0
	punpckhwd mm0,mm3				; upper half bgr0bgr0
; write to output ptr
	movq [edi], mm5					; output first 2 pixels 
	movq [edi+8], mm0				; output second 2 pixels 
%endmacro

SECTION .data align=16

Const16	dw	16
	dw	16
	dw	16
	dw	16
	dw	16
	dw	16
	dw	16
	dw	16

Const128	dw	128
	dw	128
	dw	128
	dw	128
	dw	128
	dw	128
	dw	128
	dw	128

; void Convert_YUV422_RGBA32_SSE2(void *fromPtr, void *toPtr, int width)
width    equ	ebp+16
toPtr    equ	ebp+12
fromPtr  equ	ebp+8

; void Convert_YUV420P_RGBA32_SSE2(void *fromYPtr, void *fromUPtr, void *fromVPtr, void *toPtr, int width)
width1    equ	ebp+24
toPtr1    equ	ebp+20
fromVPtr  equ	ebp+16
fromUPtr  equ	ebp+12
fromYPtr  equ	ebp+8

SECTION .text align=16

cglobal Convert_YUV422_RGBA32_SSE2
; reserve variables
	push ebp
	mov ebp, esp
	push edi
	push esi
	push ecx
	
	mov esi, [fromPtr]
	mov edi, [toPtr]
	mov ecx, [width]
; loop width / 8 times
	shr ecx,3
	test ecx,ecx
	jng ENDLOOP
REPEATLOOP:							; loop over width / 8
; YUV422 packed inputer
	movdqa xmm0, [esi]				; should have yuyv yuyv yuyv yuyv
	pshufd xmm1, xmm0, 0xE4			; copy to xmm1
	movdqa xmm2, xmm0				; copy to xmm2
; extract y
	pxor xmm7,xmm7					; 00000000000000000000000000000000
	pcmpeqd xmm6,xmm6				; ffffffffffffffffffffffffffffffff
	punpcklbw xmm6,xmm7				; interleave xmm7 into xmm6 ff00ff00ff00ff00ff00ff00ff00ff00
	pand xmm0, xmm6					; clear all but y values leaving y0y0 etc
; extract u and duplicate so each u in yuyv becomes 0u0u
	psrld xmm6,8					; 00ff0000 00ff0000 00ff0000 00ff0000
	pand xmm1, xmm6					; clear all yv values leaving 0u00 etc
	psrld xmm1,8					; rotate u to get u000
	pshuflw xmm1,xmm1, 0xA0			; copy u values
	pshufhw xmm1,xmm1, 0xA0			; to get u0u0
; extract v
	pslld xmm6,16					; 000000ff000000ff 000000ff000000ff
	pand xmm2, xmm6					; clear all yu values leaving 000v etc
	psrld xmm2,8					; rotate v to get 00v0
	pshuflw xmm2,xmm2, 0xF5			; copy v values
	pshufhw xmm2,xmm2, 0xF5			; to get v0v0

yuv2rgbsse2
	
rgba32sse2output

; endloop
	add edi,32
	add esi,16
	sub ecx, 1				; apparently sub is better than dec
	jnz REPEATLOOP
ENDLOOP:
; Cleanup
	pop ecx
	pop esi
	pop edi
	mov esp, ebp
	pop ebp
	ret

cglobal Convert_YUV420P_RGBA32_SSE2
; reserve variables
	push ebp
	mov ebp, esp
	push edi
	push esi
	push ecx
	push eax
	push ebx
		
	mov esi, [fromYPtr]
	mov eax, [fromUPtr]
	mov ebx, [fromVPtr]
	mov edi, [toPtr1]
	mov ecx, [width1]
; loop width / 8 times
	shr ecx,3
	test ecx,ecx
	jng ENDLOOP1
REPEATLOOP1:						; loop over width / 8
; YUV420 Planar inputer
	movq xmm0, [esi]				; fetch 8 y values (8 bit) yyyyyyyy00000000
	movd xmm1, [eax]				; fetch 4 u values (8 bit) uuuu000000000000
	movd xmm2, [ebx]				; fetch 4 v values (8 bit) vvvv000000000000
	
; extract y
	pxor xmm7,xmm7					; 00000000000000000000000000000000
	punpcklbw xmm0,xmm7				; interleave xmm7 into xmm0 y0y0y0y0y0y0y0y0
; extract u and duplicate so each becomes 0u0u
	punpcklbw xmm1,xmm7				; interleave xmm7 into xmm1 u0u0u0u000000000
	punpcklwd xmm1,xmm7				; interleave again u000u000u000u000
	pshuflw xmm1,xmm1, 0xA0			; copy u values
	pshufhw xmm1,xmm1, 0xA0			; to get u0u0
; extract v
	punpcklbw xmm2,xmm7				; interleave xmm7 into xmm1 v0v0v0v000000000
	punpcklwd xmm2,xmm7				; interleave again v000v000v000v000
	pshuflw xmm2,xmm2, 0xA0			; copy v values
	pshufhw xmm2,xmm2, 0xA0			; to get v0v0

yuv2rgbsse2
	
rgba32sse2output

; endloop
	add edi,32
	add esi,8
	add eax,4
	add ebx,4
	sub ecx, 1				; apparently sub is better than dec
	jnz REPEATLOOP1
ENDLOOP1:
; Cleanup
	pop ebx
	pop eax
	pop ecx
	pop esi
	pop edi
	mov esp, ebp
	pop ebp
	ret

cglobal Convert_YUV422_RGBA32_SSE
; reserve variables
	push ebp
	mov ebp, esp
	push edi
	push esi
	push ecx
	
	mov esi, [fromPtr]
	mov ecx, [width]
	mov edi, [toPtr]
; loop width / 4 times
	shr ecx,2
	test ecx,ecx
	jng ENDLOOP2
REPEATLOOP2:						; loop over width / 4

; YUV422 packed inputer
	movq mm0, [esi]					; should have yuyv yuyv
	pshufw mm1, mm0, 0xE4			; copy to mm1
	movq mm2, mm0					; copy to mm2
; extract y
	pxor mm7,mm7					; 0000000000000000
	pcmpeqb mm6,mm6					; ffffffffffffffff
	punpckhbw mm6,mm7				; interleave mm7 into mm6 ff00ff00ff00ff00
	pand mm0, mm6					; clear all but y values leaving y0y0 etc
; extract u and duplicate so each u in yuyv becomes 0u0u
	psrld mm6,8						; 00ff0000 00ff0000 
	pand mm1, mm6					; clear all yv values leaving 0u00 etc
	psrld mm1,8						; rotate u to get u000
	pshufw mm1,mm1, 0xA0			; copy u values	to get u0u0		(SSE not MMX)
; extract v
	pslld mm6,16					; 000000ff000000ff
	pand mm2, mm6					; clear all yu values leaving 000v etc
	psrld mm2,8						; rotate v to get 00v0
	pshufw mm2,mm2, 0xF5			; copy v values to get v0v0    (SSE not MMX)

yuv2rgbsse

rgba32sseoutput

	; endloop
	add edi,16
	add esi,8
	sub ecx, 1						; apparently sub is better than dec
	jnz REPEATLOOP2
ENDLOOP2:
; Cleanup
	emms							; reset mmx regs back to float
	pop ecx
	pop esi
	pop edi
	mov esp, ebp
	pop ebp
	ret

cglobal Convert_YUV420P_RGBA32_SSE
; reserve variables
	push ebp
	mov ebp, esp
	push edi
	push esi
	push ecx
	push eax
	push ebx
		
	mov esi, [fromYPtr]
	mov eax, [fromUPtr]
	mov ebx, [fromVPtr]
	mov edi, [toPtr1]
	mov ecx, [width1]
; loop width / 4 times
	shr ecx,2
	test ecx,ecx
	jng ENDLOOP3
REPEATLOOP3:						; loop over width / 4
; YUV420 Planar inputer
	movq mm0, [esi]					; fetch 4 y values (8 bit) yyyy0000
	movd mm1, [eax]					; fetch 2 u values (8 bit) uu000000
	movd mm2, [ebx]					; fetch 2 v values (8 bit) vv000000
	
; extract y
	pxor mm7,mm7					; 0000000000000000
	punpcklbw mm0,mm7				; interleave xmm7 into xmm0 y0y0y0y
; extract u and duplicate so each becomes 0u0u
	punpcklbw mm1,mm7				; interleave xmm7 into xmm1 u0u00000
	punpcklwd mm1,mm7				; interleave again u000u000
	pshufw mm1,mm1, 0xA0			; copy u values to get u0u0
; extract v
	punpcklbw mm2,mm7				; interleave xmm7 into xmm1 v0v00000
	punpcklwd mm2,mm7				; interleave again v000v000
	pshufw mm2,mm2, 0xA0			; copy v values to get v0v0

yuv2rgbsse
	
rgba32sseoutput

; endloop
	add edi,16
	add esi,4
	add eax,2
	add ebx,2
	sub ecx, 1				; apparently sub is better than dec
	jnz REPEATLOOP3
ENDLOOP3:
; Cleanup
	emms
	pop ebx
	pop eax
	pop ecx
	pop esi
	pop edi
	mov esp, ebp
	pop ebp
	ret

SECTION .note.GNU-stack noalloc noexec nowrite progbits
