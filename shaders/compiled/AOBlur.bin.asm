ps_3_0
dcl_texcoord0 v0
def c5 = 0.00000000e+00, 2.00000000e+00, 2.00000000e+00, 1.00000000e+00
def c6 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c7 = -9.00000000e+00, 5.00000000e-01, 3.33333343e-01, 3.00000000e+00
def c8 = -1.50000000e+00, -1.50000000e+00, -9.99499977e-01, -9.99989986e-01
def c9 = 2.50000000e-01, 2.50000000e-01, 7.50000000e-01, 7.50000000e-01
def c10 = 1.50000000e+00, 1.50000000e+00, 1.00000000e+00, 1.00000000e+00
def c11 = -5.00000000e-01, 9.99999975e-06, 1.20000001e-02, 6.99999975e-04
def c12 = 3.00000003e-03, -1.00000000e+00, 9.99999982e-15, 2.50000004e-02
def c13 = 0.00000000e+00, 0.00000000e+00, -1.00000000e+00, 1.00000001e-01
def c14 = 2.00000009e-03, -2.50000000e+01, 2.00000003e-01, 5.00000000e+00
def c15 = -2.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s2
dcl_2d s0
dcl_2d s1
dcl_2d s3
mov r0.x, c5.x
mul r0.yz, c0.xxyx, c5.xyzx
mov r1.xyzw, c6.xyzw
mov r1.xy, v0.xyxx
texldl r1.xyzw, r1.xyzw, s2
mov r0.w, c5.w
mov r2.x, c5.x
rep i0.xyzw
    mov r2.w, r2.x
    add r2.w, r2.w, c7.x
    cmp r2.w, r2.w, c5.x, c5.w
    add r2.w, -r2.w, c5.w
    if_ne r2.w, -r2.w
        break
    else
    endif
    mov r2.w, r2.x
    add r2.w, r2.w, c7.y
    mul r2.w, r2.w, c7.z
    frc r4.xyzw, r2.w
    add r2.w, r2.w, -r4.x
    mov r4.x, r0.w
    mov r4.y, r2.x
    mul r4.z, r2.w, c7.w
    add r4.y, r4.y, -r4.z
    mov r2.y, r4.y
    mov r2.z, r2.w
    mov r4.yz, r2.xyzx
    mul r4.yz, r4.xyzx, c5.xyzx
    add r4.yz, r4.xyzx, c8.xxyx
    min r4.yz, r4.xyzx, c5.xyzx
    mul r4.yz, r4.xyzx, r0.xyzx
    add r4.yz, v0.xxyx, r4.xyzx
    mov r3.xyzw, c6.xyzw
    mov r3.xy, r4.yzyy
    mov r5.xyzw, r3.xyzw
    texldl r5.xyzw, r5.xyzw, s0
    min r2.w, r4.x, r5.w
    mov r0.w, r2.w
    mov r2.w, r2.x
    add r2.w, r2.w, c5.w
    mov r2.x, r2.w
endrep
add r0.w, r0.w, c8.z
cmp r0.w, r0.w, c5.x, c5.w
add r0.w, -r0.w, c5.w
if_ne r0.w, -r0.w
    mov r2.xyzw, r1.xyzw
    mov r0.x, c5.w
else
endif
mov r0.w, r0.x
add r0.w, -r0.w, c5.w
if_ne r0.w, -r0.w
    rcp r3.x, c0.x
    rcp r3.y, c0.y
    mad r3.zw, v0.xxxy, r3.xxxy, c9.xxxy
    frc r3.zw, r3.xxzw
    add r3.zw, -r3.xxzw, c9.xxzw
    mad r3.zw, r3.xxzw, c0.xxxy, v0.xxxy
    mul r4.xy, c0.xyxx, c10.xyxx
    add r4.zw, -r4.xxxy, c10.xxzw
    max r3.zw, r3.xxzw, r4.xxxy
    min r3.zw, r3.xxzw, r4.xxzw
    mov r5.xyzw, c6.xyzw
    mov r5.xy, r3.zwzz
    texldl r5.xyzw, r5.xyzw, s1
    add r0.w, r5.x, -c3.x
    mul r0.w, r0.w, c3.y
    mov_sat r0.w, r0.w
    add r5.x, r0.w, c8.w
    cmp r5.x, r5.x, c5.x, c5.w
    add r5.x, -r5.x, c5.w
    add r5.y, c4.x, c11.x
    cmp r5.y, r5.y, c5.x, c5.w
    mov r5.z, r5.w
    cmp r5.z, -r5.y, r5.z, c5.x
    mov r5.w, r5.z
    add r5.z, -r5.y, c5.w
    if_ne r5.z, -r5.z
        mov r6.xyzw, c6.xyzw
        mov r6.xy, r3.zwzz
        texldl r6.xyzw, r6.xyzw, s3
        add r7.x, c0.z, -r6.x
        cmp r7.x, r7.x, c5.x, c5.w
        mul r7.y, c0.z, c0.w
        add r7.z, c0.w, -c0.z
        mul r7.z, r0.w, r7.z
        add r7.z, c0.w, -r7.z
        max r7.z, r7.z, c11.y
        rcp r7.z, r7.z
        mul r7.y, r7.y, r7.z
        mul r7.z, r6.x, c11.w
        max r7.z, r7.z, c11.z
        add r7.y, r7.y, r7.z
        add r7.y, r7.y, -r6.x
        cmp r7.y, r7.y, c5.x, c5.w
        add r7.y, -r7.y, c5.w
        min r7.x, r7.x, r7.y
        add r6.x, -r6.y, c12.x
        cmp r6.x, r6.x, c5.x, c5.w
        min r6.x, r7.x, r6.x
        mov r5.w, r6.x
    else
    endif
    mov r6.x, r5.w
    max r5.x, r5.x, r6.x
    mov r6.xyzw, r2.xyzw
    cmp r6.xyzw, -r5.x, r6.xyzw, r1.xyzw
    mov r2.xyzw, r6.xyzw
    cmp r0.x, -r5.x, r0.x, c5.w
    add r0.x, -r0.x, c5.w
    if_ne r0.x, -r0.x
        mul r0.x, c0.z, c0.w
        add r5.x, c0.w, -c0.z
        mul r0.w, r0.w, r5.x
        add r0.w, c0.w, -r0.w
        max r0.w, r0.w, c11.y
        rcp r0.w, r0.w
        mul r0.w, r0.x, r0.w
        mul r6.x, r3.z, c5.y
        add r6.x, r6.x, c12.y
        mul r6.x, r6.x, r0.w
        rcp r6.y, c1.x
        mul r6.x, r6.x, r6.y
        mul r6.y, r3.w, c5.y
        add r6.y, -r6.y, c5.w
        mul r6.y, r6.y, r0.w
        rcp r6.z, c1.y
        mul r6.y, r6.y, r6.z
        mov r6.z, r6.y
        mov r6.w, r0.w
        mov r7.xyz, r6.xzwx
        mov r8.x, c0.x
        mov r8.y, c5.x
        mov r8.z, c5.x
        mov r8.w, c0.y
        mov r9.xy, r8.xyxx
        add r9.xy, r3.zwzz, -r9.xyxx
        mov r9.zw, r8.xxxy
        add r9.zw, r3.xxzw, -r9.xxzw
        mov r10.xyzw, c6.xyzw
        mov r10.xy, r9.zwzz
        texldl r10.xyzw, r10.xyzw, s1
        add r6.y, r10.x, -c3.x
        mul r6.y, r6.y, c3.y
        mov_sat r6.y, r6.y
        mul r6.y, r6.y, r5.x
        add r6.y, c0.w, -r6.y
        max r6.y, r6.y, c11.y
        rcp r6.y, r6.y
        mul r6.y, r0.x, r6.y
        mul r7.w, r9.x, c5.y
        add r7.w, r7.w, c12.y
        mul r7.w, r7.w, r6.y
        rcp r9.z, c1.x
        mul r7.w, r7.w, r9.z
        mul r9.x, r9.y, c5.y
        add r9.x, -r9.x, c5.w
        mul r9.x, r9.x, r6.y
        rcp r9.y, c1.y
        mul r9.x, r9.x, r9.y
        mov r9.y, r7.w
        mov r9.z, r9.x
        mov r9.w, r6.y
        mov r10.xy, r8.xyxx
        add r10.xy, r3.zwzz, r10.xyxx
        add r8.xy, r3.zwzz, r8.xyxx
        mov r11.xyzw, c6.xyzw
        mov r11.xy, r8.xyxx
        texldl r11.xyzw, r11.xyzw, s1
        add r7.w, r11.x, -c3.x
        mul r7.w, r7.w, c3.y
        mov_sat r7.w, r7.w
        mul r7.w, r7.w, r5.x
        add r7.w, c0.w, -r7.w
        max r7.w, r7.w, c11.y
        rcp r7.w, r7.w
        mul r7.w, r0.x, r7.w
        mul r8.x, r10.x, c5.y
        add r8.x, r8.x, c12.y
        mul r8.x, r8.x, r7.w
        rcp r8.y, c1.x
        mul r8.x, r8.x, r8.y
        mul r8.y, r10.y, c5.y
        add r8.y, -r8.y, c5.w
        mul r8.y, r8.y, r7.w
        rcp r9.x, c1.y
        mul r8.y, r8.y, r9.x
        mov r10.x, r8.x
        mov r10.y, r8.y
        mov r10.z, r7.w
        mov r8.xy, r8.zwzz
        add r8.xy, r3.zwzz, -r8.xyxx
        mov r11.xy, r8.zwzz
        add r11.xy, r3.zwzz, -r11.xyxx
        mov r12.xyzw, c6.xyzw
        mov r12.xy, r11.xyxx
        mov r11.xyzw, r12.xyzw
        texldl r11.xyzw, r11.xyzw, s1
        add r9.x, r11.x, -c3.x
        mul r9.x, r9.x, c3.y
        mov_sat r9.x, r9.x
        mul r9.x, r9.x, r5.x
        add r9.x, c0.w, -r9.x
        max r9.x, r9.x, c11.y
        rcp r9.x, r9.x
        mul r9.x, r0.x, r9.x
        mul r10.w, r8.x, c5.y
        add r10.w, r10.w, c12.y
        mul r10.w, r10.w, r9.x
        rcp r11.x, c1.x
        mul r10.w, r10.w, r11.x
        mul r8.x, r8.y, c5.y
        add r8.x, -r8.x, c5.w
        mul r8.x, r8.x, r9.x
        rcp r8.y, c1.y
        mul r8.x, r8.x, r8.y
        mov r11.x, r10.w
        mov r11.y, r8.x
        mov r11.z, r9.x
        mov r8.xy, r8.zwzz
        add r8.xy, r3.zwzz, r8.xyxx
        add r3.zw, r3.xxzw, r8.xxzw
        mov r12.xyzw, c6.xyzw
        mov r12.xy, r3.zwzz
        texldl r12.xyzw, r12.xyzw, s1
        add r3.z, r12.x, -c3.x
        mul r3.z, r3.z, c3.y
        mov_sat r3.z, r3.z
        mul r3.z, r3.z, r5.x
        add r3.z, c0.w, -r3.z
        max r3.z, r3.z, c11.y
        rcp r3.z, r3.z
        mul r3.z, r0.x, r3.z
        mul r3.w, r8.x, c5.y
        add r3.w, r3.w, c12.y
        mul r3.w, r3.w, r3.z
        rcp r8.z, c1.x
        mul r3.w, r3.w, r8.z
        mul r8.x, r8.y, c5.y
        add r8.x, -r8.x, c5.w
        mul r8.x, r8.x, r3.z
        rcp r8.y, c1.y
        mul r8.x, r8.x, r8.y
        mov r8.y, r3.w
        mov r8.z, r8.x
        mov r8.w, r3.z
        add r3.w, r7.w, -r0.w
        abs r3.w, r3.w
        add r6.y, r0.w, -r6.y
        abs r6.y, r6.y
        add r3.w, r3.w, -r6.y
        cmp r3.w, r3.w, c5.x, c5.w
        add r10.xyz, r10.xyzx, -r7.xyzx
        add r9.yzw, r7.xxyz, -r9.xyzw
        cmp r9.yzw, -r3.w, r9.xyzw, r10.xxyz
        add r3.z, r3.z, -r0.w
        abs r3.z, r3.z
        add r3.w, r0.w, -r9.x
        abs r3.w, r3.w
        add r3.z, r3.z, -r3.w
        cmp r3.z, r3.z, c5.x, c5.w
        mov r8.xyz, r8.yzwy
        add r8.xyz, r8.xyzx, -r7.xyzx
        mov r10.xyz, r11.xyzx
        add r7.xyz, r7.xyzx, -r10.xyzx
        cmp r7.xyz, -r3.z, r7.xyzx, r8.xyzx
        mul r8.xyz, r9.wyzw, r7.yzxy
        mul r7.xyz, r9.zwyz, r7.zxyz
        add r7.xyz, r7.xyzx, -r8.xyzx
        dp3 r3.z, r7.xyzx, r7.xyzx
        add r3.w, -r3.z, c12.z
        cmp r3.w, r3.w, c5.x, c5.w
        max r3.z, r3.z, c12.z
        rsq r3.z, r3.z
        mov r8.xyz, r3.z
        mul r7.xyz, r7.xyzx, r8.xyzx
        cmp r7.xyz, -r3.w, c13.xyzx, r7.xyzx
        cmp r3.z, -r7.z, c5.x, c5.w
        cmp r7.xyz, -r3.z, r7.xyzx, -r7.xyzx
        abs r3.zw, r7.xxyx
        add r3.z, r3.z, -r3.w
        cmp r3.z, r3.z, c5.x, c5.w
        mov r8.x, c0.x
        mov r8.y, c5.x
        mov r8.z, c5.x
        mov r8.w, c0.y
        cmp r3.zw, -r3.z, r8.xxzw, r8.xxxy
        rcp r8.x, c1.x
        rcp r8.y, c1.y
        mul r6.y, c2.y, c13.w
        mul r0.w, r0.w, c14.x
        min r0.w, r6.y, r0.w
        max r0.w, r0.w, c12.w
        rcp r0.w, r0.w
        mul r0.w, r0.w, c5.y
        mov r6.y, c5.x
        mov r7.w, c5.x
        mov r8.z, c5.x
        rep i0.xyzw
            mov r16.x, r8.z
            add r16.x, r16.x, c14.y
            cmp r16.x, r16.x, c5.x, c5.w
            add r16.x, -r16.x, c5.w
            if_ne r16.x, -r16.x
                break
            else
            endif
            mov r16.x, r8.z
            add r16.x, r16.x, c7.y
            mul r16.x, r16.x, c14.z
            frc r17.xyzw, r16.x
            add r16.x, r16.x, -r17.xyzw
            mov r16.y, r8.z
            mul r16.z, r16.x, c14.w
            add r16.y, r16.y, -r16.z
            add r16.y, r16.y, c15.x
            add r16.x, r16.x, c15.x
            mov r14.z, r16.y
            mov r14.w, r16.x
            mov r16.xy, r14.zwzz
            mul r16.xy, r16.xyxx, r0.yzyy
            add r16.xy, v0.xyxx, r16.xyxx
            mad r16.zw, r16.xxxy, r3.xxxy, c9.xxxy
            frc r16.zw, r16.xxzw
            add r16.zw, -r16.xxzw, c9.xxzw
            mad r16.zw, r16.xxzw, c0.xxxy, r16.xxxy
            max r16.zw, r16.xxzw, r4.xxxy
            min r16.zw, r16.xxzw, r4.xxzw
            mov r12.xyzw, c6.xyzw
            mov r12.xy, r16.zwzz
            mov r17.xyzw, r12.xyzw
            texldl r17.xyzw, r17.xyzw, s1
            add r17.x, r17.x, -c3.x
            mul r17.x, r17.x, c3.y
            mov_sat r17.x, r17.x
            mul r17.y, r17.x, r5.x
            add r17.y, c0.w, -r17.y
            max r17.y, r17.y, c11.y
            rcp r10.w, r17.y
            mul r17.z, r0.x, r10.w
            mul r17.w, r16.z, c5.y
            add r17.w, r17.w, c12.y
            mul r17.w, r17.w, r17.z
            mov r18.x, r8.x
            mul r17.w, r17.w, r18.x
            mul r18.x, r16.w, c5.y
            add r18.x, -r18.x, c5.w
            mul r18.x, r18.x, r17.z
            mov r18.y, r8.xyxx
            mul r18.x, r18.x, r18.y
            mov r11.x, r17.w
            mov r11.y, r18.x
            mov r11.z, r17.z
            mov r10.x, r17.w
            mov r10.y, r18.x
            mov r10.z, r17.z
            mov r17.zw, r14.xxzw
            abs r17.zw, r17.xxzw
            add r18.x, -r17.z, c10.x
            cmp r18.x, r18.x, c5.x, c5.w
            cmp r18.x, -r18.x, c5.w, c7.y
            add r17.z, -r17.w, c10.x
            cmp r17.z, r17.z, c5.x, c5.w
            cmp r17.z, -r17.z, c5.w, c7.y
            mul r17.z, r18.x, r17.z
            mov r18.xyz, r10.xyzx
            mov r19.xyz, r6.xzwx
            add r18.xyz, r18.xyzx, -r19.xyzx
            dp3 r17.w, r18.xyzx, r7.xyzx
            abs r17.w, r17.w
            mul r17.w, -r17.w, r0.w
            exp r8.w, r17.w
            mul r17.z, r17.z, r8.w
            add r18.xy, r16.zwzz, r3.zwzz
            mov r9.xyzw, c6.xyzw
            mov r9.xy, r18.xyxx
            mov r19.xyzw, r9.xyzw
            texldl r19.xyzw, r19.xyzw, s1
            add r17.w, r19.x, -c3.x
            mul r17.w, r17.w, c3.y
            mov_sat r17.w, r17.w
            mul r17.w, r17.w, r5.x
            add r17.w, c0.w, -r17.w
            max r17.w, r17.w, c11.y
            rcp r14.y, r17.w
            mul r17.w, r0.x, r14.y
            mul r18.z, r18.x, c5.y
            add r18.z, r18.z, c12.y
            mul r18.z, r18.z, r17.w
            mov r18.w, r8.x
            mul r18.z, r18.z, r18.w
            mul r18.x, r18.y, c5.y
            add r18.x, -r18.x, c5.w
            mul r18.x, r18.x, r17.w
            mov r18.y, r8.xyxx
            mul r18.x, r18.x, r18.y
            mov r11.x, r18.z
            mov r11.y, r18.x
            mov r11.z, r17.w
            mov r18.xyz, r11.xyzx
            mov r19.xyz, r10.xyzx
            add r18.xyz, r18.xyzx, -r19.xyzx
            dp3 r17.w, r18.xyzx, r7.xyzx
            mul r18.w, r17.w, c5.y
            mul r17.w, r18.w, r17.w
            dp3 r18.x, r18.xyzx, r18.xyzx
            rcp r11.w, r18.x
            mul r17.w, r17.w, r11.w
            add r17.w, -r17.w, c5.w
            mov_sat r17.w, r17.w
            mul r17.z, r17.z, r17.w
            add r17.x, r17.x, c8.w
            cmp r17.x, r17.x, c5.x, c5.w
            mov r17.w, r5.w
            cmp r17.w, -r5.y, r17.w, c5.x
            mov r5.w, r17.w
            if_ne r5.z, -r5.z
                mov r15.xyzw, c6.xyzw
                mov r15.xy, r16.zwzz
                mov r18.xyzw, r15.xyzw
                texldl r18.xyzw, r18.xyzw, s3
                add r16.z, c0.z, -r18.x
                cmp r16.z, r16.z, c5.x, c5.w
                rcp r14.x, r17.y
                mul r16.w, r0.x, r14.x
                mul r17.y, r18.x, c11.w
                max r17.y, r17.y, c11.z
                add r16.w, r16.w, r17.y
                add r16.w, r16.w, -r18.x
                cmp r16.w, r16.w, c5.x, c5.w
                add r16.w, -r16.w, c5.w
                min r16.z, r16.z, r16.w
                add r16.w, -r18.y, c12.x
                cmp r16.w, r16.w, c5.x, c5.w
                min r16.z, r16.z, r16.w
                mov r5.w, r16.z
            else
            endif
            mov r16.z, r5.w
            add r16.z, -r16.z, c5.w
            min r16.z, r17.x, r16.z
            cmp r16.z, -r16.z, c5.x, c5.w
            mul r16.z, r17.z, r16.z
            mov r13.xyzw, c6.xyzw
            mov r13.xy, r16.xyxx
            mov r17.xyzw, r13.xyzw
            texldl r17.xyzw, r17.xyzw, s2
            mul r16.x, r17.w, r16.z
            mov r16.y, r6.y
            add r16.x, r16.y, r16.x
            mov r6.y, r16.x
            mov r16.x, r7.w
            add r16.x, r16.x, r16.z
            mov r7.w, r16.x
            mov r16.x, r8.z
            add r16.x, r16.x, c5.w
            mov r8.z, r16.x
        endrep
        mov r0.x, r7.w
        add r0.x, -r0.x, c11.y
        cmp r0.x, r0.x, c5.x, c5.w
        mov r0.y, r6.y
        mov r0.z, r7.w
        rcp r0.z, r0.z
        mul r0.y, r0.y, r0.z
        cmp r0.x, -r0.x, r1.w, r0.y
        mov r1.w, r0.x
        mov r0.xyzw, r1.xyzw
        mov r2.xyzw, r0.xyzw
    else
    endif
else
endif
mov oC0.xyzw, r2.xyzw
