ps_3_0
dcl_texcoord0 v0
def c73 = -1.00000000e+00, -1.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c74 = 5.00000000e-01, 5.00000000e-01, -5.00000000e-01, 1.00000000e+00
def c75 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c76 = -9.99989986e-01, 9.99999975e-06, 2.00000000e+00, -2.00000000e+00
def c77 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c78 = -1.00000000e+00, 1.00000000e+00, -1.00999999e+00, 1.20000001e-02
def c79 = 6.99999975e-04, 3.00000003e-03, 3.99999991e-02, 1.50000006e-01
def c80 = 5.72999992e+01, -5.00000000e-01, -5.00000000e-01, 1.00000001e-01
def c81 = 6.66666687e-01, 2.50000000e+00, 1.50000000e+00, 1.60000000e+01
def c82 = 6.25000000e-02, 6.25000000e-02, -4.10000000e+01, -2.50000000e+01
def c83 = 2.00000003e-01, 5.00000000e+00, -2.00000000e+00, -2.00000000e+00
def c84 = 2.50000000e-01, 4.00000000e+00, -1.50000000e+00, -1.50000000e+00
def c85 = 5.00000007e-02, -8.10000000e+01, 9.99999975e-05, 8.33333397e+00
defi i0 = 255, 0, 0, 0
dcl_2d s12
dcl_2d s10
dcl_2d s1
dcl_2d s14
dcl_2d s11
mul r0.xy, v0.xyxx, c33.xyxx
frc r1.xyzw, r0.xyxx
add r0.xy, r0.xyxx, -r1.xyzw
add r0.zw, c33.xxxy, c73.xxxy
max r0.xy, r0.xyxx, c73.zwzz
min r0.xy, r0.xyxx, r0.zwzz
add r0.xy, r0.xyxx, c74.xyxx
mul r0.xy, r0.xyxx, c0.xyxx
mov r1.xyzw, c75.xyzw
mov r1.xy, r0.xyxx
texldl r1.xyzw, r1.xyzw, s1
add r0.z, r1.x, -c1.w
mul r0.z, r0.z, c2.x
mov_sat r0.z, r0.z
add r0.w, c72.x, c74.z
cmp r0.w, r0.w, c73.z, c74.w
add r1.x, r0.z, c76.x
cmp r1.x, r1.x, c73.z, c74.w
add r1.x, -r1.x, c74.w
max r0.w, r0.w, r1.x
mov r1.xyzw, r2.xyzw
cmp r1.xyzw, -r0.w, r1.xyzw, c77.xyzw
mov r2.xyzw, r1.xyzw
add r3.x, -r0.w, c74.w
if_ne r3.x, -r3.x
    mul r3.x, c0.z, c0.w
    add r3.y, c0.w, -c0.z
    mul r0.z, r0.z, r3.y
    add r0.z, c0.w, -r0.z
    max r0.z, r0.z, c76.y
    rcp r3.y, r0.z
    mul r3.y, r3.x, r3.y
    mul r3.zw, c0.xxxy, c74.xxxy
    add r3.zw, r0.xxxy, -r3.xxzw
    mul r3.zw, r3.xxzw, c76.xxzw
    add r3.zw, r3.xxzw, c78.xxxy
    rcp r4.x, c1.x
    rcp r4.y, c1.y
    mul r3.zw, r3.xxzw, r4.xxxy
    mov r4.xy, r3.zwzz
    mov r4.z, c1.z
    mul r3.yzw, r4.xxyz, r3.y
    mul r4.xyz, r3.y, c3.xyzx
    mul r5.xyz, r3.z, c4.xyzx
    add r4.xyz, r4.xyzx, r5.xyzx
    mul r3.yzw, r3.w, c5.xxyz
    add r3.yzw, r4.xxyz, r3.xyzw
    add r3.yzw, r3.xyzw, c6.xxyz
    mul r4.xyz, r3.y, c68.xyzx
    mul r5.xyz, r3.z, c69.xyzx
    add r4.xyz, r4.xyzx, r5.xyzx
    mul r5.xyz, r3.w, c70.xyzx
    add r4.xyz, r4.xyzx, r5.xyzx
    add r4.xyz, r4.xyzx, c71.xyzx
    abs r4.xy, r4.xyxx
    max r4.x, r4.x, r4.y
    add r4.x, r4.x, c78.z
    cmp r4.x, r4.x, c73.z, c74.w
    add r4.x, -r4.x, c74.w
    add r4.y, c30.x, c74.z
    cmp r4.y, r4.y, c73.z, c74.w
    mov r4.z, r4.w
    cmp r4.z, -r4.y, r4.z, c73.z
    mov r4.w, r4.z
    add r4.y, -r4.y, c74.w
    if_ne r4.y, -r4.y
        mov r5.xyzw, c75.xyzw
        mov r5.xy, r0.xyxx
        texldl r5.xyzw, r5.xyzw, s11
        add r0.x, c0.z, -r5.x
        cmp r0.x, r0.x, c73.z, c74.w
        rcp r0.y, r0.z
        mul r0.y, r3.x, r0.y
        mul r0.z, r5.x, c79.x
        max r0.z, r0.z, c78.w
        add r0.y, r0.y, r0.z
        add r0.y, r0.y, -r5.x
        cmp r0.y, r0.y, c73.z, c74.w
        add r0.y, -r0.y, c74.w
        min r0.x, r0.x, r0.y
        add r0.y, -r5.y, c79.y
        cmp r0.y, r0.y, c73.z, c74.w
        min r0.x, r0.x, r0.y
        cmp r0.x, -r0.x, c73.z, r5.x
        mov r4.w, r0.x
    else
    endif
    mov r0.x, r4.w
    cmp r0.x, -r0.x, c73.z, c74.w
    max r0.x, r4.x, r0.x
    cmp r1.xyzw, -r0.x, r1.xyzw, c77.xyzw
    mov r2.xyzw, r1.xyzw
    cmp r0.x, -r0.x, r0.w, c74.w
    add r0.y, -r0.x, c74.w
    if_ne r0.y, -r0.y
        mov r4.xyzw, c75.xyzw
        mov r4.xy, v0.xyxx
        texldl r4.xyzw, r4.xyzw, s14
        dp3 r0.y, r4.xyzx, c16.xyzx
        abs r0.z, r0.y
        mov_sat r0.z, r0.z
        add r0.z, -r0.z, c74.w
        mul r0.z, r0.z, c76.z
        mul r0.z, r0.z, c72.y
        mul r0.z, r0.z, c72.z
        add r0.z, r0.z, c79.z
        mul r0.z, r0.z, r4.w
        mul r5.xyz, r4.xyzx, r0.z
        add r3.xyz, r3.yzwy, r5.xyzx
        mul r5.xyz, r3.x, c68.xyzx
        mul r6.xyz, r3.y, c69.xyzx
        add r5.xyz, r5.xyzx, r6.xyzx
        mul r3.xyz, r3.z, c70.xyzx
        add r3.xyz, r5.xyzx, r3.xyzx
        add r3.xyz, r3.xyzx, c71.xyzx
        abs r0.zw, r3.xxxy
        max r0.z, r0.z, r0.w
        add r0.w, r0.z, c73.x
        cmp r0.w, r0.w, c73.z, c74.w
        add r0.w, -r0.w, c74.w
        cmp r3.w, -r3.z, c73.z, c74.w
        add r3.w, -r3.w, c74.w
        max r0.w, r0.w, r3.w
        add r3.w, r3.z, c73.x
        cmp r3.w, r3.w, c73.z, c74.w
        add r3.w, -r3.w, c74.w
        max r0.w, r0.w, r3.w
        cmp r1.xyzw, -r0.w, r1.xyzw, c77.xyzw
        mov r2.xyzw, r1.xyzw
        cmp r0.x, -r0.w, r0.x, c74.w
        add r0.w, -r0.x, c74.w
        if_ne r0.w, -r0.w
            mul r0.w, r3.x, c74.x
            add r0.w, r0.w, c74.x
            mul r3.w, r3.y, c74.x
            add r3.w, -r3.w, c74.x
            mov r5.x, r0.w
            mov r5.y, r3.w
            mul r0.w, c72.y, c74.x
            add r5.xy, r5.xyxx, r0.w
            add r0.w, r3.z, -c24.x
            mov r3.x, c68.x
            mov r3.y, c69.x
            mov r3.z, c70.x
            dp3 r3.x, r4.xyzx, r3.xyzx
            mov r3.y, c68.y
            mov r3.z, c69.y
            mov r3.w, c70.y
            dp3 r3.y, r4.xyzx, r3.yzwy
            mov r3.z, -r3.y
            mov r3.xy, r3.xzxx
            mul r3.xy, r3.xyxx, c72.z
            mul r3.xy, r3.xyxx, c72.z
            mul r3.z, r4.w, c76.z
            mul r3.z, r3.z, c35.x
            cmp r3.w, r0.y, c73.z, c74.w
            abs r4.xy, r0.y
            max r0.y, r4.x, c79.w
            max r4.x, r4.y, c79.w
            cmp r0.y, -r3.w, r4.x, -r0.y
            rcp r0.y, r0.y
            mul r0.y, r3.z, r0.y
            mul r3.xy, r3.xyxx, r0.y
            mul r0.y, c72.z, c80.x
            mul r0.y, r0.y, c35.x
            max r3.xy, r3.xyxx, -r0.y
            min r3.xy, r3.xyxx, r0.y
            mul r0.y, c72.y, c76.z
            mul r0.y, r0.y, c72.z
            rcp r3.z, c72.y
            rcp r3.w, c72.y
            mul r3.zw, r5.xxxy, r3.xxzw
            add r3.zw, r3.xxzw, c80.xxyz
            rcp r4.x, r0.y
            mul r4.x, r4.x, c80.w
            mul r4.x, r4.x, c81.x
            mul r4.y, r4.x, c81.z
            add r4.y, r4.y, c74.x
            max r4.y, r4.y, c81.y
            add r4.zw, r3.xxzw, -r4.y
            mul r4.zw, r4.xxzw, c82.xxxy
            frc r6.xyzw, r4.zwzz
            add r4.zw, r4.xxzw, -r6.xxxy
            add r4.zw, r4.xxzw, c74.xxxy
            mul r5.z, c72.y, c81.w
            mul r4.zw, r4.xxzw, r5.z
            add r6.xy, r3.zwzz, r4.y
            mul r6.xy, r6.xyxx, c82.xyxx
            frc r7.xyzw, r6.xyxx
            add r6.xy, r6.xyxx, -r7.xyzw
            add r6.xy, r6.xyxx, c74.xyxx
            mul r5.zw, r6.xxxy, r5.z
            mov r6.xyzw, c75.xyzw
            mov r6.xy, r4.zwzz
            texldl r6.xyzw, r6.xyzw, s10
            mov r7.x, r5.z
            mov r7.y, r4.w
            mov r7.z, c73.z
            mov r7.w, c73.z
            mov r8.xyzw, c75.xyzw
            mov r8.xy, r7.xyxx
            mov r7.xyzw, r8.xyzw
            texldl r7.xyzw, r7.xyzw, s10
            min r6.x, r6.x, r7.x
            mov r7.x, r4.z
            mov r7.y, r5.w
            mov r7.z, c73.z
            mov r7.w, c73.z
            mov r8.xyzw, c75.xyzw
            mov r8.xy, r7.xyxx
            mov r7.xyzw, r8.xyzw
            texldl r7.xyzw, r7.xyzw, s10
            mov r8.xyzw, c75.xyzw
            mov r8.xy, r5.zwzz
            texldl r8.xyzw, r8.xyzw, s10
            min r4.z, r7.x, r8.x
            min r4.z, r6.x, r4.z
            abs r5.zw, r3.xxxy
            add r4.w, r5.z, r5.w
            mul r4.y, r4.w, r4.y
            mul r4.y, r4.y, c72.y
            add r4.y, r0.w, r4.y
            add r4.y, r4.y, c76.y
            add r4.y, r4.z, -r4.y
            cmp r4.y, r4.y, c73.z, c74.w
            add r4.y, -r4.y, c74.w
            cmp r1.xyzw, -r4.y, r1.xyzw, c77.xyzw
            mov r2.xyzw, r1.xyzw
            cmp r0.x, -r4.y, r0.x, c74.w
            add r4.y, -r0.x, c74.w
            if_ne r4.y, -r4.y
                add r4.yz, r3.xzwx, c74.xxyx
                frc r6.xyzw, r4.yzyy
                add r4.yz, r4.xyzx, -r6.xxyx
                mov r4.w, c73.z
                mov r5.z, c73.z
                mov r5.w, c73.z
                rep i0.xyzw
                    mov r8.z, r5.w
                    add r8.z, r8.z, c82.z
                    cmp r8.z, r8.z, c73.z, c74.w
                    add r8.z, -r8.z, c74.w
                    if_ne r8.z, -r8.z
                        break
                    else
                    endif
                    mov r8.z, r5.w
                    add r8.z, r8.z, c82.w
                    cmp r8.z, r8.z, c73.z, c74.w
                    if_ne r8.z, -r8.z
                        mov r8.z, r5.w
                        add r8.z, r8.z, c74.x
                        mul r8.z, r8.z, c83.x
                        frc r9.xyzw, r8.z
                        add r8.z, r8.z, -r9.x
                        mov r8.w, r5.w
                        mul r9.x, r8.z, c83.y
                        add r8.w, r8.w, -r9.x
                        mov r8.x, r8.w
                        mov r8.y, r8.z
                        mov r8.zw, r8.xxxy
                        add r8.zw, r4.xxyz, r8.xxzw
                        add r8.zw, r8.xxzw, c83.xxzw
                        mov r7.zw, r8.xxzw
                    else
                        mov r8.z, r5.w
                        add r8.z, r8.z, c82.w
                        add r8.w, r8.z, c74.x
                        mul r8.w, r8.w, c84.x
                        frc r9.xyzw, r8.w
                        add r8.w, r8.w, -r9.x
                        mul r9.x, r8.w, c84.y
                        add r8.z, r8.z, -r9.x
                        mov r7.x, r8.z
                        mov r7.y, r8.w
                        mov r8.zw, r7.xxxy
                        add r8.zw, r8.xxzw, c84.xxzw
                        mul r8.zw, r8.xxzw, r4.x
                        add r8.zw, r3.xxzw, r8.xxzw
                        add r8.zw, r8.xxzw, c74.xxxy
                        frc r9.xyzw, r8.zwzz
                        add r8.zw, r8.xxzw, -r9.xxxy
                        mov r7.zw, r8.xxzw
                    endif
                    mov r8.zw, r7.xxzw
                    add r8.zw, r8.xxzw, c74.xxxy
                    mul r8.zw, r8.xxzw, c72.y
                    mov r6.xyzw, c75.xyzw
                    mov r6.xy, r8.zwzz
                    mov r9.xyzw, r6.xyzw
                    texldl r9.xyzw, r9.xyzw, s12
                    add r8.zw, r8.xxzw, -r5.xxxy
                    dp2add r8.z, r3.xyxx, r8.zwzz, c73.z
                    add r8.z, r0.w, r8.z
                    add r8.z, r9.x, -r8.z
                    cmp r8.z, r8.z, c73.z, c74.w
                    mov r8.w, r4.w
                    add r8.w, r8.w, r9.x
                    mov r9.x, r4.w
                    cmp r8.w, -r8.z, r9.x, r8.w
                    mov r4.w, r8.w
                    mov r8.w, r5.z
                    add r8.w, r8.w, c74.w
                    mov r9.x, r5.z
                    cmp r8.z, -r8.z, r9.x, r8.w
                    mov r5.z, r8.z
                    mov r8.z, r5.w
                    add r8.z, r8.z, c74.w
                    mov r5.w, r8.z
                endrep
                mov r4.x, r5.z
                add r4.x, r4.x, c73.x
                cmp r4.x, r4.x, c73.z, c74.w
                cmp r1.xyzw, -r4.x, r1.xyzw, c77.xyzw
                mov r2.xyzw, r1.xyzw
                cmp r0.x, -r4.x, r0.x, c74.w
                add r0.x, -r0.x, c74.w
                if_ne r0.x, -r0.x
                    mov r0.x, r4.w
                    mov r1.x, r5.z
                    rcp r1.x, r1.x
                    mul r0.x, r0.x, r1.x
                    add r0.x, r0.w, -r0.x
                    mul r0.x, r0.x, c79.z
                    rcp r1.x, c35.x
                    mul r0.x, r0.x, r1.x
                    max r0.x, r0.x, c85.x
                    min r0.x, r0.x, c80.w
                    rcp r0.y, r0.y
                    mul r0.x, r0.x, r0.y
                    mul r0.y, r0.x, c76.z
                    frc r1.xyzw, r0.y
                    add r0.y, r0.y, -r1.x
                    add r0.y, r0.y, c74.w
                    add r1.xy, r3.zwzz, -r0.x
                    frc r4.xyzw, r1.xyxx
                    add r1.xy, r1.xyxx, -r4.xyzw
                    add r1.xy, r1.xyxx, c77.xyxx
                    mov r1.z, c73.z
                    mov r1.w, c73.z
                    mov r4.x, c73.z
                    rep i0.xyzw
                        mov r5.z, r4.x
                        add r5.z, r5.z, c85.y
                        cmp r5.z, r5.z, c73.z, c74.w
                        add r5.z, -r5.z, c74.w
                        if_ne r5.z, -r5.z
                            break
                        else
                        endif
                        mov r5.z, r4.x
                        mul r5.w, r0.y, r0.y
                        add r5.z, r5.z, -r5.w
                        cmp r5.z, r5.z, c73.z, c74.w
                        add r5.z, -r5.z, c74.w
                        if_ne r5.z, -r5.z
                            break
                        else
                        endif
                        mov r5.z, r4.x
                        add r5.z, r5.z, c74.x
                        rcp r4.w, r0.y
                        mul r5.z, r5.z, r4.w
                        frc r7.xyzw, r5.z
                        add r5.z, r5.z, -r7.x
                        mov r5.w, r4.x
                        mul r7.x, r5.z, r0.y
                        add r5.w, r5.w, -r7.x
                        mov r4.y, r5.w
                        mov r4.z, r5.z
                        mov r5.zw, r4.xxyz
                        add r5.zw, r1.xxxy, r5.xxzw
                        add r7.xy, r5.zwzz, c74.xyxx
                        mul r7.xy, r7.xyxx, c72.y
                        add r5.zw, r5.xxzw, -r3.xxzw
                        abs r5.zw, r5.xxzw
                        add r5.zw, r0.x, -r5.xxzw
                        max r5.zw, r5.xxzw, c73.xxzw
                        mul r5.z, r5.z, r5.w
                        mov r6.xyzw, c75.xyzw
                        mov r6.xy, r7.xyxx
                        mov r8.xyzw, r6.xyzw
                        texldl r8.xyzw, r8.xyzw, s12
                        add r7.xy, r7.xyxx, -r5.xyxx
                        dp2add r5.w, r3.xyxx, r7.xyxx, c73.z
                        add r5.w, r0.w, r5.w
                        add r5.w, r8.x, -r5.w
                        cmp r5.w, r5.w, c73.z, c74.w
                        add r5.w, -r5.w, c74.w
                        cmp r5.w, -r5.w, c73.z, c74.w
                        mul r5.w, r5.w, r5.z
                        mov r7.x, r1.z
                        add r5.w, r7.x, r5.w
                        mov r1.z, r5.w
                        mov r5.w, r1.w
                        add r5.z, r5.w, r5.z
                        mov r1.w, r5.z
                        mov r5.z, r4.x
                        add r5.z, r5.z, c74.w
                        mov r4.x, r5.z
                    endrep
                    mov r0.x, r1.z
                    mov r0.y, r1.w
                    max r0.y, r0.y, c85.z
                    rcp r0.y, r0.y
                    mul r0.x, r0.x, r0.y
                    add r0.y, -r0.z, c74.w
                    mul r0.y, r0.y, c85.w
                    mov_sat r0.y, r0.y
                    add r0.x, r0.x, c73.x
                    mul r0.x, r0.y, r0.x
                    add r0.x, r0.x, c74.w
                    mov r0.xyz, r0.x
                    mov r0.w, c74.w
                    mov r2.xyzw, r0.xyzw
                else
                endif
            else
            endif
        else
        endif
    else
    endif
else
endif
mov oC0.xyzw, r2.xyzw
