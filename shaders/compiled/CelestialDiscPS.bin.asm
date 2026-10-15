ps_3_0
dcl_texcoord0 v0
def c49 = 0.00000000e+00, 5.00000000e-01, 5.00000000e-01, -5.00000000e-01
def c50 = -1.00000000e+00, -1.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c51 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c52 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c53 = 9.99999975e-06, -9.99999940e-01, 3.00000003e-03, -9.99989986e-01
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
mul r2.xy, c0.xyxx, c49.yzyy
add r2.xy, v0.xyxx, -r2.xyxx
mul r2.xy, r2.xyxx, c52.xyxx
add r2.xy, r2.xyxx, c52.zwzz
rcp r2.z, c1.x
rcp r2.w, c1.y
mul r2.xy, r2.xyxx, r2.zwzz
mul r3.xyz, r2.x, c2.xyzx
mul r2.xyz, r2.y, c3.xyzx
add r2.xyz, r3.xyzx, r2.xyzx
mul r3.xyz, c1.z, c4.xyzx
add r2.xyz, r2.xyzx, r3.xyzx
add r2.w, -c10.w, c49.y
cmp r2.w, r2.w, c49.x, c52.w
add r3.x, c46.z, c49.w
cmp r3.x, r3.x, c49.x, c52.w
mov r3.y, r3.z
cmp r3.y, -r3.x, r3.y, c49.x
mov r3.z, r3.y
add r3.x, -r3.x, c52.w
if_ne r3.x, -r3.x
    dp3 r3.x, r2.xyzx, r2.xyzx
    rsq r3.x, r3.x
    mul r3.x, r2.z, r3.x
    cmp r3.y, -r3.x, c49.x, c52.w
    add r3.y, -r3.y, c52.w
    mul r3.w, c6.w, c52.y
    add r3.x, r3.w, -r3.x
    cmp r3.x, r3.x, c49.x, c52.w
    min r3.x, r3.y, r3.x
    mov r3.y, c49.x
    mul r3.w, c10.y, c10.z
    add r4.x, c10.z, -c10.y
    mul r4.x, r0.w, r4.x
    add r4.x, c10.z, -r4.x
    max r4.x, r4.x, c53.x
    rcp r4.x, r4.x
    mul r3.w, r3.w, r4.x
    add r4.x, r0.w, c53.y
    cmp r4.x, r4.x, c49.x, c52.w
    add r3.w, r3.w, -c46.w
    cmp r3.w, r3.w, c49.x, c52.w
    min r3.w, r4.x, r3.w
    mov r4.x, r4.y
    cmp r4.x, -r3.w, r4.x, c49.x
    mov r4.y, r4.x
    mov r3.y, r3.w
    add r4.z, -r3.w, c52.w
    if_ne r4.z, -r4.z
        add r4.z, -c1.w, c49.y
        cmp r4.z, r4.z, c49.x, c52.w
        if_ne r4.z, -r4.z
            mov r5.xyzw, c51.xyzw
            mov r5.xy, r0.yzyy
            texldl r5.xyzw, r5.xyzw, s2
            add r4.z, c10.y, -r5.x
            cmp r4.z, r4.z, c49.x, c52.w
            add r4.w, r5.x, -c46.w
            cmp r4.w, r4.w, c49.x, c52.w
            min r4.z, r4.z, r4.w
            add r4.w, -r5.y, c53.z
            cmp r4.w, r4.w, c49.x, c52.w
            min r4.z, r4.z, r4.w
            cmp r4.x, -r4.z, r4.x, c49.x
            mov r4.y, r4.x
            cmp r3.w, -r4.z, r3.w, c52.w
            mov r3.y, r3.w
        else
        endif
        mov r3.w, r4.y
        cmp r3.y, -r3.y, c52.w, r3.w
        mov r4.y, r3.y
    else
    endif
    mov r3.y, r4.y
    min r3.x, r3.x, r3.y
    mov r3.z, r3.x
else
endif
mov r3.x, r3.z
min r3.x, r2.w, r3.x
add r3.y, -r3.x, c52.w
if_ne r3.y, -r3.y
    add r3.y, -c11.y, c52.w
    add r1.x, r1.x, -c11.x
    add r1.x, r1.x, c11.z
    mul r1.x, c11.y, r1.x
    add r1.x, r3.y, r1.x
    texkill r1.x
    add r0.w, r0.w, c53.y
    cmp r0.w, r0.w, c49.x, c52.w
    min r0.w, r2.w, r0.w
    mov r1.xyzw, r4.xyzw
    cmp r1.xyzw, -r0.w, r1.xyzw, c51.xyzw
    mov r4.xyzw, r1.xyzw
    mov r0.x, r0.w
    add r3.y, -r0.w, c52.w
    if_ne r3.y, -r3.y
        add r3.y, -c1.w, c49.y
        cmp r3.y, r3.y, c49.x, c52.w
        min r2.w, r2.w, r3.y
        if_ne r2.w, -r2.w
            mov r5.xyzw, c51.xyzw
            mov r5.xy, r0.yzyy
            texldl r5.xyzw, r5.xyzw, s2
            add r0.y, c10.y, -r5.x
            cmp r0.y, r0.y, c49.x, c52.w
            add r0.z, c10.z, -r5.x
            cmp r0.z, r0.z, c49.x, c52.w
            add r0.z, -r0.z, c52.w
            min r0.y, r0.y, r0.z
            add r0.z, -r5.y, c53.z
            cmp r0.z, r0.z, c49.x, c52.w
            min r0.y, r0.y, r0.z
            cmp r1.xyzw, -r0.y, r1.xyzw, c51.xyzw
            mov r4.xyzw, r1.xyzw
            cmp r0.y, -r0.y, r0.w, c52.w
            mov r0.x, r0.y
        else
        endif
    else
    endif
else
endif
add r0.x, -r0.x, c52.w
if_ne r0.x, -r0.x
    add r0.x, c46.x, c49.w
    cmp r0.x, r0.x, c49.x, c52.w
    mov r0.y, r0.z
    cmp r0.y, -r0.x, r0.y, c52.w
    mov r0.z, r0.y
    add r0.w, -r0.x, c52.w
    if_ne r0.w, -r0.w
        dp3 r0.w, r2.xyzx, c6.xyzx
        cmp r1.x, -r0.w, c49.x, c52.w
        add r1.x, -r1.x, c52.w
        cmp r0.y, -r1.x, r0.y, c52.w
        mov r0.z, r0.y
        cmp r0.x, -r1.x, r0.x, c52.w
        add r1.x, -r0.x, c52.w
        if_ne r1.x, -r1.x
            dp3 r1.x, r2.xyzx, c7.xyzx
            dp3 r1.y, r2.xyzx, c8.xyzx
            mov r1.z, -r1.y
            mov r1.xy, r1.xzxx
            mul r0.w, r0.w, c6.w
            mul r0.w, r0.w, c46.y
            rcp r1.z, r0.w
            rcp r1.w, r0.w
            mul r1.xy, r1.xyxx, r1.zwzz
            abs r1.zw, r1.xxxy
            max r0.w, r1.z, r1.w
            add r0.w, -r0.w, c52.w
            cmp r0.w, r0.w, c49.x, c52.w
            cmp r0.y, -r0.w, r0.y, c52.w
            mov r0.z, r0.y
            cmp r0.x, -r0.w, r0.x, c52.w
            add r0.x, -r0.x, c52.w
            if_ne r0.x, -r0.x
                mul r0.xy, r1.xyxx, c49.yzyy
                add r0.xy, r0.xyxx, c49.yzyy
                mov r1.xyzw, c51.xyzw
                mov r1.xy, r0.xyxx
                texldl r1.xyzw, r1.xyzw, s4
                mov_sat r0.x, r1.x
                add r0.x, -r0.x, c52.w
                mov r0.z, r0.x
            else
            endif
        else
        endif
    else
    endif
    mov r0.x, r0.z
    cmp r0.x, -r3.x, r0.x, c52.w
    add r0.y, c10.w, c49.w
    cmp r0.y, r0.y, c49.x, c52.w
    add r0.z, r0.x, c53.w
    cmp r0.z, -r0.y, c49.x, r0.z
    texkill r0.z
    add r0.z, r0.x, c54.x
    texkill r0.z
    dp3 r0.z, r2.xyzx, c6.xyzx
    add r0.w, r0.z, c54.y
    texkill r0.w
    dp3 r0.w, r2.xyzx, c7.xyzx
    dp3 r1.x, r2.xyzx, c8.xyzx
    mov r1.y, r0.w
    mov r1.z, -r1.x
    mov r1.xy, r1.yzyy
    mul r0.z, r0.z, c6.w
    rcp r1.z, r0.z
    rcp r1.w, r0.z
    mul r0.zw, r1.xxxy, r1.xxzw
    mul r1.xy, r0.zwzz, c49.yzyy
    add r1.xy, r1.xyxx, c49.yzyy
    mov r2.xyzw, c51.xyzw
    mov r2.xy, r1.xyxx
    mov r1.xyzw, r2.xyzw
    texldl r1.xyzw, r1.xyzw, s1
    mov r2.xyzw, r1.xyzw
    add r3.x, -c11.w, c49.y
    cmp r3.x, r3.x, c49.x, c52.w
    if_ne r3.x, -r3.x
        dp2add r3.y, r0.zwzz, r0.zwzz, c49.x
        rsq r3.y, r3.y
        rcp r3.y, r3.y
        mov r3.y, -r3.y
        add r3.y, r3.y, c52.w
        mul r3.y, r3.y, c54.z
        mov_sat r3.y, r3.y
        mul r3.z, r3.y, r3.y
        mul r3.y, r3.y, c52.x
        add r3.y, -r3.y, c54.w
        mul r3.y, r3.z, r3.y
        mov r2.w, r3.y
    else
    endif
    mov r5.xyzw, r2.xyzw
    add r3.y, r5.w, c53.w
    cmp r0.y, -r0.y, c49.x, r3.y
    texkill r0.y
    add r0.y, -c12.w, c49.y
    cmp r0.y, r0.y, c49.x, c52.w
    if_ne r0.y, -r0.y
        dp2add r3.y, r0.zwzz, r0.zwzz, c49.x
        rsq r3.y, r3.y
        rcp r3.y, r3.y
        mov r3.z, -r3.y
        add r3.z, c12.y, r3.z
        texkill r3.z
        mul r3.z, -c12.x, r3.y
        mul r3.z, r3.z, r3.y
        exp r3.z, r3.z
        mov_sat r3.z, r3.z
        mul r3.w, r3.z, r3.z
        mul r3.z, r3.z, c52.x
        add r3.z, -r3.z, c54.w
        mul r3.z, r3.w, r3.z
        add r5.xyz, c48.xyzx, -c47.xyzx
        mul r5.xyz, r3.z, r5.xyzx
        add r5.xyz, c47.xyzx, r5.xyzx
        mov_sat r5.xyz, r5.xyzx
        mov_sat r3.z, c9.w
        mul r3.w, r3.y, r3.y
        mul r5.w, c12.y, c55.x
        add r6.x, r3.y, -r5.w
        add r5.w, c12.y, -r5.w
        rcp r5.w, r5.w
        mul r5.w, r6.x, r5.w
        mov_sat r5.w, r5.w
        mul r6.x, r5.w, r5.w
        mul r5.w, r5.w, c52.x
        add r5.w, -r5.w, c54.w
        mul r5.w, r6.x, r5.w
        add r5.w, -r5.w, c52.w
        texldl r6.xyzw, c56.xyzw, s3
        mov_sat r6.x, r6.x
        texldl r7.xyzw, c56.xyzw, s5
        mov_sat r6.y, r7.x
        mul r6.y, c48.w, r6.y
        mul r6.z, r3.w, c55.y
        exp r6.z, r6.z
        mul r6.w, r3.w, c55.z
        exp r6.w, r6.w
        mul r7.x, r3.w, c55.w
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
        mul r3.w, -c12.x, r3.w
        exp r3.w, r3.w
        mul r3.w, c12.z, r3.w
        mul r3.w, r3.w, r6.x
        mul r6.y, c47.w, r6.y
        add r3.w, r3.w, r6.y
        mul r3.w, r3.w, r5.w
        mul r6.y, c12.z, c55.x
        add r3.y, r3.y, c50.x
        max r3.y, r3.y, c49.x
        mul r3.y, r3.y, c57.w
        exp r3.y, r3.y
        mul r3.y, r6.y, r3.y
        mul r3.y, r3.y, r5.w
        mul r3.y, r3.y, r6.x
        cmp r3.x, -r3.x, r3.y, r3.w
        mul r3.x, r3.z, r3.x
        mov r3.y, r3.x
        add r3.z, c11.w, c49.w
        cmp r3.z, r3.z, c49.x, c52.w
        mov r6.xyzw, r2.xyzw
        add r3.w, -r6.w, c52.w
        mul r3.w, r3.x, r3.w
        cmp r3.x, -r3.z, r3.x, r3.w
        mov r3.y, r3.x
    else
        abs r0.zw, r0.xxzw
        max r0.z, r0.z, r0.w
        add r0.z, -r0.z, c52.w
        texkill r0.z
        mul r1.xyz, r1.xyzx, c12.x
        mov_sat r1.xyz, r1.xyzx
        mov_sat r3.xzw, c9.xxyz
        mul r0.z, c47.w, c49.y
        add r6.xyz, c48.xyzx, -r3.xzwx
        mul r6.xyz, r0.z, r6.xyzx
        add r3.xzw, r3.xxzw, r6.xxyz
        mul r1.xyz, r1.xyzx, r3.xzwx
        mov r5.xyz, r1.xyzx
        mov r1.xyzw, r2.xyzw
        mov_sat r0.z, c9.w
        mul r0.z, r1.w, r0.z
        mov r3.y, r0.z
    endif
    mov r0.z, r3.y
    mul r0.x, r0.z, r0.x
    add r0.z, r0.x, c54.x
    texkill r0.z
    if_ne r0.y, -r0.y
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
    add r3.xyz, r0.yzwy, c62.xyzx
    mul r3.xyz, r3.xyzx, c63.xyzx
    log r5.x, r3.x
    log r5.y, r3.y
    log r5.z, r3.z
    mul r3.xyz, r5.xyzx, c64.xyzx
    exp r5.x, r3.x
    exp r5.y, r3.y
    exp r5.z, r3.z
    cmp r1.xyz, -r1.xyzx, r5.xyzx, r2.xyzx
    add r1.w, -c10.x, c49.y
    cmp r1.w, r1.w, c49.x, c52.w
    cmp r0.yzw, -r1.w, r0.xyzw, r1.xxyz
    mov r1.xyz, r0.yzwy
    mov r1.w, r0.x
    mov r0.xyzw, r1.xyzw
    mov r4.xyzw, r0.xyzw
else
endif
mov oC0.xyzw, r4.xyzw
