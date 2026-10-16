ps_3_0
dcl_texcoord0 v0
dcl_position0 vPos
def c68 = -1.00000000e+00, -1.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c69 = 5.00000000e-01, 5.00000000e-01, -5.00000000e-01, 1.00000000e+00
def c70 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c71 = 9.99999975e-06, 1.20000001e-02, 6.99999975e-04, 3.00000003e-03
def c72 = -9.99989986e-01, 1.00000002e+20, -5.00000000e-01, -5.00000000e-01
def c73 = 2.98999995e-01, 5.87000012e-01, 1.14000000e-01, 1.44270003e+00
def c74 = 1.50000006e-01, 9.99999978e-03, -4.00000000e+00, 2.00000000e+00
def c75 = 1.00000000e+00, 1.00000000e+00, 1.60000000e+01, 1.99999996e-02
def c76 = 2.50000000e-01, 2.00000003e-01, 1.44269502e+00, -1.99999996e-02
def c77 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 9.99999975e-05
def c78 = 1.50000006e-01, 1.50000006e-01, 1.50000006e-01, -2.00000000e+00
def c79 = -4.49999988e-01, -4.49999988e-01, -4.49999988e-01, 3.00000000e+00
def c80 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c81 = 9.99999996e-13, 9.99999997e-07, 6.39999986e-01, 1.36000001e+00
def c82 = 1.20000005e+00, -1.50000000e+00, -3.00000000e+00, 0.00000000e+00
def c83 = 1.56250000e-02, 1.56250000e-02, 7.81250000e-03, 7.81250000e-03
defi i0 = 255, 0, 0, 0
dcl_2d s10
dcl_2d s12
dcl_2d s1
dcl_2d s2
dcl_2d s9
dcl_2d s8
dcl_2d s13
dcl_2d s0
dcl_2d s11
texld r0.xyzw, v0.xyxx, s0
mul r1.xy, v0.xyxx, c33.xyxx
frc r2.xyzw, r1.xyxx
add r1.xy, r1.xyxx, -r2.xyzw
add r1.zw, c33.xxxy, c68.xxxy
max r1.xy, r1.xyxx, c68.zwzz
min r1.xy, r1.xyxx, r1.zwzz
add r1.xy, r1.xyxx, c69.xyxx
mul r1.xy, r1.xyxx, c0.xyxx
texld r2.xyzw, r1.xyxx, s1
add r2.x, r2.x, -c1.w
mul r2.x, r2.x, c2.x
mov_sat r2.x, r2.x
add r2.y, c30.x, c69.z
cmp r2.y, r2.y, c68.z, c69.w
mov r2.z, r2.w
cmp r2.z, -r2.y, r2.z, c68.z
mov r2.w, r2.z
add r2.z, -r2.y, c69.w
if_ne r2.z, -r2.z
    mov r3.xyzw, c70.xyzw
    mov r3.xy, r1.xyxx
    texldl r3.xyzw, r3.xyzw, s11
    add r4.x, c0.z, -r3.x
    cmp r4.x, r4.x, c68.z, c69.w
    mul r4.y, c0.z, c0.w
    add r4.z, c0.w, -c0.z
    mul r4.z, r2.x, r4.z
    add r4.z, c0.w, -r4.z
    max r4.z, r4.z, c71.x
    rcp r4.z, r4.z
    mul r4.y, r4.y, r4.z
    mul r4.z, r3.x, c71.z
    max r4.z, r4.z, c71.y
    add r4.y, r4.y, r4.z
    add r4.y, r4.y, -r3.x
    cmp r4.y, r4.y, c68.z, c69.w
    add r4.y, -r4.y, c69.w
    min r4.x, r4.x, r4.y
    add r4.y, -r3.y, c71.w
    cmp r4.y, r4.y, c68.z, c69.w
    min r4.x, r4.x, r4.y
    cmp r3.x, -r4.x, c68.z, r3.x
    mov r2.w, r3.x
else
endif
mov r3.x, r2.w
cmp r3.y, -r3.x, c68.z, c69.w
mul r3.z, c0.z, c0.w
add r3.w, c0.w, -c0.z
mul r4.x, r2.x, r3.w
add r4.x, c0.w, -r4.x
max r4.x, r4.x, c71.x
rcp r4.y, r4.x
mul r4.y, r3.z, r4.y
cmp r3.x, -r3.y, r4.y, r3.x
mul r4.y, r3.x, c1.z
mad r4.y, r4.y, c25.x, c25.y
max r4.y, r4.y, c68.z
log r4.y, r4.y
mul r4.y, c25.z, r4.y
exp r4.y, r4.y
mov_sat r4.y, r4.y
add r4.y, r4.y, c68.x
mad r4.y, c25.w, r4.y, c69.w
add r2.x, r2.x, c72.x
cmp r2.x, r2.x, c68.z, c69.w
add r3.y, -r3.y, c69.w
min r4.z, r2.x, r3.y
mov r5.x, c68.z
mov r5.y, c68.z
mov r5.z, c68.z
mov r6.x, c68.z
mov r6.y, c68.z
mov r6.z, c68.z
mov r4.w, c68.z
mov r5.w, c68.z
mov r6.w, c72.y
mov r7.x, c68.z
mov r7.y, c68.z
mov r7.z, c68.z
mov r7.w, c68.z
mov r8.x, c68.z
mov r8.y, c68.z
mov r8.z, c68.z
mov r8.w, c68.z
mov r9.x, c69.w
mov r10.x, c68.z
mov r10.y, c68.z
mov r10.z, c68.z
mov r10.w, c68.z
mov r11.x, c68.z
mov r11.y, c68.z
mov r11.z, c68.z
mov r11.w, c69.w
mov r9.y, c68.z
mov r9.z, c72.y
mul r12.xy, v0.xyxx, c33.zwzz
add r12.xy, r12.xyxx, c72.zwzz
frc r13.xyzw, r12.xyxx
add r12.zw, r12.xxxy, -r13.xxxy
add r12.xy, r12.xyxx, -r12.zwzz
rcp r4.x, r4.x
mul r4.x, r3.z, r4.x
dp3 r9.w, r0.xyzx, c73.xyzx
mul r13.x, r4.x, c74.y
max r13.x, r13.x, c74.x
rcp r13.x, r13.x
mul r13.x, r13.x, c73.w
mov r13.y, c68.z
rep i0.xyzw
    mov r22.w, r13.y
    add r22.w, r22.w, c74.z
    cmp r22.w, r22.w, c68.z, c69.w
    add r22.w, -r22.w, c69.w
    if_ne r22.w, -r22.w
        break
    else
    endif
    mov r22.w, r13.y
    mul r22.w, r22.w, c69.x
    frc r24.xyzw, r22.w
    add r22.w, r22.w, -r24.x
    mov r23.z, r13.y
    mul r23.w, r22.w, c74.w
    add r23.z, r23.z, -r23.w
    mov r23.x, r23.z
    mov r23.y, r22.w
    mov r23.zw, r23.xxxy
    add r23.zw, r12.xxzw, r23.xxzw
    add r24.xy, c33.zwzz, c68.xyxx
    max r23.zw, r23.xxzw, c68.xxzw
    min r23.zw, r23.xxzw, r24.xxxy
    add r23.zw, r23.xxzw, c69.xxxy
    rcp r22.y, c33.z
    rcp r22.z, c33.w
    mul r23.zw, r23.xxzw, r22.xxyz
    mul r24.xy, r23.zwzz, c33.xyxx
    frc r25.xyzw, r24.xyxx
    add r24.xy, r24.xyxx, -r25.xyzw
    max r24.xy, r24.xyxx, c68.zwzz
    min r24.xy, r24.xyxx, r1.zwzz
    add r24.xy, r24.xyxx, c69.xyxx
    mul r24.xy, r24.xyxx, c0.xyxx
    mov r19.xyzw, c70.xyzw
    mov r19.xy, r24.xyxx
    mov r25.xyzw, r19.xyzw
    texldl r25.xyzw, r25.xyzw, s1
    add r22.w, r25.x, -c1.w
    mul r22.w, r22.w, c2.x
    mov_sat r22.w, r22.w
    add r24.zw, -r12.xxxy, c75.xxxy
    mov r25.xy, r23.xyxx
    add r25.zw, r12.xxxy, -r24.xxzw
    mul r25.xy, r25.xyxx, r25.zwzz
    add r24.zw, r24.xxzw, r25.xxxy
    if_ne r4.z, -r4.z
        mul r25.x, r22.w, r3.w
        add r25.x, c0.w, -r25.x
        max r25.x, r25.x, c71.x
        rcp r18.x, r25.x
        mul r25.x, r3.z, r18.x
        add r25.x, r25.x, -r4.x
        abs r25.x, r25.x
        mov r16.xyzw, c70.xyzw
        mov r16.xy, r24.xyxx
        mov r26.xyzw, r16.xyzw
        texldl r26.xyzw, r26.xyzw, s0
        dp3 r25.y, r26.xyzx, c73.xyzx
        add r25.z, r9.w, -r25.y
        abs r25.z, r25.z
        mul r25.z, r25.z, c75.z
        add r25.y, r9.w, r25.y
        add r25.y, r25.y, c75.w
        rcp r13.z, r25.y
        mul r25.y, r25.z, r13.z
        mul r25.x, r25.x, r13.x
        mul r25.z, r24.z, r24.w
        add r25.w, -r25.x, -r25.y
        exp r13.w, r25.w
        mul r25.z, r25.z, r13.w
        mad r25.x, r25.x, c76.x, r25.y
        mov r21.xyzw, c70.xyzw
        mov r21.xy, r23.zwzz
        mov r26.xyzw, r21.xyzw
        texldl r26.xyzw, r26.xyzw, s8
        mov r14.xyzw, c70.xyzw
        mov r14.xy, r23.zwzz
        mov r27.xyzw, r14.xyzw
        texldl r27.xyzw, r27.xyzw, s12
        mov r15.xyzw, c70.xyzw
        mov r15.xy, r23.zwzz
        mov r28.xyzw, r15.xyzw
        texldl r28.xyzw, r28.xyzw, s10
        mov r29.xyz, r6.xyzx
        mad r29.xyz, r26.xyzx, r25.z, r29.xyzx
        mov r6.xyz, r29.xyzx
        mov r25.y, r4.w
        mad r25.y, r26.w, r25.z, r25.y
        mov r4.w, r25.y
        mov r29.xyz, r5.xyzx
        mad r29.xyz, r27.xyzx, r25.z, r29.xyzx
        mov r5.xyz, r29.xyzx
        mov r25.y, r8.w
        mad r25.y, r28.w, r25.z, r25.y
        mov r8.w, r25.y
        mov r25.y, r5.w
        add r25.y, r25.y, r25.z
        mov r5.w, r25.y
        mov r25.y, r6.w
        add r25.y, r25.x, -r25.y
        cmp r25.y, r25.y, c68.z, c69.w
        mov r25.z, r6.w
        cmp r25.x, -r25.y, r25.z, r25.x
        mov r6.w, r25.x
        mov r29.xyzw, r7.xyzw
        cmp r26.xyzw, -r25.y, r29.xyzw, r26.xyzw
        mov r7.xyzw, r26.xyzw
        mov r25.xzw, r8.xxyz
        cmp r25.xzw, -r25.y, r25.xxzw, r27.xxyz
        mov r8.xyz, r25.xzwx
        mov r25.x, r9.x
        cmp r25.x, -r25.y, r25.x, r28.w
        mov r9.x, r25.x
    else
    endif
    mov r25.x, r2.w
    cmp r25.x, -r2.y, r25.x, c68.z
    mov r2.w, r25.x
    if_ne r2.z, -r2.z
        mov r17.xyzw, c70.xyzw
        mov r17.xy, r24.xyxx
        mov r25.xyzw, r17.xyzw
        texldl r25.xyzw, r25.xyzw, s11
        add r24.x, c0.z, -r25.x
        cmp r24.x, r24.x, c68.z, c69.w
        mul r24.y, r22.w, r3.w
        add r24.y, c0.w, -r24.y
        max r24.y, r24.y, c71.x
        rcp r18.y, r24.y
        mul r24.y, r3.z, r18.y
        mul r26.x, r25.x, c71.z
        max r26.x, r26.x, c71.y
        add r24.y, r24.y, r26.x
        add r24.y, r24.y, -r25.x
        cmp r24.y, r24.y, c68.z, c69.w
        add r24.y, -r24.y, c69.w
        min r24.x, r24.x, r24.y
        add r24.y, -r25.y, c71.w
        cmp r24.y, r24.y, c68.z, c69.w
        min r24.x, r24.x, r24.y
        cmp r24.x, -r24.x, c68.z, r25.x
        mov r2.w, r24.x
    else
    endif
    mov r24.x, r2.w
    cmp r24.y, -r24.x, c68.z, c69.w
    mul r22.w, r22.w, r3.w
    add r22.w, c0.w, -r22.w
    max r22.w, r22.w, c71.x
    rcp r18.z, r22.w
    mul r22.w, r3.z, r18.z
    cmp r22.w, -r24.y, r22.w, r24.x
    add r22.w, r22.w, -r3.x
    abs r22.w, r22.w
    mov r20.xyzw, c70.xyzw
    mov r20.xy, r23.zwzz
    mov r25.xyzw, r20.xyzw
    texldl r25.xyzw, r25.xyzw, s9
    mul r23.z, r24.z, r24.w
    mul r23.w, r3.x, c75.w
    max r23.w, r23.w, c76.y
    rcp r18.w, r23.w
    mul r23.w, -r22.w, r18.w
    mul r23.w, r23.w, c76.z
    exp r22.x, r23.w
    mul r23.z, r23.z, r22.x
    mov r24.xyzw, r10.xyzw
    mad r24.xyzw, r25.xyzw, r23.z, r24.xyzw
    mov r10.xyzw, r24.xyzw
    mov r23.w, r9.y
    add r23.z, r23.w, r23.z
    mov r9.y, r23.z
    mov r23.z, r9.z
    add r23.z, r22.w, -r23.z
    cmp r23.z, r23.z, c68.z, c69.w
    mov r23.w, r9.z
    cmp r22.w, -r23.z, r23.w, r22.w
    mov r9.z, r22.w
    mov r24.xyzw, r11.xyzw
    cmp r24.xyzw, -r23.z, r24.xyzw, r25.xyzw
    mov r11.xyzw, r24.xyzw
    mov r22.w, r13.y
    add r22.w, r22.w, c69.w
    mov r13.y, r22.w
endrep
mov r1.z, r9.y
add r1.z, -r1.z, c71.x
cmp r1.z, r1.z, c68.z, c69.w
mov r1.w, r9.y
rcp r1.w, r1.w
mov r12.xyzw, r1.w
mul r10.xyzw, r10.xyzw, r12.xyzw
cmp r10.xyzw, -r1.z, r11.xyzw, r10.xyzw
mov r1.z, r5.w
add r1.z, r1.z, c76.w
cmp r1.z, r1.z, c68.z, c69.w
mov r1.w, r9.x
mov r2.y, r8.w
mov r2.z, r5.w
rcp r2.z, r2.z
mul r2.y, r2.y, r2.z
cmp r1.z, -r1.z, r2.y, r1.w
cmp r1.z, -r4.z, c69.w, r1.z
texld r9.xyzw, v0.xyxx, s10
mul r2.yzw, r0.xxyz, r1.z
mov_sat r11.xyz, r2.yzwy
add r11.xyz, -r11.xyzx, c77.xyzx
mad r2.yzw, r9.xxyz, r11.xxyz, r2.xyzw
mov_sat r2.yzw, r2.xyzw
mov r9.xyz, r2.yzwy
add r1.z, -r4.y, c69.w
mul r11.xyz, r1.z, c26.xyzx
min r11.xyz, r11.xyzx, r2.yzwy
if_ne r4.z, -r4.z
    mov r1.z, r5.w
    add r1.z, r1.z, c76.w
    cmp r1.z, r1.z, c68.z, c69.w
    mov r12.xyzw, r7.xyzw
    cmp r6.xyz, -r1.z, r6.xyzx, r12.xyzx
    mov r1.w, r4.w
    cmp r1.w, -r1.z, r1.w, r7.w
    mov r4.xzw, r8.xxyz
    cmp r4.xzw, -r1.z, r5.xxyz, r4.xxzw
    mov r3.z, r5.w
    cmp r1.z, -r1.z, r3.z, c69.w
    max r1.z, r1.z, c77.w
    rcp r1.z, r1.z
    mov r5.xyz, r1.z
    mul r5.xyz, r6.xyzx, r5.xyzx
    mul r1.w, r1.w, r1.z
    mov r6.xyz, r1.z
    mul r4.xzw, r4.xxzw, r6.xxyz
    max r4.xzw, r4.xxzw, c78.xxyz
    add r6.xyz, r2.yzwy, -r11.xyzx
    max r6.xyz, r6.xyzx, c70.xyzx
    rcp r7.x, r4.x
    rcp r7.y, r4.z
    rcp r7.z, r4.w
    mul r4.xzw, r6.xxyz, r7.xxyz
    min r4.xyz, r4.xzwx, r4.y
    mad r7.xyz, r4.xyzx, r5.xyzx, r2.yzwy
    mov r9.xyz, r7.xyzx
    mad r2.yzw, c79.xxyz, r6.xxyz, r2.xyzw
    max r2.yzw, r7.xxyz, r2.xyzw
    mov r9.xyz, r2.yzwy
    mad r2.yzw, r4.xxyz, c19.xxyz, r2.xyzw
    mov r9.xyz, r2.yzwy
    add r1.z, c24.z, c68.x
    abs r1.z, r1.z
    add r1.z, -r1.z, -r1.z
    cmp r1.z, r1.z, c68.z, c69.w
    add r1.z, -r1.z, c69.w
    cmp r2.yzw, -r1.z, r2.xyzw, r1.w
    mov r9.xyz, r2.yzwy
    add r1.z, c24.z, c78.w
    abs r1.z, r1.z
    add r1.z, -r1.z, -r1.z
    cmp r1.z, r1.z, c68.z, c69.w
    add r1.z, -r1.z, c69.w
    add r4.xyz, c18.xyzx, r5.xyzx
    max r4.xyz, r4.xyzx, c70.xyzx
    cmp r2.yzw, -r1.z, r2.xyzw, r4.xxyz
    mov r9.xyz, r2.yzwy
else
endif
add r1.z, c24.z, c69.z
cmp r1.z, r1.z, c68.z, c69.w
if_ne r1.z, -r1.z
    mov r2.yzw, r9.xxyz
    mov r4.xyzw, c70.xyzw
    mov r4.xy, v0.xyxx
    texldl r4.xyzw, r4.xyzw, s13
    mov r5.xyz, r9.xyzx
    add r1.z, -r2.x, c69.w
    min r1.z, r1.z, r3.y
    mov r1.w, c68.z
    add r2.x, r3.x, -c57.y
    mul r2.x, r2.x, c57.z
    mov_sat r2.x, r2.x
    cmp r1.z, -r1.z, r2.x, c69.w
    cmp r2.x, -r1.z, c68.z, c69.w
    add r2.x, -r2.x, c69.w
    cmp r3.x, -c34.w, c68.z, c69.w
    add r3.x, -r3.x, c69.w
    max r2.x, r2.x, r3.x
    if_ne r2.x, -r2.x
        mov r3.xyz, r5.xyzx
        mov r1.w, c69.w
    else
    endif
    add r1.w, -r1.w, c69.w
    if_ne r1.w, -r1.w
        mul r6.xy, c0.xyxx, c69.xyxx
        add r1.xy, r1.xyxx, -r6.xyxx
        mul r1.xy, r1.xyxx, c80.xyxx
        add r1.xy, r1.xyxx, c80.zwzz
        rcp r6.x, c1.x
        rcp r6.y, c1.y
        mul r1.xy, r1.xyxx, r6.xyxx
        mov r6.xy, r1.xyxx
        mov r6.z, c1.z
        mul r7.xyz, r1.x, c3.xyzx
        mul r1.xyw, r1.y, c4.xyxz
        add r1.xyw, r7.xyxz, r1.xyxw
        mul r7.xyz, c1.z, c5.xyzx
        add r1.xyw, r1.xyxw, r7.xyxz
        mov r7.xyz, r6.xyzx
        dp3 r2.x, r7.xyzx, r6.xyzx
        rsq r2.x, r2.x
        mad r2.x, r1.w, r2.x, -c64.w
        max r2.x, r2.x, c68.z
        mul r2.x, -r2.x, c57.w
        exp r2.x, r2.x
        mul r3.w, r1.z, r1.z
        mul r1.z, r1.z, c74.w
        add r1.z, -r1.z, c79.w
        mul r1.z, r3.w, r1.z
        mul r2.x, -c34.w, r2.x
        exp r2.x, r2.x
        mov r2.x, -r2.x
        add r2.x, r2.x, c69.w
        mul r1.z, r1.z, r2.x
        dp2add r2.x, c67.zwzz, c67.zwzz, c68.z
        rsq r2.x, r2.x
        rcp r2.x, r2.x
        dp2add r3.w, r1.xyxx, c67.zwzz, c68.z
        dp2add r1.x, r1.xyxx, r1.xyxx, c68.z
        max r1.x, r1.x, c81.x
        rsq r1.x, r1.x
        mul r1.x, r3.w, r1.x
        max r1.y, r2.x, c81.y
        rcp r1.y, r1.y
        mul r1.x, r1.x, r1.y
        mul r1.y, r2.x, c81.z
        mul r1.x, r1.x, c82.x
        add r1.x, -r1.x, c81.w
        log r1.x, r1.x
        mul r1.x, r1.x, c82.y
        exp r1.x, r1.x
        mul r1.x, r1.y, r1.x
        min r1.x, r1.x, c76.x
        mad r1.xyw, r1.x, c35.yzxw, c77.xyxz
        mul r1.xyw, c34.xyxz, r1.xyxw
        add r1.xyw, r1.xyxw, -r5.xyxz
        mul r1.xyz, r1.z, r1.xywx
        add r1.xyz, r5.xyzx, r1.xyzx
        mov r3.xyz, r1.xyzx
    else
    endif
    mov r1.xyz, r3.xyzx
    mad r1.xyz, r1.xyzx, r10.w, r10.xyzx
    add r2.xyz, r2.yzwy, -r1.xyzx
    mul r2.xyz, r4.x, r2.xyzx
    add r1.xyz, r1.xyzx, r2.xyzx
    mov r9.xyz, r1.xyzx
else
endif
add r1.x, c24.z, c82.z
abs r1.x, r1.x
add r1.x, -r1.x, -r1.x
cmp r1.x, r1.x, c68.z, c69.w
add r1.x, -r1.x, c69.w
mov r1.yzw, r9.xxyz
cmp r1.xyz, -r1.x, r1.yzwy, r10.xyzx
mad r2.xy, vPos.xyxx, c83.xyxx, c83.zwzz
texld r2.xyzw, r2.xyxx, s2
add r1.xyz, r1.xyzx, r2.xyzx
max r1.xyz, r1.xyzx, c70.xyzx
mov r1.w, r0.w
mov oC0.xyzw, r1.xyzw
