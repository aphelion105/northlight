ps_3_0
dcl_texcoord0 v0
def c73 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c74 = -1.00000000e+00, -1.00000000e+00, 5.00000000e-01, 5.00000000e-01
def c75 = -5.00000000e-01, 1.00000000e+00, -9.99989986e-01, 9.99999975e-06
def c76 = 1.20000001e-02, 6.99999975e-04, 3.00000003e-03, 3.99999991e-02
def c77 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c78 = 2.00000000e+00, 2.00000000e+00, -2.00000000e+00, 1.50000006e-01
def c79 = -1.00000000e+00, 1.00000000e+00, 5.72999992e+01, 1.00000001e-01
def c80 = -5.00000000e-01, -5.00000000e-01, 6.66666687e-01, -4.10000000e+01
def c81 = -2.50000000e+01, 2.00000003e-01, 5.00000000e+00, 2.50000000e-01
def c82 = -2.00000000e+00, -2.00000000e+00, 4.00000000e+00, 5.00000007e-02
def c83 = -1.50000000e+00, -1.50000000e+00, -8.10000000e+01, 9.99999975e-05
def c84 = 8.33333397e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s12
dcl_2d s1
dcl_2d s14
dcl_2d s11
mov r0.xyzw, c73.xyzw
mov r0.xy, v0.xyxx
texldl r0.xyzw, r0.xyzw, s14
mul r1.xy, v0.xyxx, c33.xyxx
frc r2.xyzw, r1.xyxx
add r1.xy, r1.xyxx, -r2.xyzw
add r1.zw, c33.xxxy, c74.xxxy
max r1.xy, r1.xyxx, c73.xyxx
min r1.xy, r1.xyxx, r1.zwzz
add r1.xy, r1.xyxx, c74.zwzz
mul r1.xy, r1.xyxx, c0.xyxx
mov r2.xyzw, c73.xyzw
mov r2.xy, r1.xyxx
texldl r2.xyzw, r2.xyzw, s1
add r1.z, r2.x, -c1.w
mul r1.z, r1.z, c2.x
mov_sat r1.z, r1.z
add r1.w, c72.x, c75.x
cmp r1.w, r1.w, c73.x, c75.y
add r2.x, r1.z, c75.z
cmp r2.x, r2.x, c73.x, c75.y
add r2.x, -r2.x, c75.y
max r1.w, r1.w, r2.x
add r2.x, c30.x, c75.x
cmp r2.x, r2.x, c73.x, c75.y
mov r2.y, r2.z
cmp r2.y, -r2.x, r2.y, c73.x
mov r2.z, r2.y
add r2.x, -r2.x, c75.y
if_ne r2.x, -r2.x
    mov r3.xyzw, c73.xyzw
    mov r3.xy, r1.xyxx
    texldl r3.xyzw, r3.xyzw, s11
    add r2.x, c0.z, -r3.x
    cmp r2.x, r2.x, c73.x, c75.y
    mul r2.y, c0.z, c0.w
    add r2.w, c0.w, -c0.z
    mul r2.w, r1.z, r2.w
    add r2.w, c0.w, -r2.w
    max r2.w, r2.w, c75.w
    rcp r2.w, r2.w
    mul r2.y, r2.y, r2.w
    mul r2.w, r3.x, c76.y
    max r2.w, r2.w, c76.x
    add r2.y, r2.y, r2.w
    add r2.y, r2.y, -r3.x
    cmp r2.y, r2.y, c73.x, c75.y
    add r2.y, -r2.y, c75.y
    min r2.x, r2.x, r2.y
    add r2.y, -r3.y, c76.z
    cmp r2.y, r2.y, c73.x, c75.y
    min r2.x, r2.x, r2.y
    cmp r2.x, -r2.x, c73.x, r3.x
    mov r2.z, r2.x
else
endif
mov r2.x, r2.z
cmp r2.x, -r2.x, c73.x, c75.y
max r1.w, r1.w, r2.x
mov r2.xyzw, r3.xyzw
cmp r2.xyzw, -r1.w, r2.xyzw, c77.xyzw
mov r3.xyzw, r2.xyzw
add r4.x, -r1.w, c75.y
if_ne r4.x, -r4.x
    dp3 r4.x, r0.xyzx, c16.xyzx
    abs r4.y, r4.x
    mov_sat r4.y, r4.y
    add r4.y, -r4.y, c75.y
    mul r4.y, r4.y, c78.x
    mul r4.y, r4.y, c72.y
    mul r4.y, r4.y, c72.z
    add r4.y, r4.y, c76.w
    mul r4.z, c0.z, c0.w
    add r4.w, c0.w, -c0.z
    mul r1.z, r1.z, r4.w
    add r1.z, c0.w, -r1.z
    max r1.z, r1.z, c75.w
    rcp r1.z, r1.z
    mul r1.z, r4.z, r1.z
    mul r4.zw, c0.xxxy, c74.xxzw
    add r1.xy, r1.xyxx, -r4.zwzz
    mul r1.xy, r1.xyxx, c78.yzyy
    add r1.xy, r1.xyxx, c79.xyxx
    rcp r4.z, c1.x
    rcp r4.w, c1.y
    mul r1.xy, r1.xyxx, r4.zwzz
    mov r5.xy, r1.xyxx
    mov r5.z, c1.z
    mul r1.xyz, r5.xyzx, r1.z
    mul r5.xyz, r1.x, c3.xyzx
    mul r6.xyz, r1.y, c4.xyzx
    add r5.xyz, r5.xyzx, r6.xyzx
    mul r1.xyz, r1.z, c5.xyzx
    add r1.xyz, r5.xyzx, r1.xyzx
    add r1.xyz, r1.xyzx, c6.xyzx
    mul r4.y, r4.y, r0.w
    mul r4.yzw, r0.xxyz, r4.y
    add r1.xyz, r1.xyzx, r4.yzwy
    mul r4.yzw, r1.x, c68.xxyz
    mul r5.xyz, r1.y, c69.xyzx
    add r4.yzw, r4.xyzw, r5.xxyz
    mul r1.xyz, r1.z, c70.xyzx
    add r1.xyz, r4.yzwy, r1.xyzx
    add r1.xyz, r1.xyzx, c71.xyzx
    abs r4.yz, r1.xxyx
    max r4.y, r4.y, r4.z
    add r4.z, r4.y, c74.x
    cmp r4.z, r4.z, c73.x, c75.y
    add r4.z, -r4.z, c75.y
    cmp r4.w, -r1.z, c73.x, c75.y
    add r4.w, -r4.w, c75.y
    max r4.z, r4.z, r4.w
    add r4.w, r1.z, c74.x
    cmp r4.w, r4.w, c73.x, c75.y
    add r4.w, -r4.w, c75.y
    max r4.z, r4.z, r4.w
    cmp r2.xyzw, -r4.z, r2.xyzw, c77.xyzw
    mov r3.xyzw, r2.xyzw
    cmp r1.w, -r4.z, r1.w, c75.y
    add r4.z, -r1.w, c75.y
    if_ne r4.z, -r4.z
        mul r4.z, r1.x, c74.z
        add r4.z, r4.z, c74.z
        mul r4.w, r1.y, c74.z
        add r4.w, -r4.w, c74.z
        mov r5.x, r4.z
        mov r5.y, r4.w
        mov r4.zw, r5.xxxy
        mul r5.x, c72.y, c74.z
        add r4.zw, r4.xxzw, r5.x
        add r1.x, r1.z, -c24.x
        mov r5.x, c68.x
        mov r5.y, c69.x
        mov r5.z, c70.x
        dp3 r1.y, r0.xyzx, r5.xyzx
        mov r5.x, c68.y
        mov r5.y, c69.y
        mov r5.z, c70.y
        dp3 r1.z, r0.xyzx, r5.xyzx
        mov r5.x, r1.y
        mov r5.y, -r1.z
        mov r1.yz, r5.xxyx
        mul r1.yz, r1.xyzx, c72.z
        mul r1.yz, r1.xyzx, c72.z
        mul r0.x, r0.w, c78.x
        mul r0.x, r0.x, c35.x
        cmp r0.y, r4.x, c73.x, c75.y
        abs r0.zw, r4.x
        max r4.x, r0.z, c78.w
        max r0.z, r0.w, c78.w
        cmp r0.y, -r0.y, r0.z, -r4.x
        rcp r0.y, r0.y
        mul r0.x, r0.x, r0.y
        mul r0.xy, r1.yzyy, r0.x
        mul r0.z, c72.z, c79.z
        mul r0.z, r0.z, c35.x
        max r0.xy, r0.xyxx, -r0.z
        min r0.xy, r0.xyxx, r0.z
        mul r0.z, c72.y, c78.x
        mul r0.z, r0.z, c72.z
        rcp r1.y, c72.y
        rcp r1.z, c72.y
        mul r1.yz, r4.xzwx, r1.xyzx
        add r1.yz, r1.xyzx, c80.xxyx
        rcp r0.w, r0.z
        mul r0.w, r0.w, c79.w
        mul r0.w, r0.w, c80.z
        add r5.xy, r1.yzyy, c74.zwzz
        frc r6.xyzw, r5.xyxx
        add r5.xy, r5.xyxx, -r6.xyzw
        mov r4.x, c73.x
        mov r5.z, c73.x
        mov r5.w, c73.x
        rep i0.xyzw
            mov r7.z, r5.w
            add r7.z, r7.z, c80.w
            cmp r7.z, r7.z, c73.x, c75.y
            add r7.z, -r7.z, c75.y
            if_ne r7.z, -r7.z
                break
            else
            endif
            mov r7.z, r5.w
            add r7.z, r7.z, c81.x
            cmp r7.z, r7.z, c73.x, c75.y
            if_ne r7.z, -r7.z
                mov r7.z, r5.w
                add r7.z, r7.z, c74.z
                mul r7.z, r7.z, c81.y
                frc r9.xyzw, r7.z
                add r7.z, r7.z, -r9.x
                mov r7.w, r5.w
                mul r9.x, r7.z, c81.z
                add r7.w, r7.w, -r9.x
                mov r6.x, r7.w
                mov r6.y, r7.z
                mov r7.zw, r6.xxxy
                add r7.zw, r5.xxxy, r7.xxzw
                add r7.zw, r7.xxzw, c82.xxxy
                mov r7.xy, r7.zwzz
            else
                mov r7.z, r5.w
                add r7.z, r7.z, c81.x
                add r7.w, r7.z, c74.z
                mul r7.w, r7.w, c81.w
                frc r9.xyzw, r7.w
                add r7.w, r7.w, -r9.x
                mul r9.x, r7.w, c82.z
                add r7.z, r7.z, -r9.x
                mov r6.z, r7.z
                mov r6.w, r7.w
                mov r7.zw, r6.xxzw
                add r7.zw, r7.xxzw, c83.xxxy
                mul r7.zw, r7.xxzw, r0.w
                add r7.zw, r1.xxyz, r7.xxzw
                add r7.zw, r7.xxzw, c74.xxzw
                frc r9.xyzw, r7.zwzz
                add r7.zw, r7.xxzw, -r9.xxxy
                mov r7.xy, r7.zwzz
            endif
            mov r7.zw, r7.xxxy
            add r7.zw, r7.xxzw, c74.xxzw
            mul r7.zw, r7.xxzw, c72.y
            mov r8.xyzw, c73.xyzw
            mov r8.xy, r7.zwzz
            mov r9.xyzw, r8.xyzw
            texldl r9.xyzw, r9.xyzw, s12
            add r7.zw, r7.xxzw, -r4.xxzw
            dp2add r7.z, r0.xyxx, r7.zwzz, c73.x
            add r7.z, r1.x, r7.z
            add r7.z, r9.x, -r7.z
            cmp r7.z, r7.z, c73.x, c75.y
            mov r7.w, r4.x
            add r7.w, r7.w, r9.x
            mov r9.x, r4.x
            cmp r7.w, -r7.z, r9.x, r7.w
            mov r4.x, r7.w
            mov r7.w, r5.z
            add r7.w, r7.w, c75.y
            mov r9.x, r5.z
            cmp r7.z, -r7.z, r9.x, r7.w
            mov r5.z, r7.z
            mov r7.z, r5.w
            add r7.z, r7.z, c75.y
            mov r5.w, r7.z
        endrep
        mov r0.w, r5.z
        add r0.w, r0.w, c74.x
        cmp r0.w, r0.w, c73.x, c75.y
        cmp r2.xyzw, -r0.w, r2.xyzw, c77.xyzw
        mov r3.xyzw, r2.xyzw
        cmp r0.w, -r0.w, r1.w, c75.y
        add r0.w, -r0.w, c75.y
        if_ne r0.w, -r0.w
            mov r0.w, r4.x
            mov r1.w, r5.z
            rcp r1.w, r1.w
            mul r0.w, r0.w, r1.w
            add r0.w, r1.x, -r0.w
            mul r0.w, r0.w, c76.w
            rcp r1.w, c35.x
            mul r0.w, r0.w, r1.w
            max r0.w, r0.w, c82.w
            min r0.w, r0.w, c79.w
            rcp r0.z, r0.z
            mul r0.z, r0.w, r0.z
            mul r0.w, r0.z, c78.x
            frc r2.xyzw, r0.w
            add r0.w, r0.w, -r2.x
            add r0.w, r0.w, c75.y
            add r2.xy, r1.yzyy, -r0.z
            frc r5.xyzw, r2.xyxx
            add r2.xy, r2.xyxx, -r5.xyzw
            add r2.xy, r2.xyxx, c77.xyxx
            mov r1.w, c73.x
            mov r2.z, c73.x
            mov r2.w, c73.x
            rep i0.xyzw
                mov r6.z, r2.w
                add r6.z, r6.z, c83.z
                cmp r6.z, r6.z, c73.x, c75.y
                add r6.z, -r6.z, c75.y
                if_ne r6.z, -r6.z
                    break
                else
                endif
                mov r6.z, r2.w
                mul r6.w, r0.w, r0.w
                add r6.z, r6.z, -r6.w
                cmp r6.z, r6.z, c73.x, c75.y
                add r6.z, -r6.z, c75.y
                if_ne r6.z, -r6.z
                    break
                else
                endif
                mov r6.z, r2.w
                add r6.z, r6.z, c74.z
                rcp r4.x, r0.w
                mul r6.z, r6.z, r4.x
                frc r7.xyzw, r6.z
                add r6.z, r6.z, -r7.x
                mov r6.w, r2.w
                mul r7.x, r6.z, r0.w
                add r6.w, r6.w, -r7.x
                mov r6.x, r6.w
                mov r6.y, r6.z
                mov r6.zw, r6.xxxy
                add r6.zw, r2.xxxy, r6.xxzw
                add r7.xy, r6.zwzz, c74.zwzz
                mul r7.xy, r7.xyxx, c72.y
                add r6.zw, r6.xxzw, -r1.xxyz
                abs r6.zw, r6.xxzw
                add r6.zw, r0.z, -r6.xxzw
                max r6.zw, r6.xxzw, c73.xxxy
                mul r6.z, r6.z, r6.w
                mov r5.xyzw, c73.xyzw
                mov r5.xy, r7.xyxx
                mov r8.xyzw, r5.xyzw
                texldl r8.xyzw, r8.xyzw, s12
                add r7.xy, r7.xyxx, -r4.zwzz
                dp2add r6.w, r0.xyxx, r7.xyxx, c73.x
                add r6.w, r1.x, r6.w
                add r6.w, r8.x, -r6.w
                cmp r6.w, r6.w, c73.x, c75.y
                add r6.w, -r6.w, c75.y
                cmp r6.w, -r6.w, c73.x, c75.y
                mul r6.w, r6.w, r6.z
                mov r7.x, r1.w
                add r6.w, r7.x, r6.w
                mov r1.w, r6.w
                mov r6.w, r2.z
                add r6.z, r6.w, r6.z
                mov r2.z, r6.z
                mov r6.z, r2.w
                add r6.z, r6.z, c75.y
                mov r2.w, r6.z
            endrep
            mov r0.x, r1.w
            mov r0.y, r2.z
            max r0.y, r0.y, c83.w
            rcp r0.y, r0.y
            mul r0.x, r0.x, r0.y
            add r0.y, -r4.y, c75.y
            mul r0.y, r0.y, c84.x
            mov_sat r0.y, r0.y
            add r0.x, r0.x, c74.x
            mul r0.x, r0.y, r0.x
            add r0.x, r0.x, c75.y
            mov r0.xyz, r0.x
            mov r0.w, c75.y
            mov r3.xyzw, r0.xyzw
        else
        endif
    else
    endif
else
endif
mov oC0.xyzw, r3.xyzw
