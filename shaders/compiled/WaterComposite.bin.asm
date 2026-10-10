ps_3_0
dcl_texcoord0 v0
def c38 = 5.00000000e-01, 5.00000000e-01, -1.00000000e+00, -1.00000000e+00
def c39 = 0.00000000e+00, 0.00000000e+00, 9.99999975e-06, 1.20000001e-02
def c40 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c41 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c42 = 6.99999975e-04, 3.00000003e-03, -5.00000000e-01, 4.00000000e+00
def c43 = 7.20000029e-01, 3.10000002e-01, 1.10000002e+00, 7.30000019e-01
def c44 = -4.09999996e-01, 8.89999986e-01, 1.73000002e+00, -1.21000004e+00
def c45 = 1.63000000e+00, 6.28318548e+00, 1.59154937e-01, -3.14159274e+00
def c46 = 5.49999997e-02, 5.49999997e-02, 3.50000001e-02, 3.50000001e-02
def c47 = 1.20000001e-02, 1.20000001e-02, 4.51999992e-01, 2.03699991e-02
def c48 = 9.79629993e-01, 5.00000000e+00, -4.00000000e+00, -9.49999988e-01
def c49 = 5.00000000e-01, -5.00000000e-01, 9.98000026e-01, -1.19999997e-04
def c50 = 2.50000000e-01, 3.84000000e+02, 5.12000000e+02, 2.38418608e-07
def c51 = 6.00000000e+00, 6.00000000e+00, 6.00000000e+00, 1.49999997e-02
def c52 = 4.00000000e+00, 4.00000000e+00, 4.00000000e+00, -8.00000012e-01
def c53 = -7.99999982e-02, 1.38888884e+00, 3.00000000e+00, 5.09999990e+00
def c54 = 3.70000005e+00, 1.50000000e+00, -3.49999994e-01, 2.22222209e+00
def c55 = 4.03000021e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s8
dcl_2d s1
dcl_2d s2
dcl_2d s9
dcl_2d s7
dcl_2d s6
dcl_2d s3
dcl_2d s0
dcl_2d s5
dcl_2d s4
rcp r0.x, c0.x
rcp r0.y, c0.y
add r0.xy, r0.xyxx, c38.xyxx
frc r1.xyzw, r0.xyxx
add r0.xy, r0.xyxx, -r1.xyzw
mul r0.zw, v0.xxxy, r0.xxxy
frc r1.xyzw, r0.zwzz
add r0.zw, r0.xxzw, -r1.xxxy
add r0.xy, r0.xyxx, c38.zwzz
max r0.zw, r0.xxzw, c39.xxxy
min r0.xy, r0.zwzz, r0.xyxx
add r0.xy, r0.xyxx, c38.xyxx
mul r0.xy, r0.xyxx, c0.xyxx
mov r1.xyzw, c40.xyzw
mov r1.xy, r0.xyxx
texldl r1.xyzw, r1.xyzw, s0
mov r2.xyzw, c40.xyzw
mov r2.xy, r0.xyxx
texldl r2.xyzw, r2.xyzw, s2
mov r3.xyzw, c40.xyzw
mov r3.xy, r0.xyxx
texldl r3.xyzw, r3.xyzw, s1
add r0.z, r3.x, -c1.w
mul r0.z, r0.z, c2.x
mov_sat r0.z, r0.z
mul r0.w, c0.z, c0.w
add r3.x, c0.w, -c0.z
mul r0.z, r0.z, r3.x
add r0.z, c0.w, -r0.z
max r0.z, r0.z, c39.z
rcp r0.z, r0.z
mul r0.z, r0.w, r0.z
max r0.w, r2.x, c0.z
mul r3.xy, c0.xyxx, c38.xyxx
add r3.xy, r0.xyxx, -r3.xyxx
mul r3.xy, r3.xyxx, c41.xyxx
add r3.xy, r3.xyxx, c41.zwzz
rcp r3.z, c1.x
rcp r3.w, c1.y
mul r3.zw, r3.xxxy, r3.xxzw
mov r4.xy, r3.zwzz
mov r4.z, c1.z
mov r5.xyz, r4.xyzx
mul r5.xyz, r5.xyzx, r0.w
mul r6.xyz, r5.x, c3.xyzx
mul r7.xyz, r5.y, c4.xyzx
add r6.xyz, r6.xyzx, r7.xyzx
mul r5.xyz, r5.z, c5.xyzx
add r5.xyz, r6.xyzx, r5.xyzx
add r5.xyz, r5.xyzx, c6.xyzx
dsx r6.xyz, r5.xyzx
dsy r7.xyz, r5.xyzx
dp3 r0.w, r6.xyzx, r6.xyzx
dp3 r3.z, r7.xyzx, r7.xyzx
max r0.w, r0.w, r3.z
add r3.z, c0.z, -r2.x
cmp r3.z, r3.z, c39.x, c41.w
mul r3.w, r2.x, c42.x
max r3.w, r3.w, c39.w
add r3.w, r0.z, r3.w
add r3.w, r3.w, -r2.x
cmp r3.w, r3.w, c39.x, c41.w
add r3.w, -r3.w, c41.w
min r3.z, r3.z, r3.w
add r3.w, -r2.y, c42.y
cmp r3.w, r3.w, c39.x, c41.w
min r3.z, r3.z, r3.w
cmp r3.z, -r3.z, c39.x, c41.w
add r3.z, r3.z, c42.z
cmp r3.z, r3.z, c39.x, c41.w
mov r6.xyzw, r7.xyzw
cmp r6.xyzw, -r3.z, r6.xyzw, r1.xyzw
mov r7.xyzw, r6.xyzw
add r3.z, -r3.z, c41.w
if_ne r3.z, -r3.z
    mov r6.xyzw, c40.xyzw
    mov r6.xy, r0.xyxx
    texldl r6.xyzw, r6.xyzw, s3
    mul r0.x, r2.y, c42.w
    mov_sat r0.x, r0.x
    dp2add r0.y, r5.xyxx, c43.xyxx, c39.x
    mul r3.z, c2.y, c43.z
    add r0.y, r0.y, -r3.z
    dp2add r3.z, r5.xyxx, c44.xyxx, c39.x
    mul r3.w, c2.y, c43.w
    add r3.z, r3.z, r3.w
    dp2add r3.w, r5.xyxx, c44.zwzz, c39.x
    mul r4.w, c2.y, c45.x
    add r3.w, r3.w, -r4.w
    mad r0.y, r0.y, c45.z, c38.x
    frc r0.y, r0.y
    mad r0.y, r0.y, c45.y, c45.w
    sincos r8.x, r0.y
    mov r8.xy, r8.x
    mul r8.xy, r8.xyxx, c43.xyxx
    mul r8.xy, r8.xyxx, c46.xyxx
    mad r0.y, r3.z, c45.z, c38.x
    frc r0.y, r0.y
    mad r0.y, r0.y, c45.y, c45.w
    sincos r9.x, r0.y
    mov r8.zw, r9.x
    mul r8.zw, r8.xxzw, c44.xxxy
    mul r8.zw, r8.xxzw, c46.xxzw
    add r8.xy, r8.xyxx, r8.zwzz
    mad r0.y, r3.w, c45.z, c38.x
    frc r0.y, r0.y
    mad r0.y, r0.y, c45.y, c45.w
    sincos r9.x, r0.y
    mov r3.zw, r9.x
    mul r3.zw, r3.xxzw, c44.xxzw
    mul r3.zw, r3.xxzw, c47.xxxy
    add r3.zw, r8.xxxy, r3.xxzw
    mul r0.y, r0.w, c47.z
    add r0.y, -r0.y, c41.w
    mov_sat r0.y, r0.y
    mul r3.zw, r3.xxzw, r0.y
    mov r8.xy, -r3.zwzz
    mov r8.z, c41.w
    dp3 r0.y, r8.xyzx, r8.xyzx
    rsq r0.y, r0.y
    mov r9.xyz, r0.y
    mul r8.xyz, r9.xyzx, r8.xyzx
    add r9.xyz, r5.xyzx, -c6.xyzx
    dp3 r0.y, r9.xyzx, r9.xyzx
    rsq r0.y, r0.y
    mov r10.xyz, r0.y
    mul r9.xyz, r10.xyzx, r9.xyzx
    dp3 r0.y, r8.xyzx, -r9.xyzx
    cmp r0.y, r0.y, c39.x, c41.w
    cmp r8.xyz, -r0.y, r8.xyzx, -r8.xyzx
    dp3 r0.y, r9.xyzx, r8.xyzx
    add r0.y, r0.y, r0.y
    mul r10.xyz, r8.xyzx, r0.y
    add r10.xyz, r9.xyzx, -r10.xyzx
    dp3 r0.y, -r9.xyzx, r8.xyzx
    mov_sat r0.y, r0.y
    add r0.y, -r0.y, c41.w
    log r0.y, r0.y
    mul r0.y, r0.y, c48.y
    exp r0.y, r0.y
    mul r0.y, r0.y, c48.x
    add r0.y, r0.y, c47.w
    mov r3.z, c41.w
    mov r3.w, c41.w
    mov r4.w, c39.x
    rep i0.xyzw
        mov r17.y, r4.w
        add r17.y, r17.y, c48.z
        cmp r17.y, r17.y, c39.x, c41.w
        add r17.y, -r17.y, c41.w
        if_ne r17.y, -r17.y
            break
        else
        endif
        mov r17.y, r4.w
        add r17.y, r17.y, c41.y
        cmp r17.y, r17.y, c39.x, c41.w
        add r17.y, -r17.y, c41.w
        mov r17.z, r4.w
        mul r17.z, r17.z, c38.x
        frc r17.z, r17.z
        cmp r17.z, -r17.z, c39.x, c41.w
        cmp r17.w, -r17.y, c37.x, c37.y
        cmp r18.x, -r17.y, c28.x, c28.y
        cmp r17.w, -r17.z, r18.x, r17.w
        add r17.w, -r17.w, c38.x
        cmp r17.w, r17.w, c39.x, c41.w
        if_ne r17.w, -r17.w
            mov r9.x, c39.x
            if_ne r17.z, -r17.z
                cmp r18.xyz, -r17.y, c29.xyzx, c33.xyzx
                mul r18.xyz, r5.x, r18.xyzx
                cmp r19.xyz, -r17.y, c30.xyzx, c34.xyzx
                mul r19.xyz, r5.y, r19.xyzx
                add r18.xyz, r18.xyzx, r19.xyzx
                cmp r19.xyz, -r17.y, c31.xyzx, c35.xyzx
                mul r19.xyz, r5.z, r19.xyzx
                add r18.xyz, r18.xyzx, r19.xyzx
                cmp r19.xyz, -r17.y, c32.xyzx, c36.xyzx
                add r18.xyz, r18.xyzx, r19.xyzx
                mov r9.yzw, r18.xxyz
            else
                cmp r18.xyz, -r17.y, c12.xyzx, c20.xyzx
                mul r18.xyz, r5.x, r18.xyzx
                cmp r19.xyz, -r17.y, c13.xyzx, c21.xyzx
                mul r19.xyz, r5.y, r19.xyzx
                add r18.xyz, r18.xyzx, r19.xyzx
                cmp r19.xyz, -r17.y, c14.xyzx, c22.xyzx
                mul r19.xyz, r5.z, r19.xyzx
                add r18.xyz, r18.xyzx, r19.xyzx
                cmp r19.xyz, -r17.y, c15.xyzx, c23.xyzx
                add r18.xyz, r18.xyzx, r19.xyzx
                mov r9.yzw, r18.xxyz
                abs r18.xy, r18.xyxx
                max r17.w, r18.x, r18.y
                add r17.w, r17.w, c48.w
                cmp r17.w, r17.w, c39.x, c41.w
                add r17.w, -r17.w, c41.w
                if_ne r17.w, -r17.w
                    cmp r18.xyz, -r17.y, c16.xyzx, c24.xyzx
                    mul r18.xyz, r5.x, r18.xyzx
                    cmp r19.xyz, -r17.y, c17.xyzx, c25.xyzx
                    mul r19.xyz, r5.y, r19.xyzx
                    add r18.xyz, r18.xyzx, r19.xyzx
                    cmp r19.xyz, -r17.y, c18.xyzx, c26.xyzx
                    mul r19.xyz, r5.z, r19.xyzx
                    add r18.xyz, r18.xyzx, r19.xyzx
                    cmp r19.xyz, -r17.y, c19.xyzx, c27.xyzx
                    add r18.xyz, r18.xyzx, r19.xyzx
                    mov r9.yzw, r18.xxyz
                    mov r9.x, c41.w
                else
                endif
            endif
            cmp r17.w, -r17.z, c28.z, c37.z
            mov r18.xyz, r9.yzwy
            mul r18.xy, r18.xyxx, c49.xyxx
            add r18.xy, r18.xyxx, c38.xyxx
            mov r19.xyz, r9.yzwy
            add r18.z, r19.x, r17.w
            abs r18.z, r18.z
            mov r19.xyz, r9.yzwy
            add r18.w, r19.y, -r17.w
            abs r18.w, r18.w
            max r18.z, r18.z, r18.w
            mov r19.xyz, r9.yzwy
            mul r18.w, r19.z, c41.x
            add r18.w, r18.w, c38.z
            abs r18.w, r18.w
            mul r18.w, r18.w, c49.z
            max r18.z, r18.z, r18.w
            add r18.z, -r18.z, c49.z
            cmp r18.z, r18.z, c39.x, c41.w
            add r18.z, -r18.z, c41.w
            if_ne r18.z, -r18.z
                mov r19.xyz, r9.yzwy
                add r18.z, r19.z, c49.w
                mov r10.w, c39.x
                mov r17.x, c39.x
                rep i0.xyzw
                    mov r18.w, r17.x
                    add r18.w, r18.w, c48.z
                    cmp r18.w, r18.w, c39.x, c41.w
                    add r18.w, -r18.w, c41.w
                    if_ne r18.w, -r18.w
                        break
                    else
                    endif
                    mov r18.w, r17.x
                    mul r18.w, r18.w, c38.x
                    frc r19.x, r18.w
                    mul r19.y, r19.x, c41.x
                    add r18.w, r18.w, -r19.x
                    mov r15.x, r19.y
                    mov r15.y, r18.w
                    mov r19.xy, r15.xyxx
                    mul r19.xy, r19.xyxx, r17.w
                    add r19.xy, r18.xyxx, r19.xyxx
                    mov r18.w, r9.x
                    if_ne r17.z, -r17.z
                        if_ne r17.y, -r17.y
                            mov r11.xyzw, c40.xyzw
                            mov r11.xy, r19.xyxx
                            mov r20.xyzw, r11.xyzw
                            texldl r20.xyzw, r20.xyzw, s9
                            mov r5.w, r20.x
                        else
                            mov r8.xyzw, c40.xyzw
                            mov r8.xy, r19.xyxx
                            mov r20.xyzw, r8.xyzw
                            texldl r20.xyzw, r20.xyzw, s8
                            mov r5.w, r20.x
                        endif
                    else
                        if_ne r17.y, -r17.y
                            if_ne r18.w, -r18.w
                                mov r14.xyzw, c40.xyzw
                                mov r14.xy, r19.xyxx
                                mov r20.xyzw, r14.xyzw
                                texldl r20.xyzw, r20.xyzw, s7
                                mov r5.w, r20.x
                            else
                                mov r12.xyzw, c40.xyzw
                                mov r12.xy, r19.xyxx
                                mov r20.xyzw, r12.xyzw
                                texldl r20.xyzw, r20.xyzw, s6
                                mov r5.w, r20.x
                            endif
                        else
                            if_ne r18.w, -r18.w
                                mov r13.xyzw, c40.xyzw
                                mov r13.xy, r19.xyxx
                                mov r20.xyzw, r13.xyzw
                                texldl r20.xyzw, r20.xyzw, s5
                                mov r5.w, r20.x
                            else
                                mov r16.xyzw, c40.xyzw
                                mov r16.xy, r19.xyxx
                                mov r19.xyzw, r16.xyzw
                                texldl r19.xyzw, r19.xyzw, s4
                                mov r5.w, r19.x
                            endif
                        endif
                    endif
                    mov r18.w, r5.w
                    add r18.w, r18.w, -r18.z
                    cmp r18.w, r18.w, c39.x, c41.w
                    add r18.w, -r18.w, c41.w
                    mov r19.x, r10.w
                    add r18.w, r19.x, r18.w
                    mov r10.w, r18.w
                    mov r18.w, r17.x
                    add r18.w, r18.w, c41.w
                    mov r17.x, r18.w
                endrep
                cmp r17.y, -r17.y, c39.x, c41.w
                mov r17.zw, r3.xxzw
                mov r18.x, r10.w
                mul r18.x, r18.x, c50.x
                add r18.y, -r17.y, c41.w
                mov r15.z, r17.y
                mov r15.w, r18.y
                mov r18.yz, r15.xzwx
                add r18.xy, r18.x, r18.yzyy
                mov_sat r18.xy, r18.xyxx
                min r17.yz, r17.xzwx, r18.xxyx
                mov r3.zw, r17.xxyz
            else
            endif
        else
        endif
        mov r17.y, r4.w
        add r17.y, r17.y, c41.w
        mov r4.w, r17.y
    endrep
    dp3 r4.w, r10.xyzx, c7.xyzx
    mov_sat r4.w, r4.w
    log r4.w, r4.w
    mul r4.w, r4.w, c50.y
    exp r4.w, r4.w
    mov r8.xyz, r4.w
    mul r8.xyz, c8.xyzx, r8.xyzx
    mul r8.xyz, r8.xyzx, c51.xyzx
    mul r8.xyz, r8.xyzx, r3.z
    dp3 r4.w, r10.xyzx, c9.xyzx
    mov_sat r4.w, r4.w
    log r4.w, r4.w
    mul r4.w, r4.w, c50.z
    exp r4.w, r4.w
    mov r9.xyz, r4.w
    mul r9.xyz, c10.xyzx, r9.xyzx
    mul r9.xyz, r9.xyzx, c52.xyzx
    mul r9.xyz, r9.xyzx, r3.w
    add r8.xyz, r8.xyzx, r9.xyzx
    add r3.z, -r6.w, c41.w
    mul r8.xyz, r8.xyzx, r3.z
    add r6.xyz, r6.xyzx, r8.xyzx
    mul r0.y, r0.y, c2.z
    mov_sat r0.y, r0.y
    mul r0.y, r0.y, r0.x
    add r6.xyz, r6.xyzx, -r1.xyzx
    mul r6.xyz, r0.y, r6.xyzx
    add r6.xyz, r1.xyzx, r6.xyzx
    rcp r3.z, c1.x
    rcp r3.w, c1.y
    mul r3.xy, r3.xyxx, r3.zwzz
    mov r4.xy, r3.xyxx
    mov r4.z, c1.z
    mov r3.xyz, r4.xyzx
    mul r3.xyz, r3.xyzx, r0.z
    mul r4.xyz, r3.x, c3.xyzx
    mul r8.xyz, r3.y, c4.xyzx
    add r4.xyz, r4.xyzx, r8.xyzx
    mul r3.xyz, r3.z, c5.xyzx
    add r3.xyz, r4.xyzx, r3.xyzx
    add r3.xyz, r3.xyzx, c6.xyzx
    add r0.y, r5.z, -r3.z
    mul r3.x, r2.x, r2.x
    mul r3.x, r3.x, c50.w
    rcp r3.y, c0.z
    mul r3.x, r3.x, r3.y
    add r0.z, r0.z, -r2.x
    add r0.z, r3.x, -r0.z
    cmp r0.z, r0.z, c39.x, c41.w
    add r2.x, -r0.y, c51.w
    cmp r2.x, r2.x, c39.x, c41.w
    min r0.z, r0.z, r2.x
    add r2.x, r0.y, c52.w
    cmp r2.x, r2.x, c39.x, c41.w
    min r0.z, r0.z, r2.x
    add r0.y, r0.y, c53.x
    mul r0.y, r0.y, c53.y
    mov_sat r0.y, r0.y
    mul r2.x, r0.y, r0.y
    mul r0.y, r0.y, c41.x
    add r0.y, -r0.y, c53.z
    mul r0.y, r2.x, r0.y
    add r0.y, -r0.y, c41.w
    cmp r0.y, -r0.z, c39.x, r0.y
    mul r0.z, r5.x, c53.w
    mul r2.x, r5.y, c54.x
    add r0.z, r0.z, r2.x
    mul r2.x, c2.y, c54.y
    add r0.z, r0.z, r2.x
    mad r0.z, r0.z, c45.z, c38.x
    frc r0.z, r0.z
    mad r0.z, r0.z, c45.y, c45.w
    sincos r2.y, r0.z
    mul r0.z, r2.y, c38.x
    add r0.z, r0.z, c38.x
    add r0.z, r0.z, c54.z
    mul r0.z, r0.z, c54.w
    mov_sat r0.z, r0.z
    mul r2.x, r0.z, r0.z
    mul r0.z, r0.z, c41.x
    add r0.z, -r0.z, c53.z
    mul r0.z, r2.x, r0.z
    mul r0.y, r0.y, r0.z
    mul r0.y, r0.y, c2.w
    mul r0.x, r0.y, r0.x
    mul r0.y, r0.w, c55.x
    add r0.y, -r0.y, c41.w
    mov_sat r0.y, r0.y
    mul r0.x, r0.x, r0.y
    mov_sat r0.x, r0.x
    add r0.yzw, c11.xxyz, -r6.xxyz
    mul r0.xyz, r0.x, r0.yzwy
    add r0.xyz, r6.xyzx, r0.xyzx
    mov r0.w, r1.w
    mov r7.xyzw, r0.xyzw
else
endif
mov oC0.xyzw, r7.xyzw
