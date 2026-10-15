ps_3_0
dcl_texcoord0 v0
def c10 = 0.00000000e+00, 5.00000000e-01, 5.00000000e-01, -9.99998987e-01
def c11 = -1.00000000e+00, -1.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c12 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c13 = 1.00000000e+00, 9.99999975e-06, 1.20000001e-02, 6.99999975e-04
def c14 = 3.00000003e-03, 2.00000000e+00, -2.00000000e+00, 9.99999975e-05
def c15 = -1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c16 = 9.99999996e-13, -8.99999976e-01, 1.00000001e-01, 8.00000012e-01
def c17 = 0.00000000e+00, 0.00000000e+00, 1.00000000e+00, 1.20000005e+00
def c18 = 0.00000000e+00, 1.00000000e+00, 0.00000000e+00, 1.99999999e-06
def c19 = -5.00000000e-01, -5.00000000e-01, -5.00000000e-01, 2.50000000e-01
def c20 = 5.00000000e-01, 5.00000000e-01, 5.00000000e-01, 0.00000000e+00
def c21 = 2.00000000e+00, 2.00000000e+00, 2.00000000e+00, 0.00000000e+00
def c22 = 3.49999994e-01, 3.49999994e-01, 3.49999994e-01, 0.00000000e+00
dcl_cube s1
dcl_2d s0
dcl_2d s2
mov r0.x, c10.x
rcp r0.y, c0.x
rcp r0.z, c0.y
add r0.yz, r0.xyzx, c10.xyzx
frc r1.xyzw, r0.yzyy
add r0.yz, r0.xyzx, -r1.xxyx
mul r1.xy, v0.xyxx, r0.yzyy
frc r2.xyzw, r1.xyxx
add r1.xy, r1.xyxx, -r2.xyzw
add r0.yz, r0.xyzx, c11.xxyx
max r1.xy, r1.xyxx, c11.zwzz
min r0.yz, r1.xxyx, r0.xyzx
add r0.yz, r0.xyzx, c10.xyzx
mul r0.yz, r0.xyzx, c0.xxyx
mov r1.xyzw, c12.xyzw
mov r1.xy, r0.yzyy
texldl r1.xyzw, r1.xyzw, s0
add r0.w, r1.x, -c1.w
mul r0.w, r0.w, c2.x
mov_sat r0.w, r0.w
add r1.x, r0.w, c10.w
cmp r1.x, r1.x, c10.x, c13.x
add r1.x, -r1.x, c13.x
mov r2.xyzw, r3.xyzw
cmp r2.xyzw, -r1.x, r2.xyzw, c12.xyzw
mov r3.xyzw, r2.xyzw
mov r0.x, r1.x
add r1.y, -r1.x, c13.x
if_ne r1.y, -r1.y
    add r1.y, -c2.y, c10.y
    cmp r1.y, r1.y, c10.x, c13.x
    if_ne r1.y, -r1.y
        mov r4.xyzw, c12.xyzw
        mov r4.xy, r0.yzyy
        texldl r4.xyzw, r4.xyzw, s2
        add r1.y, c0.z, -r4.x
        cmp r1.y, r1.y, c10.x, c13.x
        mul r1.z, c0.z, c0.w
        add r1.w, c0.w, -c0.z
        mul r1.w, r0.w, r1.w
        add r1.w, c0.w, -r1.w
        max r1.w, r1.w, c13.y
        rcp r1.w, r1.w
        mul r1.z, r1.z, r1.w
        mul r1.w, r4.x, c13.w
        max r1.w, r1.w, c13.z
        add r1.z, r1.z, r1.w
        add r1.z, r1.z, -r4.x
        cmp r1.z, r1.z, c10.x, c13.x
        add r1.z, -r1.z, c13.x
        min r1.y, r1.y, r1.z
        add r1.z, -r4.y, c14.x
        cmp r1.z, r1.z, c10.x, c13.x
        min r1.y, r1.y, r1.z
        cmp r2.xyzw, -r1.y, r2.xyzw, c12.xyzw
        mov r3.xyzw, r2.xyzw
        cmp r1.x, -r1.y, r1.x, c13.x
        mov r0.x, r1.x
    else
    endif
    mov r1.x, r0.x
    add r1.x, -r1.x, c13.x
    if_ne r1.x, -r1.x
        mul r1.xy, c0.xyxx, c10.yzyy
        add r1.zw, r0.xxyz, -r1.xxxy
        mul r1.zw, r1.xxzw, c14.xxyz
        add r1.zw, r1.xxzw, c15.xxxy
        rcp r2.x, c1.x
        rcp r2.y, c1.y
        mul r1.zw, r1.xxzw, r2.xxxy
        mov r2.xy, r1.zwzz
        mov r2.z, c1.z
        mov r4.xyz, r2.xyzx
        mul r1.z, c0.z, c0.w
        add r1.w, c0.w, -c0.z
        mul r0.w, r0.w, r1.w
        add r0.w, c0.w, -r0.w
        max r0.w, r0.w, c13.y
        rcp r0.w, r0.w
        mul r0.w, r1.z, r0.w
        mul r4.xyz, r4.xyzx, r0.w
        mul r5.xyz, r4.x, c3.xyzx
        mul r6.xyz, r4.y, c4.xyzx
        add r5.xyz, r5.xyzx, r6.xyzx
        mul r6.xyz, r4.z, c5.xyzx
        add r5.xyz, r5.xyzx, r6.xyzx
        add r5.xyz, r5.xyzx, c6.xyzx
        add r5.xyz, r5.xyzx, -c7.xyzx
        dp3 r0.w, r5.xyzx, r5.xyzx
        rsq r2.w, r0.w
        rcp r2.w, r2.w
        add r4.w, r2.w, -c8.w
        cmp r4.w, r4.w, c10.x, c13.x
        add r4.w, -r4.w, c13.x
        mov r5.w, -r2.w
        add r5.w, r5.w, c14.w
        cmp r5.w, r5.w, c10.x, c13.x
        add r5.w, -r5.w, c13.x
        max r4.w, r4.w, r5.w
        mov r6.xyzw, r3.xyzw
        cmp r6.xyzw, -r4.w, r6.xyzw, c12.xyzw
        mov r3.xyzw, r6.xyzw
        cmp r0.x, -r4.w, r0.x, c13.x
        add r0.x, -r0.x, c13.x
        if_ne r0.x, -r0.x
            add r6.xy, -r1.xyxx, c15.zwzz
            mov r6.z, c0.x
            mov r6.w, c10.x
            add r6.zw, r0.xxyz, -r6.xxzw
            max r6.zw, r6.xxzw, r1.xxxy
            mov r7.x, c0.x
            mov r7.y, c10.x
            add r7.xy, r0.yzyy, r7.xyxx
            min r7.xy, r7.xyxx, r6.xyxx
            mov r7.z, c10.x
            mov r7.w, c0.y
            add r7.zw, r0.xxyz, -r7.xxzw
            max r7.zw, r7.xxzw, r1.xxxy
            mov r8.x, c10.x
            mov r8.y, c0.y
            add r8.xy, r0.yzyy, r8.xyxx
            min r8.xy, r8.xyxx, r6.xyxx
            mov r9.xyzw, c12.xyzw
            mov r9.xy, r6.zwzz
            texldl r9.xyzw, r9.xyzw, s0
            add r0.x, r9.x, -c1.w
            mul r0.x, r0.x, c2.x
            mov_sat r0.x, r0.x
            add r6.zw, r6.xxzw, -r1.xxxy
            mul r6.zw, r6.xxzw, c14.xxyz
            add r6.zw, r6.xxzw, c15.xxxy
            rcp r8.z, c1.x
            rcp r8.w, c1.y
            mul r6.zw, r6.xxzw, r8.xxzw
            mov r2.xy, r6.zwzz
            mov r2.z, c1.z
            mov r9.xyz, r2.xyzx
            mul r0.x, r0.x, r1.w
            add r0.x, c0.w, -r0.x
            max r0.x, r0.x, c13.y
            rcp r0.x, r0.x
            mul r0.x, r1.z, r0.x
            mul r9.xyz, r9.xyzx, r0.x
            mov r10.xyzw, c12.xyzw
            mov r10.xy, r7.xyxx
            texldl r10.xyzw, r10.xyzw, s0
            add r0.x, r10.x, -c1.w
            mul r0.x, r0.x, c2.x
            mov_sat r0.x, r0.x
            add r6.zw, r7.xxxy, -r1.xxxy
            mul r6.zw, r6.xxzw, c14.xxyz
            add r6.zw, r6.xxzw, c15.xxxy
            rcp r7.x, c1.x
            rcp r7.y, c1.y
            mul r6.zw, r6.xxzw, r7.xxxy
            mov r2.xy, r6.zwzz
            mov r2.z, c1.z
            mov r10.xyz, r2.xyzx
            mul r0.x, r0.x, r1.w
            add r0.x, c0.w, -r0.x
            max r0.x, r0.x, c13.y
            rcp r0.x, r0.x
            mul r0.x, r1.z, r0.x
            mul r10.xyz, r10.xyzx, r0.x
            mov r11.xyzw, c12.xyzw
            mov r11.xy, r7.zwzz
            texldl r11.xyzw, r11.xyzw, s0
            add r0.x, r11.x, -c1.w
            mul r0.x, r0.x, c2.x
            mov_sat r0.x, r0.x
            add r6.zw, r7.xxzw, -r1.xxxy
            mul r6.zw, r6.xxzw, c14.xxyz
            add r6.zw, r6.xxzw, c15.xxxy
            rcp r7.x, c1.x
            rcp r7.y, c1.y
            mul r6.zw, r6.xxzw, r7.xxxy
            mov r2.xy, r6.zwzz
            mov r2.z, c1.z
            mov r7.xyz, r2.xyzx
            mul r0.x, r0.x, r1.w
            add r0.x, c0.w, -r0.x
            max r0.x, r0.x, c13.y
            rcp r0.x, r0.x
            mul r0.x, r1.z, r0.x
            mul r7.xyz, r7.xyzx, r0.x
            mov r11.xyzw, c12.xyzw
            mov r11.xy, r8.xyxx
            texldl r11.xyzw, r11.xyzw, s0
            add r0.x, r11.x, -c1.w
            mul r0.x, r0.x, c2.x
            mov_sat r0.x, r0.x
            add r6.zw, r8.xxxy, -r1.xxxy
            mul r6.zw, r6.xxzw, c14.xxyz
            add r6.zw, r6.xxzw, c15.xxxy
            rcp r8.x, c1.x
            rcp r8.y, c1.y
            mul r6.zw, r6.xxzw, r8.xxxy
            mov r2.xy, r6.zwzz
            mov r2.z, c1.z
            mul r0.x, r0.x, r1.w
            add r0.x, c0.w, -r0.x
            max r0.x, r0.x, c13.y
            rcp r0.x, r0.x
            mul r0.x, r1.z, r0.x
            mul r2.xyz, r2.xyzx, r0.x
            add r1.xy, r1.xyxx, -r0.yzyy
            cmp r1.xy, r1.xyxx, c11.zwzz, c15.yzyy
            add r1.xy, -r1.xyxx, c15.yzyy
            add r8.xyz, r10.xyzx, -r4.xyzx
            dp3 r0.x, r8.xyzx, r8.xyzx
            add r9.xyz, r4.xyzx, -r9.xyzx
            dp3 r1.z, r9.xyzx, r9.xyzx
            add r0.x, r0.x, -r1.z
            cmp r0.x, r0.x, c10.x, c13.x
            max r0.x, r1.x, r0.x
            add r0.yz, r0.xyzx, -r6.xxyx
            cmp r0.yz, r0.xyzx, c11.xzwx, c15.xyzx
            min r0.x, r0.x, r0.y
            cmp r6.xyz, -r0.x, r9.xyzx, r8.xyzx
            add r2.xyz, r2.xyzx, -r4.xyzx
            dp3 r0.x, r2.xyzx, r2.xyzx
            add r7.xyz, r4.xyzx, -r7.xyzx
            dp3 r1.z, r7.xyzx, r7.xyzx
            add r0.x, r0.x, -r1.z
            cmp r0.x, r0.x, c10.x, c13.x
            max r0.x, r1.y, r0.x
            min r0.x, r0.x, r0.z
            cmp r0.xyz, -r0.x, r7.xyzx, r2.xyzx
            mul r1.xyz, r6.zxyz, r0.yzxy
            mul r0.xyz, r6.yzxy, r0.zxyz
            add r0.xyz, r0.xyzx, -r1.xyzx
            dp3 r1.x, r0.xyzx, r0.xyzx
            max r1.x, r1.x, c16.x
            rsq r1.x, r1.x
            mov r1.xyz, r1.x
            mul r0.xyz, r0.xyzx, r1.xyzx
            dp3 r1.x, r0.xyzx, -r4.xyzx
            cmp r1.x, r1.x, c10.x, c13.x
            cmp r0.xyz, -r1.x, r0.xyzx, -r0.xyzx
            mul r1.xyz, r0.x, c3.xyzx
            mul r2.xyz, r0.y, c4.xyzx
            add r1.xyz, r1.xyzx, r2.xyzx
            mul r0.xyz, r0.z, c5.xyzx
            add r0.xyz, r1.xyzx, r0.xyzx
            mov r1.xyz, r2.w
            rcp r2.x, r1.x
            rcp r2.y, r1.y
            rcp r2.z, r1.z
            mul r1.xyz, -r5.xyzx, r2.xyzx
            dp3 r1.x, r0.xyzx, r1.xyzx
            mov_sat r1.x, r1.x
            mov r1.y, -r2.w
            add r1.y, c8.w, r1.y
            add r1.z, c8.w, -c7.w
            max r1.z, r1.z, c14.w
            rcp r1.z, r1.z
            mul r1.y, r1.y, r1.z
            mov_sat r1.y, r1.y
            rsq r0.w, r0.w
            rcp r0.w, r0.w
            mov r1.z, -r0.w
            add r1.z, c9.x, r1.z
            cmp r1.z, r1.z, c10.x, c13.x
            add r1.z, -r1.z, c13.x
            add r1.w, r0.w, -c9.y
            cmp r1.w, r1.w, c10.x, c13.x
            add r1.w, -r1.w, c13.x
            max r1.z, r1.z, r1.w
            dp3 r1.w, r0.xyzx, r5.xyzx
            cmp r1.w, r1.w, c10.x, c13.x
            add r1.w, -r1.w, c13.x
            max r1.z, r1.z, r1.w
            mov r1.w, r2.x
            cmp r1.w, -r1.z, r1.w, c13.x
            mov r2.x, r1.w
            add r1.z, -r1.z, c13.x
            if_ne r1.z, -r1.z
                max r1.z, r0.w, c13.y
                rcp r2.y, r1.z
                rcp r2.z, r1.z
                rcp r2.w, r1.z
                mul r2.yzw, r5.xxyz, r2.xyzw
                dp3 r1.z, r0.xyzx, r2.yzwy
                abs r1.z, r1.z
                add r1.z, -r1.z, c13.x
                mul r4.xyz, r0.xyzx, c2.w
                mul r1.z, r1.z, c14.y
                add r1.z, r1.z, c13.x
                mul r4.xyz, r4.xyzx, r1.z
                add r4.xyz, r5.xyzx, r4.xyzx
                abs r1.z, r2.w
                add r1.z, r1.z, c16.y
                cmp r1.z, r1.z, c10.x, c13.x
                cmp r5.xyz, -r1.z, c18.xyzx, c17.xyzx
                mul r6.xyz, r2.wyzw, r5.yzxy
                mul r5.xyz, r2.zwyz, r5.zxyz
                add r5.xyz, r5.xyzx, -r6.xyzx
                dp3 r1.z, r5.xyzx, r5.xyzx
                rsq r1.z, r1.z
                mov r6.xyz, r1.z
                mul r5.xyz, r6.xyzx, r5.xyzx
                mul r6.xyz, r2.wyzw, r5.yzxy
                mul r2.yzw, r2.xzwy, r5.xzxy
                add r2.yzw, r2.xyzw, -r6.xxyz
                mul r1.z, c9.z, c14.y
                mul r1.z, r1.z, c9.w
                mov r6.xyzw, r0.w
                mul r6.xyzw, r6.xyzw, r1.z
                mul r7.xyz, r5.xyzx, c19.xyzx
                mul r8.xyz, r2.yzwy, c19.xyzx
                add r9.xyz, r7.xyzx, r8.xyzx
                mul r9.xyz, r6.x, r9.xyzx
                add r9.xyz, r4.xyzx, r9.xyzx
                dp3 r1.z, r0.xyzx, r9.xyzx
                dp3 r1.w, r0.xyzx, r4.xyzx
                mul r4.w, r0.w, c16.z
                abs r5.w, r1.z
                add r4.w, r4.w, -r5.w
                cmp r4.w, r4.w, c10.x, c13.x
                rcp r1.z, r1.z
                mul r1.z, r1.w, r1.z
                max r1.z, r1.z, c16.w
                min r1.z, r1.z, c17.w
                cmp r1.z, -r4.w, c13.x, r1.z
                abs r10.xyz, r9.xyzx
                max r4.w, r10.y, r10.z
                max r4.w, r10.x, r4.w
                mul r1.z, r4.w, r1.z
                add r4.w, c9.y, -c9.x
                rcp r5.w, r4.w
                mul r5.w, c9.y, r5.w
                max r1.z, r1.z, c9.x
                rcp r1.z, r1.z
                mul r1.z, c9.x, r1.z
                add r1.z, -r1.z, c13.x
                mul r1.z, r5.w, r1.z
                mov r10.xyzw, c12.xyzw
                mov r10.xyz, r9.xyzx
                mov r9.xyzw, r10.xyzw
                texldl r9.xyzw, r9.xyzw, s1
                add r5.w, r9.x, c18.w
                add r1.z, r5.w, -r1.z
                cmp r1.z, r1.z, c10.x, c13.x
                add r1.z, -r1.z, c13.x
                cmp r1.z, -r1.z, c10.x, c13.x
                mul r5.xyz, r5.xyzx, c20.xyzx
                add r8.xyz, r5.xyzx, r8.xyzx
                mul r8.xyz, r6.y, r8.xyzx
                add r8.xyz, r4.xyzx, r8.xyzx
                dp3 r5.w, r0.xyzx, r8.xyzx
                mul r7.w, r0.w, c16.z
                abs r8.w, r5.w
                add r7.w, r7.w, -r8.w
                cmp r7.w, r7.w, c10.x, c13.x
                rcp r5.w, r5.w
                mul r5.w, r1.w, r5.w
                max r5.w, r5.w, c16.w
                min r5.w, r5.w, c17.w
                cmp r5.w, -r7.w, c13.x, r5.w
                abs r9.xyz, r8.xyzx
                max r7.w, r9.y, r9.z
                max r7.w, r9.x, r7.w
                mul r5.w, r7.w, r5.w
                rcp r7.w, r4.w
                mul r7.w, c9.y, r7.w
                max r5.w, r5.w, c9.x
                rcp r5.w, r5.w
                mul r5.w, c9.x, r5.w
                add r5.w, -r5.w, c13.x
                mul r5.w, r7.w, r5.w
                mov r10.xyzw, c12.xyzw
                mov r10.xyz, r8.xyzx
                mov r8.xyzw, r10.xyzw
                texldl r8.xyzw, r8.xyzw, s1
                add r7.w, r8.x, c18.w
                add r5.w, r7.w, -r5.w
                cmp r5.w, r5.w, c10.x, c13.x
                add r5.w, -r5.w, c13.x
                cmp r5.w, -r5.w, c10.x, c13.x
                add r1.z, r1.z, r5.w
                mul r2.yzw, r2.xyzw, c20.xxyz
                add r7.xyz, r7.xyzx, r2.yzwy
                mul r7.xyz, r6.z, r7.xyzx
                add r7.xyz, r4.xyzx, r7.xyzx
                dp3 r5.w, r0.xyzx, r7.xyzx
                mul r7.w, r0.w, c16.z
                abs r8.x, r5.w
                add r7.w, r7.w, -r8.x
                cmp r7.w, r7.w, c10.x, c13.x
                rcp r5.w, r5.w
                mul r5.w, r1.w, r5.w
                max r5.w, r5.w, c16.w
                min r5.w, r5.w, c17.w
                cmp r5.w, -r7.w, c13.x, r5.w
                abs r8.xyz, r7.xyzx
                max r7.w, r8.y, r8.z
                max r7.w, r8.x, r7.w
                mul r5.w, r7.w, r5.w
                rcp r7.w, r4.w
                mul r7.w, c9.y, r7.w
                max r5.w, r5.w, c9.x
                rcp r5.w, r5.w
                mul r5.w, c9.x, r5.w
                add r5.w, -r5.w, c13.x
                mul r5.w, r7.w, r5.w
                mov r10.xyzw, c12.xyzw
                mov r10.xyz, r7.xyzx
                mov r7.xyzw, r10.xyzw
                texldl r7.xyzw, r7.xyzw, s1
                add r7.x, r7.x, c18.w
                add r5.w, r7.x, -r5.w
                cmp r5.w, r5.w, c10.x, c13.x
                add r5.w, -r5.w, c13.x
                cmp r5.w, -r5.w, c10.x, c13.x
                add r1.z, r1.z, r5.w
                add r2.yzw, r5.xxyz, r2.xyzw
                mul r2.yzw, r6.w, r2.xyzw
                add r2.yzw, r4.xxyz, r2.xyzw
                dp3 r0.x, r0.xyzx, r2.yzwy
                mul r0.y, r0.w, c16.z
                abs r0.z, r0.x
                add r0.y, r0.y, -r0.z
                cmp r0.y, r0.y, c10.x, c13.x
                rcp r0.x, r0.x
                mul r0.x, r1.w, r0.x
                max r0.x, r0.x, c16.w
                min r0.x, r0.x, c17.w
                cmp r0.x, -r0.y, c13.x, r0.x
                abs r0.yzw, r2.xyzw
                max r1.w, r0.z, r0.w
                max r0.y, r0.y, r1.w
                mul r0.x, r0.y, r0.x
                rcp r0.y, r4.w
                mul r0.y, c9.y, r0.y
                max r0.x, r0.x, c9.x
                rcp r0.x, r0.x
                mul r0.x, c9.x, r0.x
                add r0.x, -r0.x, c13.x
                mul r0.x, r0.y, r0.x
                mov r10.xyzw, c12.xyzw
                mov r10.xyz, r2.yzwy
                mov r4.xyzw, r10.xyzw
                texldl r4.xyzw, r4.xyzw, s1
                add r0.y, r4.x, c18.w
                add r0.x, r0.y, -r0.x
                cmp r0.x, r0.x, c10.x, c13.x
                add r0.x, -r0.x, c13.x
                cmp r0.x, -r0.x, c10.x, c13.x
                add r0.x, r1.z, r0.x
                mul r0.x, r0.x, c19.w
                mov r2.x, r0.x
            else
            endif
            mov r0.x, r2.x
            add r0.x, -r0.x, c13.x
            max r0.yzw, c8.xxyz, c12.xxyz
            min r0.yzw, r0.xyzw, c21.xxyz
            mul r0.yzw, r0.xyzw, r1.y
            mul r0.yzw, r0.xyzw, r1.x
            min r0.yzw, r0.xyzw, c22.xxyz
            mul r0.xyz, -r0.yzwy, r0.x
            mov_sat r0.w, c2.z
            mul r0.xyz, r0.xyzx, r0.w
            mov r0.w, c10.x
            mov r3.xyzw, r0.xyzw
        else
        endif
    else
    endif
else
endif
mov oC0.xyzw, r3.xyzw
