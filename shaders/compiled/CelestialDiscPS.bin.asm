ps_3_0
dcl_texcoord0 v0
def c49 = 0.00000000e+00, 5.00000000e-01, 5.00000000e-01, 1.00000000e+00
def c50 = -1.00000000e+00, -1.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c51 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c52 = -9.99999940e-01, 3.00000003e-03, 2.00000000e+00, -2.00000000e+00
def c53 = -1.00000000e+00, 1.00000000e+00, -5.00000000e-01, -9.99989986e-01
def c54 = -1.00000005e-03, -9.99999997e-07, 1.78571427e+00, 3.00000000e+00
def c55 = 3.49999994e-01, -1.19999997e-01, -2.50000004e-02, -8.00000038e-03
def c56 = 5.00000000e-01, 5.00000000e-01, 0.00000000e+00, 0.00000000e+00
def c57 = 2.50000000e-01, 3.49999994e-01, 4.00000006e-01, -1.11000001e+00
def c58 = 1.18257001e-01, 2.99457997e-01, 3.80526990e-01, -1.44269502e+00
def c59 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 0.00000000e+00
def c60 = 4.04499993e-02, 4.04499993e-02, 4.04499993e-02, 0.00000000e+00
def c61 = 7.73993805e-02, 7.73993805e-02, 7.73993805e-02, 0.00000000e+00
def c62 = 5.49999997e-02, 5.49999997e-02, 5.49999997e-02, 0.00000000e+00
def c63 = 9.47867334e-01, 9.47867334e-01, 9.47867334e-01, 0.00000000e+00
def c64 = 2.40000010e+00, 2.40000010e+00, 2.40000010e+00, 0.00000000e+00
dcl_2d s0
dcl_2d s3
dcl_2d s5
dcl_2d s4
dcl_2d s1
dcl_2d s2
mov r0.x, c49.x
rcp r0.y, c0.x
rcp r0.z, c0.y
add r0.yz, r0.xyzx, c49.xyzx
frc r1.xyzw, r0.yzyy
add r0.yz, r0.xyzx, -r1.xxyx
mul r1.xy, v0.xyxx, r0.yzyy
frc r2.xyzw, r1.xyxx
add r1.xy, r1.xyxx, -r2.xyzw
add r0.yz, r0.xyzx, c50.xxyx
max r1.xy, r1.xyxx, c50.zwzz
min r0.yz, r1.xxyx, r0.xyzx
add r0.yz, r0.xyzx, c49.xyzx
mul r0.yz, r0.xyzx, c0.xxyx
mov r1.xyzw, c51.xyzw
mov r1.xy, r0.yzyy
texldl r1.xyzw, r1.xyzw, s0
add r0.w, r1.x, -c0.z
mul r0.w, r0.w, c0.w
mov_sat r0.w, r0.w
add r2.x, -c11.y, c49.w
add r1.x, r1.x, -c11.x
add r1.x, r1.x, c11.z
mul r1.x, c11.y, r1.x
add r1.x, r2.x, r1.x
texkill r1.x
add r1.x, -c10.w, c49.y
cmp r1.x, r1.x, c49.x, c49.w
add r0.w, r0.w, c52.x
cmp r0.w, r0.w, c49.x, c49.w
min r0.w, r1.x, r0.w
mov r2.xyzw, r3.xyzw
cmp r2.xyzw, -r0.w, r2.xyzw, c51.xyzw
mov r3.xyzw, r2.xyzw
mov r0.x, r0.w
add r1.y, -r0.w, c49.w
if_ne r1.y, -r1.y
    add r1.y, -c1.w, c49.y
    cmp r1.y, r1.y, c49.x, c49.w
    min r1.x, r1.x, r1.y
    if_ne r1.x, -r1.x
        mov r1.xyzw, c51.xyzw
        mov r1.xy, r0.yzyy
        texldl r1.xyzw, r1.xyzw, s2
        add r0.y, c10.y, -r1.x
        cmp r0.y, r0.y, c49.x, c49.w
        add r0.z, c10.z, -r1.x
        cmp r0.z, r0.z, c49.x, c49.w
        add r0.z, -r0.z, c49.w
        min r0.y, r0.y, r0.z
        add r0.z, -r1.y, c52.y
        cmp r0.z, r0.z, c49.x, c49.w
        min r0.y, r0.y, r0.z
        cmp r1.xyzw, -r0.y, r2.xyzw, c51.xyzw
        mov r3.xyzw, r1.xyzw
        cmp r0.y, -r0.y, r0.w, c49.w
        mov r0.x, r0.y
    else
    endif
    add r0.x, -r0.x, c49.w
    if_ne r0.x, -r0.x
        mul r0.xy, c0.xyxx, c49.yzyy
        add r0.xy, v0.xyxx, -r0.xyxx
        mul r0.xy, r0.xyxx, c52.zwzz
        add r0.xy, r0.xyxx, c53.xyxx
        rcp r0.z, c1.x
        rcp r0.w, c1.y
        mul r0.xy, r0.xyxx, r0.zwzz
        mul r1.xyz, r0.x, c2.xyzx
        mul r0.xyz, r0.y, c3.xyzx
        add r0.xyz, r1.xyzx, r0.xyzx
        mul r1.xyz, c1.z, c4.xyzx
        add r0.xyz, r0.xyzx, r1.xyzx
        add r0.w, c46.x, c53.z
        cmp r0.w, r0.w, c49.x, c49.w
        mov r1.x, r1.y
        cmp r1.x, -r0.w, r1.x, c49.w
        mov r1.y, r1.x
        add r1.z, -r0.w, c49.w
        if_ne r1.z, -r1.z
            dp3 r1.z, r0.xyzx, c6.xyzx
            cmp r1.w, -r1.z, c49.x, c49.w
            add r1.w, -r1.w, c49.w
            cmp r1.x, -r1.w, r1.x, c49.w
            mov r1.y, r1.x
            cmp r0.w, -r1.w, r0.w, c49.w
            add r1.w, -r0.w, c49.w
            if_ne r1.w, -r1.w
                dp3 r1.w, r0.xyzx, c7.xyzx
                dp3 r2.x, r0.xyzx, c8.xyzx
                mov r2.y, r1.w
                mov r2.z, -r2.x
                mov r2.xy, r2.yzyy
                mul r1.z, r1.z, c6.w
                mul r1.z, r1.z, c46.y
                rcp r2.z, r1.z
                rcp r2.w, r1.z
                mul r1.zw, r2.xxxy, r2.xxzw
                abs r2.xy, r1.zwzz
                max r2.x, r2.x, r2.y
                add r2.x, -r2.x, c49.w
                cmp r2.x, r2.x, c49.x, c49.w
                cmp r1.x, -r2.x, r1.x, c49.w
                mov r1.y, r1.x
                cmp r0.w, -r2.x, r0.w, c49.w
                add r0.w, -r0.w, c49.w
                if_ne r0.w, -r0.w
                    mul r1.xz, r1.zxwx, c49.yxzx
                    add r1.xz, r1.xxzx, c49.yxzx
                    mov r2.xyzw, c51.xyzw
                    mov r2.xy, r1.xzxx
                    texldl r2.xyzw, r2.xyzw, s4
                    mov_sat r0.w, r2.x
                    add r0.w, -r0.w, c49.w
                    mov r1.y, r0.w
                else
                endif
            else
            endif
        else
        endif
        mov r0.w, r1.y
        add r1.x, c10.w, c53.z
        cmp r1.x, r1.x, c49.x, c49.w
        add r1.y, r0.w, c53.w
        cmp r1.y, -r1.x, c49.x, r1.y
        texkill r1.y
        add r1.y, r0.w, c54.x
        texkill r1.y
        dp3 r1.y, r0.xyzx, c6.xyzx
        add r1.z, r1.y, c54.y
        texkill r1.z
        dp3 r1.z, r0.xyzx, c7.xyzx
        dp3 r0.x, r0.xyzx, c8.xyzx
        mov r0.y, r1.z
        mov r0.z, -r0.x
        mov r0.xy, r0.yzyy
        mul r0.z, r1.y, c6.w
        rcp r1.y, r0.z
        rcp r1.z, r0.z
        mul r0.xy, r0.xyxx, r1.yzyy
        mul r1.yz, r0.xxyx, c49.xyzx
        add r1.yz, r1.xyzx, c49.xyzx
        mov r2.xyzw, c51.xyzw
        mov r2.xy, r1.yzyy
        texldl r2.xyzw, r2.xyzw, s1
        mov r4.xyzw, r2.xyzw
        add r0.z, -c11.w, c49.y
        cmp r0.z, r0.z, c49.x, c49.w
        if_ne r0.z, -r0.z
            dp2add r1.y, r0.xyxx, r0.xyxx, c49.x
            rsq r1.y, r1.y
            rcp r1.y, r1.y
            mov r1.y, -r1.y
            add r1.y, r1.y, c49.w
            mul r1.y, r1.y, c54.z
            mov_sat r1.y, r1.y
            mul r1.z, r1.y, r1.y
            mul r1.y, r1.y, c52.z
            add r1.y, -r1.y, c54.w
            mul r1.y, r1.z, r1.y
            mov r4.w, r1.y
        else
        endif
        mov r5.xyzw, r4.xyzw
        add r1.y, r5.w, c53.w
        cmp r1.x, -r1.x, c49.x, r1.y
        texkill r1.x
        add r1.x, -c12.w, c49.y
        cmp r1.x, r1.x, c49.x, c49.w
        if_ne r1.x, -r1.x
            dp2add r1.y, r0.xyxx, r0.xyxx, c49.x
            rsq r1.y, r1.y
            rcp r1.y, r1.y
            mov r1.z, -r1.y
            add r1.z, c12.y, r1.z
            texkill r1.z
            mul r1.z, -c12.x, r1.y
            mul r1.z, r1.z, r1.y
            exp r1.z, r1.z
            mov_sat r1.z, r1.z
            mul r1.w, r1.z, r1.z
            mul r1.z, r1.z, c52.z
            add r1.z, -r1.z, c54.w
            mul r1.z, r1.w, r1.z
            add r5.xyz, c48.xyzx, -c47.xyzx
            mul r5.xyz, r1.z, r5.xyzx
            add r5.xyz, c47.xyzx, r5.xyzx
            mov_sat r5.xyz, r5.xyzx
            mov_sat r1.z, c9.w
            mul r1.w, r1.y, r1.y
            mul r5.w, c12.y, c55.x
            add r6.x, r1.y, -r5.w
            add r5.w, c12.y, -r5.w
            rcp r5.w, r5.w
            mul r5.w, r6.x, r5.w
            mov_sat r5.w, r5.w
            mul r6.x, r5.w, r5.w
            mul r5.w, r5.w, c52.z
            add r5.w, -r5.w, c54.w
            mul r5.w, r6.x, r5.w
            add r5.w, -r5.w, c49.w
            texldl r6.xyzw, c56.xyzw, s3
            mov_sat r6.x, r6.x
            texldl r7.xyzw, c56.xyzw, s5
            mov_sat r6.y, r7.x
            mul r6.y, c48.w, r6.y
            mul r6.z, r1.w, c55.y
            exp r6.z, r6.z
            mul r6.w, r1.w, c55.z
            exp r6.w, r6.w
            mul r7.x, r1.w, c55.w
            exp r7.x, r7.x
            mov r7.y, r6.z
            mov r7.z, r6.w
            mov r7.w, r7.x
            mov r8.xyz, r7.yzwy
            dp3 r6.z, r8.xyzx, c57.xyzx
            mul r6.z, r6.x, r6.z
            mov r7.xyz, r7.yzwy
            dp3 r6.w, r7.xyzx, c58.xyzx
            mul r6.y, r6.y, r6.w
            max r6.y, r6.z, r6.y
            mul r1.w, -c12.x, r1.w
            exp r1.w, r1.w
            mul r1.w, c12.z, r1.w
            mul r1.w, r1.w, r6.x
            mul r6.y, c47.w, r6.y
            add r1.w, r1.w, r6.y
            mul r1.w, r1.w, r5.w
            mul r6.y, c12.z, c55.x
            add r1.y, r1.y, c50.x
            max r1.y, r1.y, c49.x
            mul r1.y, r1.y, c57.w
            exp r1.y, r1.y
            mul r1.y, r6.y, r1.y
            mul r1.y, r1.y, r5.w
            mul r1.y, r1.y, r6.x
            cmp r0.z, -r0.z, r1.y, r1.w
            mul r0.z, r1.z, r0.z
            mov r1.y, r0.z
            add r1.z, c11.w, c53.z
            cmp r1.z, r1.z, c49.x, c49.w
            mov r6.xyzw, r4.xyzw
            add r1.w, -r6.w, c49.w
            mul r1.w, r0.z, r1.w
            cmp r0.z, -r1.z, r0.z, r1.w
            mov r1.y, r0.z
        else
            abs r0.xy, r0.xyxx
            max r0.x, r0.x, r0.y
            add r0.x, -r0.x, c49.w
            texkill r0.x
            mul r0.xyz, r2.xyzx, c12.x
            mov_sat r0.xyz, r0.xyzx
            mov_sat r2.xyz, c9.xyzx
            mul r1.z, c47.w, c49.y
            add r6.xyz, c48.xyzx, -r2.xyzx
            mul r6.xyz, r1.z, r6.xyzx
            add r2.xyz, r2.xyzx, r6.xyzx
            mul r0.xyz, r0.xyzx, r2.xyzx
            mov r5.xyz, r0.xyzx
            mov r2.xyzw, r4.xyzw
            mov_sat r0.x, c9.w
            mul r0.x, r2.w, r0.x
            mov r1.y, r0.x
        endif
        mov r0.x, r1.y
        mul r0.x, r0.x, r0.w
        add r0.y, r0.x, c54.x
        texkill r0.y
        if_ne r1.x, -r1.x
            mov r0.yzw, r5.xxyz
            mul r1.x, r0.x, c58.w
            mul r0.yzw, r1.x, r0.xyzw
            exp r1.x, r0.y
            exp r1.y, r0.z
            exp r1.z, r0.w
            mov r0.yzw, -r1.xxyz
            add r0.yzw, r0.xyzw, c59.xxyz
            mov r5.xyz, r0.yzwy
        else
        endif
        mov r0.yzw, r5.xxyz
        add r1.xyz, -r0.yzwy, c60.xyzx
        cmp r1.xyz, r1.xyzx, c51.xyzx, c59.xyzx
        add r1.xyz, -r1.xyzx, c59.xyzx
        mul r2.xyz, r0.yzwy, c61.xyzx
        add r4.xyz, r0.yzwy, c62.xyzx
        mul r4.xyz, r4.xyzx, c63.xyzx
        log r5.x, r4.x
        log r5.y, r4.y
        log r5.z, r4.z
        mul r4.xyz, r5.xyzx, c64.xyzx
        exp r5.x, r4.x
        exp r5.y, r4.y
        exp r5.z, r4.z
        cmp r1.xyz, -r1.xyzx, r5.xyzx, r2.xyzx
        add r1.w, -c10.x, c49.y
        cmp r1.w, r1.w, c49.x, c49.w
        cmp r0.yzw, -r1.w, r0.xyzw, r1.xxyz
        mov r1.xyz, r0.yzwy
        mov r1.w, r0.x
        mov r0.xyzw, r1.xyzw
        mov r3.xyzw, r0.xyzw
    else
    endif
else
endif
mov oC0.xyzw, r3.xyzw
