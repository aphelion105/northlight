ps_3_0
dcl_texcoord0 v0
def c68 = 5.00000000e-01, 5.00000000e-01, -1.00000000e+00, -1.00000000e+00
def c69 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c70 = 1.00000000e+00, -9.99989986e-01, 9.99999975e-06, 3.90625000e-03
def c71 = 1.00000005e-03, 1.50000006e-01, 1.50000006e-01, 1.50000006e-01
def c72 = 3.00000000e+00, 1.60000000e+01, -1.44269502e+00, 2.50000000e-01
def c73 = 2.99999993e-02, 2.00000000e+00, -2.40000000e+01, 4.16666679e-02
def c74 = -4.49999988e-01, -4.49999988e-01, -4.49999988e-01, -6.42799973e-01
def c75 = 5.67000008e+00, -7.37368822e-01, 6.75490320e-01, 7.37368822e-01
def c76 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c77 = 1.00000000e+01, 9.99999978e-03, 9.99999975e-05, -8.00000000e+00
def c78 = -4.00000000e+00, 3.33333343e-01, 2.00000000e+00, 2.00000000e+00
def c79 = 1.50000000e+00, 3.49999994e-01, 4.00000000e+00, 6.42799973e-01
def c80 = 3.32229996e+00, 1.44269502e+00, 0.00000000e+00, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s10
dcl_2d s12
dcl_2d s1
dcl_2d s8
dcl_2d s14
dcl_2d s0
mul r0.xy, v0.xyxx, c33.zwzz
frc r1.xyzw, r0.xyxx
add r0.xy, r0.xyxx, -r1.xyzw
add r0.zw, r0.xxxy, c68.xxxy
rcp r1.x, c33.z
rcp r1.y, c33.w
mul r1.xy, r0.zwzz, r1.xyxx
mov r2.xyzw, c69.xyzw
mov r2.xy, r1.xyxx
texldl r2.xyzw, r2.xyzw, s8
mov r3.xyzw, r2.xyzw
mul r1.zw, r1.xxxy, c33.xxxy
frc r4.xyzw, r1.zwzz
add r1.zw, r1.xxzw, -r4.xxxy
add r4.xy, c33.xyxx, c68.zwzz
max r1.zw, r1.xxzw, c69.xxxy
min r1.zw, r1.xxzw, r4.xxxy
add r1.zw, r1.xxzw, c68.xxxy
mul r1.zw, r1.xxzw, c0.xxxy
mov r5.xyzw, c69.xyzw
mov r5.xy, r1.zwzz
texldl r5.xyzw, r5.xyzw, s1
add r4.z, r5.x, -c1.w
mul r4.z, r4.z, c2.x
mov_sat r4.z, r4.z
cmp r4.w, -c30.y, c69.x, c70.x
add r5.x, r4.z, c70.y
cmp r5.x, r5.x, c69.x, c70.x
min r4.w, r4.w, r5.x
if_ne r4.w, -r4.w
    mul r4.w, c0.z, c0.w
    add r5.x, c0.w, -c0.z
    mul r4.z, r4.z, r5.x
    add r4.z, c0.w, -r4.z
    max r4.z, r4.z, c70.z
    rcp r4.z, r4.z
    mul r4.z, r4.w, r4.z
    mul r5.y, r2.w, c30.z
    add r5.y, -r5.y, c70.x
    rcp r5.z, c20.z
    mul r5.z, r5.y, r5.z
    mov_sat r5.z, r5.z
    mov r6.xyz, r2.xyzx
    add r5.w, -r5.z, c70.w
    cmp r5.w, r5.w, c69.x, c70.x
    if_ne r5.w, -r5.w
        mul r5.w, r4.z, c1.z
        mad r5.w, r5.w, c25.x, c25.y
        max r5.w, r5.w, c69.x
        log r5.w, r5.w
        mul r5.w, c25.z, r5.w
        exp r5.w, r5.w
        mov_sat r5.w, r5.w
        add r5.w, r5.w, c68.z
        mad r5.w, c25.w, r5.w, c70.x
        max r5.w, r5.w, c71.x
        add r6.w, -r5.w, c70.x
        mul r7.xyz, r6.w, c26.xyzx
        mov r8.xyzw, c69.xyzw
        mov r8.xy, r1.xyxx
        texldl r8.xyzw, r8.xyzw, s12
        mov r9.xyzw, c69.xyzw
        mov r9.xy, r1.xyxx
        texldl r9.xyzw, r9.xyzw, s0
        mov r10.xyzw, c69.xyzw
        mov r10.xy, r1.xyxx
        texldl r10.xyzw, r10.xyzw, s10
        mul r9.xyz, r9.xyzx, r10.w
        max r8.xyz, r8.xyzx, c71.yzwy
        mul r8.xyz, r5.w, r8.xyzx
        min r10.xyz, r7.xyzx, r9.xyzx
        add r9.xyz, r9.xyzx, -r10.xyzx
        max r8.xyz, r8.xyzx, r9.xyzx
        mul r9.xyz, r2.xyzx, r5.w
        rcp r10.x, r8.x
        rcp r10.y, r8.y
        rcp r10.z, r8.z
        mul r9.xyz, r9.xyzx, r10.xyzx
        rcp r6.w, r4.z
        mul r6.w, c30.w, r6.w
        max r6.w, r6.w, c72.x
        min r6.w, r6.w, c72.y
        mul r7.w, r4.z, c73.x
        max r7.w, r7.w, c72.w
        rcp r7.w, r7.w
        mul r7.w, r7.w, c72.z
        mul r8.w, c30.z, c73.y
        rcp r9.w, c20.z
        mul r8.w, r8.w, r9.w
        mov r10.xyzw, c69.xyzw
        mov r10.xy, r1.xyxx
        texldl r10.xyzw, r10.xyzw, s14
        max r11.xyz, r9.xyzx, c74.xyzx
        mov r9.w, c70.x
        mov r11.w, c68.x
        mov r12.x, c70.x
        mov r12.y, c69.x
        mov r12.z, c69.x
        rep i0.xyzw
            mov r16.w, r12.z
            add r16.w, r16.w, c73.z
            cmp r16.w, r16.w, c69.x, c70.x
            add r16.w, -r16.w, c70.x
            if_ne r16.w, -r16.w
                break
            else
            endif
            mov r22.xy, r12.xyxx
            mov r16.w, r11.w
            mul r16.w, r16.w, c73.w
            rsq r14.w, r16.w
            rcp r14.z, r14.w
            mul r16.w, r14.z, r6.w
            mul r22.xy, r22.xyxx, r16.w
            add r22.xy, r0.zwzz, r22.xyxx
            frc r23.xyzw, r22.xyxx
            add r22.xy, r22.xyxx, -r23.xyzw
            add r22.zw, c33.xxzw, c68.xxzw
            max r22.xy, r22.xyxx, c69.xyxx
            min r22.xy, r22.xyxx, r22.zwzz
            add r22.xy, r22.xyxx, c68.xyxx
            rcp r14.x, c33.z
            rcp r14.y, c33.w
            mul r22.xy, r22.xyxx, r14.xyxx
            mov r19.xyzw, c69.xyzw
            mov r19.xy, r22.xyxx
            mov r23.xyzw, r19.xyzw
            texldl r23.xyzw, r23.xyzw, s8
            mov r13.xyzw, c69.xyzw
            mov r13.xy, r22.xyxx
            mov r24.xyzw, r13.xyzw
            texldl r24.xyzw, r24.xyzw, s14
            dp3 r16.w, r10.xyzx, r24.xyzx
            add r16.w, r16.w, c74.w
            mul r16.w, r16.w, c75.x
            mov_sat r16.w, r16.w
            mul r22.zw, r22.xxxy, c33.xxxy
            frc r24.xyzw, r22.zwzz
            add r22.zw, r22.xxzw, -r24.xxxy
            max r22.zw, r22.xxzw, c69.xxxy
            min r22.zw, r22.xxzw, r4.xxxy
            add r22.zw, r22.xxzw, c68.xxxy
            mul r22.zw, r22.xxzw, c0.xxxy
            mov r15.xyzw, c69.xyzw
            mov r15.xy, r22.zwzz
            mov r24.xyzw, r15.xyzw
            texldl r24.xyzw, r24.xyzw, s1
            add r20.w, r24.x, -c1.w
            mul r20.w, r20.w, c2.x
            mov_sat r20.w, r20.w
            mul r20.w, r20.w, r5.x
            add r20.w, c0.w, -r20.w
            max r20.w, r20.w, c70.z
            rcp r16.x, r20.w
            mul r20.w, r4.w, r16.x
            add r20.w, r20.w, -r4.z
            abs r20.w, r20.w
            mul r20.w, r20.w, r7.w
            exp r12.w, r20.w
            add r20.w, r23.w, -r2.w
            abs r20.w, r20.w
            mul r20.w, r20.w, r8.w
            add r20.w, -r20.w, c70.x
            mov_sat r20.w, r20.w
            mul r20.w, r12.w, r20.w
            add r22.z, -r10.w, c68.x
            cmp r22.z, r22.z, c69.x, c70.x
            cmp r16.w, -r22.z, c70.x, r16.w
            mul r16.w, r20.w, r16.w
            mul r23.xyz, r23.xyzx, r5.w
            mov r18.xyzw, c69.xyzw
            mov r18.xy, r22.xyxx
            mov r24.xyzw, r18.xyzw
            texldl r24.xyzw, r24.xyzw, s12
            mov r17.xyzw, c69.xyzw
            mov r17.xy, r22.xyxx
            mov r25.xyzw, r17.xyzw
            texldl r25.xyzw, r25.xyzw, s0
            mov r21.xyzw, c69.xyzw
            mov r21.xy, r22.xyxx
            mov r22.xyzw, r21.xyzw
            texldl r22.xyzw, r22.xyzw, s10
            mul r22.xyz, r25.xyzx, r22.w
            max r24.xyz, r24.xyzx, c71.yzwy
            mul r24.xyz, r5.w, r24.xyzx
            min r25.xyz, r7.xyzx, r22.xyzx
            add r22.xyz, r22.xyzx, -r25.xyzx
            max r22.xyz, r24.xyzx, r22.xyzx
            rcp r20.x, r22.x
            rcp r20.y, r22.y
            rcp r20.z, r22.z
            mul r22.xyz, r23.xyzx, r20.xyzx
            max r22.xyz, r22.xyzx, c74.xyzx
            mul r22.xyz, r22.xyzx, r16.w
            mov r23.xyz, r11.xyzx
            add r22.xyz, r23.xyzx, r22.xyzx
            mov r11.xyz, r22.xyzx
            mov r20.w, r9.w
            add r16.w, r20.w, r16.w
            mov r9.w, r16.w
            mov r22.xy, r12.xyxx
            mul r16.w, r22.x, c75.y
            mov r22.xy, r12.xyxx
            mul r20.w, r22.y, c75.z
            add r16.w, r16.w, -r20.w
            mov r22.xy, r12.xyxx
            mul r20.w, r22.x, c75.z
            mov r22.xy, r12.xyxx
            mul r22.x, r22.y, c75.w
            add r20.w, r20.w, -r22.x
            mov r16.y, r16.w
            mov r16.z, r20.w
            mov r22.xy, r16.yzyy
            mov r12.xy, r22.xyxx
            mov r16.w, r11.w
            add r16.w, r16.w, c70.x
            mov r11.w, r16.w
            mov r16.w, r12.z
            add r16.w, r16.w, c70.x
            mov r12.z, r16.w
        endrep
        mov r2.xyz, r11.xyzx
        mov r0.z, r9.w
        rcp r7.x, r0.z
        rcp r7.y, r0.z
        rcp r7.z, r0.z
        mul r2.xyz, r2.xyzx, r7.xyzx
        add r2.xyz, r2.xyzx, -r9.xyzx
        mul r2.xyz, r5.z, r2.xyzx
        add r2.xyz, r9.xyzx, r2.xyzx
        mul r2.xyz, r2.xyzx, r8.xyzx
        rcp r7.x, r5.w
        rcp r7.y, r5.w
        rcp r7.z, r5.w
        mul r2.xyz, r2.xyzx, r7.xyzx
        mov r6.xyz, r2.xyzx
    else
    endif
    mov r2.xyz, r6.xyzx
    mov r3.xyz, r2.xyzx
    min r6.xyz, r2.xyzx, c69.xyzx
    mov r7.x, c69.x
    mov r7.y, c69.x
    mov r7.z, c69.x
    mov r8.xyzw, c69.xyzw
    mov r8.xy, r1.xyxx
    texldl r8.xyzw, r8.xyzw, s14
    dp3 r0.z, r8.xyzx, c3.xyzx
    dp3 r0.w, r8.xyzx, c4.xyzx
    dp3 r1.x, r8.xyzx, c5.xyzx
    mov r9.x, r0.z
    mov r9.y, r0.w
    mov r9.z, r1.x
    mul r0.zw, c0.xxxy, c68.xxxy
    add r1.xy, r1.zwzz, -r0.zwzz
    mul r1.xy, r1.xyxx, c76.xyxx
    add r1.xy, r1.xyxx, c76.zwzz
    rcp r1.z, c1.x
    rcp r1.w, c1.y
    mul r1.xy, r1.xyxx, r1.zwzz
    mov r1.z, c1.z
    mov r10.xyz, r1.xyzx
    mul r10.xyz, r10.xyzx, r4.z
    rcp r1.w, c20.z
    mul r1.w, r5.y, r1.w
    mov_sat r1.w, r1.w
    add r5.yzw, c16.xxyz, -c28.xxyz
    mul r5.yzw, c16.w, r5.xyzw
    add r5.yzw, c28.xxyz, r5.xyzw
    dp3 r2.w, r8.xyzx, r5.yzwy
    mul r2.w, r2.w, c77.x
    add r2.w, -r2.w, c70.x
    mov_sat r2.w, r2.w
    max r1.w, r1.w, r2.w
    mul r2.w, r4.z, c77.y
    max r2.w, r2.w, c71.y
    rcp r2.w, r2.w
    mul r2.w, r2.w, c68.z
    abs r5.y, c1.x
    mul r5.y, r5.y, c33.x
    max r5.y, r5.y, c77.z
    rcp r5.y, r5.y
    mul r5.y, r5.y, c73.y
    mul r5.y, r5.y, r4.z
    mul r5.y, r5.y, c73.y
    mov r5.z, c69.x
    rep i0.xyzw
        mov r17.x, r5.z
        add r17.x, r17.x, c77.w
        cmp r17.x, r17.x, c69.x, c70.x
        add r17.x, -r17.x, c70.x
        if_ne r17.x, -r17.x
            break
        else
        endif
        mov r17.x, r5.z
        mov r17.y, r5.z
        add r17.y, r17.y, c78.x
        cmp r17.y, r17.y, c69.x, c70.x
        add r17.y, -r17.y, c70.x
        cmp r17.y, -r17.y, c69.x, c70.x
        add r17.x, r17.x, r17.y
        mul r17.x, r17.x, c78.y
        abs r17.y, r17.x
        frc r17.y, r17.y
        cmp r17.z, r17.x, c69.x, c70.x
        add r17.z, -r17.z, c70.x
        cmp r17.y, -r17.z, -r17.y, r17.y
        mul r17.y, r17.y, c72.x
        add r17.y, r17.y, c68.z
        frc r18.xyzw, r17.x
        add r17.x, r17.x, -r18.xyzw
        add r17.x, r17.x, c68.z
        mov r11.x, r17.y
        mov r11.y, r17.x
        mov r17.xy, r11.xyxx
        add r17.xy, r0.xyxx, r17.xyxx
        add r17.zw, c33.xxzw, c68.xxzw
        max r17.xy, r17.xyxx, c69.xyxx
        min r17.xy, r17.xyxx, r17.zwzz
        add r17.xy, r17.xyxx, c68.xyxx
        rcp r11.z, c33.z
        rcp r11.w, c33.w
        mul r17.xy, r17.xyxx, r11.zwzz
        mul r18.xy, r17.xyxx, c33.xyxx
        frc r19.xyzw, r18.xyxx
        add r18.xy, r18.xyxx, -r19.xyzw
        max r18.xy, r18.xyxx, c69.xyxx
        min r18.xy, r18.xyxx, r4.xyxx
        add r18.xy, r18.xyxx, c68.xyxx
        mul r18.xy, r18.xyxx, c0.xyxx
        mov r12.xyzw, c69.xyzw
        mov r12.xy, r18.xyxx
        mov r19.xyzw, r12.xyzw
        texldl r19.xyzw, r19.xyzw, s1
        add r18.z, r19.x, -c1.w
        mul r18.z, r18.z, c2.x
        mov_sat r18.z, r18.z
        mul r18.w, r18.z, r5.x
        add r18.w, c0.w, -r18.w
        max r18.w, r18.w, c70.z
        rcp r5.w, r18.w
        mul r18.w, r4.w, r5.w
        mov r19.xy, r11.xyxx
        mul r19.xy, r19.xyxx, c78.zwzz
        add r19.xy, r0.xyxx, r19.xyxx
        max r19.xy, r19.xyxx, c69.xyxx
        min r17.zw, r19.xxxy, r17.xxzw
        add r17.zw, r17.xxzw, c68.xxxy
        rcp r13.x, c33.z
        rcp r13.y, c33.w
        mul r17.zw, r17.xxzw, r13.xxxy
        mul r17.zw, r17.xxzw, c33.xxxy
        frc r19.xyzw, r17.zwzz
        add r17.zw, r17.xxzw, -r19.xxxy
        max r17.zw, r17.xxzw, c69.xxxy
        min r17.zw, r17.xxzw, r4.xxxy
        add r17.zw, r17.xxzw, c68.xxxy
        mul r17.zw, r17.xxzw, c0.xxxy
        mov r14.xyzw, c69.xyzw
        mov r14.xy, r17.zwzz
        mov r19.xyzw, r14.xyzw
        texldl r19.xyzw, r19.xyzw, s1
        add r17.z, r19.x, -c1.w
        mul r17.z, r17.z, c2.x
        mov_sat r17.z, r17.z
        add r17.w, r17.z, c70.y
        cmp r17.w, r17.w, c69.x, c70.x
        mul r17.z, r17.z, r5.x
        add r17.z, c0.w, -r17.z
        max r17.z, r17.z, c70.z
        rcp r6.w, r17.z
        mul r17.z, r4.w, r6.w
        add r17.z, r18.w, -r17.z
        abs r17.z, r17.z
        mul r17.z, r17.z, c79.x
        mul r19.x, r4.z, c79.y
        min r17.z, r17.z, r19.x
        cmp r17.z, -r17.w, c69.x, r17.z
        mov r15.xyzw, c69.xyzw
        mov r15.xy, r17.xyxx
        mov r19.xyzw, r15.xyzw
        texldl r19.xyzw, r19.xyzw, s8
        add r18.xy, r18.xyxx, -r0.zwzz
        mul r18.xy, r18.xyxx, c76.xyxx
        add r18.xy, r18.xyxx, c76.zwzz
        rcp r13.z, c1.x
        rcp r13.w, c1.y
        mul r18.xy, r18.xyxx, r13.zwzz
        mov r1.xy, r18.xyxx
        mov r1.z, c1.z
        mov r20.xyz, r1.xyzx
        mul r20.xyz, r20.xyzx, r18.w
        add r20.xyz, r20.xyzx, -r10.xyzx
        mov r21.xyz, r9.xyzx
        dp3 r17.w, r20.xyzx, r21.xyzx
        rcp r7.w, r5.y
        mul r18.x, r7.w, c79.z
        mul r17.w, r17.w, r18.x
        add r17.w, r17.w, c70.x
        mov_sat r17.w, r17.w
        mul r18.x, r19.w, c30.z
        add r18.x, -r18.x, c70.x
        rcp r9.w, c20.z
        mul r18.x, r18.x, r9.w
        mov_sat r18.x, r18.x
        add r18.y, -r8.w, c68.x
        cmp r18.y, r18.y, c69.x, c70.x
        mov r16.xyzw, c69.xyzw
        mov r16.xy, r17.xyxx
        mov r20.xyzw, r16.xyzw
        texldl r20.xyzw, r20.xyzw, s14
        dp3 r17.x, r8.xyzx, r20.xyzx
        add r17.x, -r17.x, c79.w
        mul r17.x, r17.x, c80.x
        mov_sat r17.x, r17.x
        cmp r17.x, -r18.y, c70.x, r17.x
        add r17.y, r18.w, -r4.z
        abs r17.y, r17.y
        add r17.y, r17.y, -r17.z
        max r17.y, r17.y, c69.x
        mul r17.y, r17.y, r2.w
        mul r17.y, r17.y, c80.y
        exp r10.w, r17.y
        mul r17.x, r17.x, r10.w
        mul r17.x, r17.x, r17.w
        mul r17.x, r17.x, r1.w
        mul r17.x, r17.x, r18.x
        add r17.y, r18.z, c70.y
        cmp r17.y, r17.y, c69.x, c70.x
        cmp r17.y, -r17.y, c69.x, c70.x
        mul r17.x, r17.x, r17.y
        mov r17.yzw, r7.xxyz
        min r18.xyz, r19.xyzx, c69.xyzx
        add r18.xyz, r18.xyzx, -r6.xyzx
        mul r18.xyz, r17.x, r18.xyzx
        min r17.xyz, r17.yzwy, r18.xyzx
        mov r7.xyz, r17.xyzx
        mov r17.x, r5.z
        add r17.x, r17.x, c70.x
        mov r5.z, r17.x
    endrep
    mov r0.xyz, r7.xyzx
    add r0.xyz, r2.xyzx, r0.xyzx
    mov r3.xyz, r0.xyzx
else
endif
mov oC0.xyzw, r3.xyzw
