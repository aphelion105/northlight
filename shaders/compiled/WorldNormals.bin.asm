ps_3_0
dcl_texcoord0 v0
def c68 = -1.00000000e+00, -1.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c69 = 5.00000000e-01, 5.00000000e-01, 9.99999975e-06, -9.99989986e-01
def c70 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c71 = 1.00000000e+00, 2.00000000e+00, -2.00000000e+00, 1.50000000e+00
def c72 = -1.00000000e+00, 1.00000000e+00, 4.00000000e+01, 9.99999996e-13
def c73 = 1.00000000e+00, 1.00000000e+00, 3.49999994e-01, 3.99999991e-02
def c74 = -9.49999988e-01, 1.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c75 = -8.50000024e-01, -3.00000000e+00, 1.99999996e-02, 1.99999996e-02
def c76 = 2.98999995e-01, 5.87000012e-01, 1.14000000e-01, 5.00000007e-02
defi i0 = 255, 0, 0, 0
dcl_2d s1
dcl_2d s0
mul r0.xy, v0.xyxx, c33.xyxx
frc r1.xyzw, r0.xyxx
add r0.xy, r0.xyxx, -r1.xyzw
add r0.zw, c33.xxxy, c68.xxxy
max r0.xy, r0.xyxx, c68.zwzz
min r0.xy, r0.xyxx, r0.zwzz
add r0.xy, r0.xyxx, c69.xyxx
mul r0.xy, r0.xyxx, c0.xyxx
mov r1.xyzw, c70.xyzw
mov r1.xy, r0.xyxx
texldl r1.xyzw, r1.xyzw, s1
add r1.x, r1.x, -c1.w
mul r1.x, r1.x, c2.x
mov_sat r1.x, r1.x
mul r1.y, c0.z, c0.w
add r1.z, c0.w, -c0.z
mul r1.w, r1.x, r1.z
add r1.w, c0.w, -r1.w
max r1.w, r1.w, c69.z
rcp r1.w, r1.w
mul r1.w, r1.y, r1.w
add r1.x, r1.x, c69.w
cmp r1.x, r1.x, c68.z, c71.x
add r1.x, -r1.x, c71.x
mov r2.xyzw, r3.xyzw
cmp r2.x, -r1.x, r2.x, c68.z
mov r3.x, r2.x
mov r2.xyzw, r3.xyzw
cmp r2.x, -r1.x, r2.y, c68.z
mov r3.y, r2.x
mov r2.xyzw, r3.xyzw
cmp r2.x, -r1.x, r2.z, c71.x
mov r3.z, r2.x
mov r2.xyzw, r3.xyzw
cmp r2.x, -r1.x, r2.w, r1.w
mov r3.w, r2.x
mov r2.xyzw, r3.xyzw
mov r3.xyzw, r4.xyzw
cmp r2.xyzw, -r1.x, r3.xyzw, r2.xyzw
mov r4.xyzw, r2.xyzw
add r1.x, -r1.x, c71.x
if_ne r1.x, -r1.x
    mul r2.xy, c0.xyxx, c69.xyxx
    add r2.zw, r0.xxxy, -r2.xxxy
    mul r2.zw, r2.xxzw, c71.xxyz
    add r2.zw, r2.xxzw, c72.xxxy
    rcp r3.x, c1.x
    rcp r3.y, c1.y
    mul r2.zw, r2.xxzw, r3.xxxy
    mov r3.xy, r2.zwzz
    mov r3.z, c1.z
    mov r5.xyz, r3.xyzx
    mul r5.xyz, r5.xyzx, r1.w
    mul r1.x, c1.x, c71.w
    mul r1.x, r1.x, c33.x
    mul r1.x, r1.x, c69.x
    rcp r2.z, r1.w
    mul r1.x, r1.x, r2.z
    max r1.x, r1.x, c71.y
    min r1.x, r1.x, c72.z
    mov r2.z, c71.x
    mov r6.x, c68.z
    mov r6.y, c68.z
    mov r6.z, c68.z
    mov r2.w, c68.z
    rep i0.xyzw
        mov r20.z, r2.w
        add r20.z, r20.z, c71.z
        cmp r20.z, r20.z, c68.z, c71.x
        add r20.z, -r20.z, c71.x
        if_ne r20.z, -r20.z
            break
        else
        endif
        mov r20.z, r2.w
        abs r20.z, r20.z
        add r20.z, -r20.z, -r20.z
        cmp r20.z, r20.z, c68.z, c71.x
        add r20.z, -r20.z, c71.x
        mov r11.x, c0.x
        mov r11.y, c68.z
        mov r22.xy, r11.xyxx
        mov r9.z, c68.z
        mov r9.w, c0.y
        mov r22.zw, r9.xxzw
        cmp r20.zw, -r20.z, r22.xxzw, r22.xxxy
        add r22.xy, r0.xyxx, -r20.zwzz
        max r22.xy, r22.xyxx, r2.xyxx
        add r22.zw, r0.xxxy, r20.xxzw
        add r23.xy, -r2.xyxx, c73.xyxx
        min r22.zw, r22.xxzw, r23.xxxy
        mov r10.xyzw, c70.xyzw
        mov r10.xy, r22.xyxx
        mov r24.xyzw, r10.xyzw
        texldl r24.xyzw, r24.xyzw, s1
        add r21.w, r24.x, -c1.w
        mul r21.w, r21.w, c2.x
        mov_sat r21.w, r21.w
        mul r21.w, r21.w, r1.z
        add r21.w, c0.w, -r21.w
        max r21.w, r21.w, c69.z
        rcp r6.w, r21.w
        mul r21.w, r1.y, r6.w
        add r23.zw, r22.xxxy, -r2.xxxy
        mul r23.zw, r23.xxzw, c71.xxyz
        add r23.zw, r23.xxzw, c72.xxxy
        rcp r9.x, c1.x
        rcp r9.y, c1.y
        mul r23.zw, r23.xxzw, r9.xxxy
        mov r3.xy, r23.zwzz
        mov r3.z, c1.z
        mov r24.xyz, r3.xyzx
        mul r24.xyz, r24.xyzx, r21.w
        mov r12.xyzw, c70.xyzw
        mov r12.xy, r22.zwzz
        mov r25.xyzw, r12.xyzw
        texldl r25.xyzw, r25.xyzw, s1
        add r21.w, r25.x, -c1.w
        mul r21.w, r21.w, c2.x
        mov_sat r21.w, r21.w
        mul r21.w, r21.w, r1.z
        add r21.w, c0.w, -r21.w
        max r21.w, r21.w, c69.z
        rcp r5.w, r21.w
        mul r21.w, r1.y, r5.w
        add r23.zw, r22.xxzw, -r2.xxxy
        mul r23.zw, r23.xxzw, c71.xxyz
        add r23.zw, r23.xxzw, c72.xxxy
        rcp r8.z, c1.x
        rcp r8.w, c1.y
        mul r23.zw, r23.xxzw, r8.xxzw
        mov r3.xy, r23.zwzz
        mov r3.z, c1.z
        mov r25.xyz, r3.xyzx
        mul r25.xyz, r25.xyzx, r21.w
        mov r13.xyz, r24.xyzx
        mov r21.xyz, r25.xyzx
        mov r11.z, c68.z
        rep i0.xyzw
            mov r21.w, r11.z
            add r21.w, r21.w, c71.z
            cmp r21.w, r21.w, c68.z, c71.x
            add r21.w, -r21.w, c71.x
            if_ne r21.w, -r21.w
                break
            else
            endif
            mov r21.w, r11.z
            abs r21.w, r21.w
            add r21.w, -r21.w, -r21.w
            cmp r21.w, r21.w, c68.z, c71.x
            add r21.w, -r21.w, c71.x
            cmp r23.zw, -r21.w, r20.xxzw, -r20.xxzw
            mov r21.w, r11.z
            abs r21.w, r21.w
            add r21.w, -r21.w, -r21.w
            cmp r21.w, r21.w, c68.z, c71.x
            add r21.w, -r21.w, c71.x
            cmp r26.xyz, -r21.w, r25.xyzx, r24.xyzx
            mul r27.xy, r23.zwzz, r1.x
            add r27.xy, r0.xyxx, r27.xyxx
            max r27.xy, r27.xyxx, r2.xyxx
            min r27.xy, r27.xyxx, r23.xyxx
            mul r27.xy, r27.xyxx, c33.xyxx
            frc r28.xyzw, r27.xyxx
            add r27.xy, r27.xyxx, -r28.xyzw
            max r27.xy, r27.xyxx, c68.zwzz
            min r27.xy, r27.xyxx, r0.zwzz
            add r27.xy, r27.xyxx, c69.xyxx
            mul r27.xy, r27.xyxx, c0.xyxx
            mov r7.xyzw, c70.xyzw
            mov r7.xy, r27.xyxx
            mov r28.xyzw, r7.xyzw
            texldl r28.xyzw, r28.xyzw, s1
            add r21.w, r28.x, -c1.w
            mul r21.w, r21.w, c2.x
            mov_sat r21.w, r21.w
            mul r21.w, r21.w, r1.z
            add r21.w, c0.w, -r21.w
            max r21.w, r21.w, c69.z
            rcp r3.w, r21.w
            mul r21.w, r1.y, r3.w
            add r27.zw, r27.xxxy, -r2.xxxy
            mul r27.zw, r27.xxzw, c71.xxyz
            add r27.zw, r27.xxzw, c72.xxxy
            rcp r8.x, c1.x
            rcp r8.y, c1.y
            mul r27.zw, r27.xxzw, r8.xxxy
            mov r3.xy, r27.zwzz
            mov r3.z, c1.z
            mov r28.xyz, r3.xyzx
            mul r28.xyz, r28.xyzx, r21.w
            add r29.xyz, r26.xyzx, -r5.xyzx
            add r30.xyz, r28.xyzx, -r5.xyzx
            dp3 r24.w, r29.xyzx, r30.xyzx
            dp3 r25.w, r29.xyzx, r29.xyzx
            dp3 r26.w, r30.xyzx, r30.xyzx
            mul r26.w, r25.w, r26.w
            max r26.w, r26.w, c72.w
            rsq r19.w, r26.w
            mul r24.w, r24.w, r19.w
            add r23.zw, r27.xxxy, -r23.xxzw
            max r23.zw, r23.xxzw, r2.xxxy
            min r23.zw, r23.xxzw, r23.xxxy
            mov r18.xyzw, c70.xyzw
            mov r18.xy, r23.zwzz
            mov r27.xyzw, r18.xyzw
            texldl r27.xyzw, r27.xyzw, s1
            add r26.w, r27.x, -c1.w
            mul r26.w, r26.w, c2.x
            mov_sat r26.w, r26.w
            mul r26.w, r26.w, r1.z
            add r26.w, c0.w, -r26.w
            max r26.w, r26.w, c69.z
            rcp r17.w, r26.w
            mul r26.w, r1.y, r17.w
            add r23.zw, r23.xxzw, -r2.xxxy
            mul r23.zw, r23.xxzw, c71.xxyz
            add r23.zw, r23.xxzw, c72.xxxy
            rcp r20.x, c1.x
            rcp r20.y, c1.y
            mul r23.zw, r23.xxzw, r20.xxxy
            mov r3.xy, r23.zwzz
            mov r3.z, c1.z
            mov r27.xyz, r3.xyzx
            mul r27.xyz, r27.xyzx, r26.w
            add r27.xyz, r28.xyzx, -r27.xyzx
            dp3 r23.z, r29.xyzx, r27.xyzx
            dp3 r23.w, r27.xyzx, r27.xyzx
            mul r23.w, r25.w, r23.w
            max r23.w, r23.w, c72.w
            rsq r15.w, r23.w
            mul r23.z, r23.z, r15.w
            mul r23.w, r1.w, c73.w
            max r23.w, r23.w, c73.z
            add r21.w, r21.w, -r1.w
            abs r21.w, r21.w
            add r21.w, r23.w, -r21.w
            cmp r21.w, r21.w, c68.z, c71.x
            min r23.z, r24.w, r23.z
            add r23.z, r23.z, c74.x
            cmp r23.z, r23.z, c68.z, c71.x
            max r21.w, r21.w, r23.z
            cmp r26.xyz, -r21.w, r28.xyzx, r26.xyzx
            mov r21.w, r11.z
            abs r21.w, r21.w
            add r21.w, -r21.w, -r21.w
            cmp r21.w, r21.w, c68.z, c71.x
            add r21.w, -r21.w, c71.x
            mov r27.xyz, r13.xyzx
            cmp r27.xyz, -r21.w, r27.xyzx, r26.xyzx
            mov r13.xyz, r27.xyzx
            mov r27.xyz, r21.xyzx
            cmp r26.xyz, -r21.w, r26.xyzx, r27.xyzx
            mov r21.xyz, r26.xyzx
            mov r21.w, r11.z
            add r21.w, r21.w, c71.x
            mov r11.z, r21.w
        endrep
        mov r23.xyz, r13.xyzx
        add r23.xyz, r23.xyzx, -r24.xyzx
        abs r23.xyz, r23.xyzx
        add r23.xyz, -r23.xyzx, -r23.xyzx
        cmp r23.xyz, r23.xyzx, c70.xyzx, c74.yzwy
        add r23.xyz, -r23.xyzx, c74.yzwy
        min r20.z, r23.x, r23.y
        min r20.z, r20.z, r23.z
        mov r23.xyz, r21.xyzx
        add r23.xyz, r23.xyzx, -r25.xyzx
        abs r23.xyz, r23.xyzx
        add r23.xyz, -r23.xyzx, -r23.xyzx
        cmp r23.xyz, r23.xyzx, c70.xyzx, c74.yzwy
        add r23.xyz, -r23.xyzx, c74.yzwy
        min r20.w, r23.x, r23.y
        min r20.w, r20.w, r23.z
        max r20.z, r20.z, r20.w
        mov r23.xyz, r13.xyzx
        cmp r23.xyz, -r20.z, r23.xyzx, r24.xyzx
        mov r13.xyz, r23.xyzx
        mov r26.xyz, r21.xyzx
        cmp r26.xyz, -r20.z, r26.xyzx, r25.xyzx
        mov r21.xyz, r26.xyzx
        mov r20.z, r2.z
        mov r14.w, r20.z
        add r27.xyz, r5.xyzx, -r23.xyzx
        add r28.xyz, r26.xyzx, -r5.xyzx
        dp3 r20.z, r27.xyzx, r28.xyzx
        dp3 r20.w, r27.xyzx, r27.xyzx
        dp3 r21.w, r28.xyzx, r28.xyzx
        mul r20.w, r20.w, r21.w
        max r20.w, r20.w, c72.w
        rsq r14.z, r20.w
        mul r20.z, r20.z, r14.z
        add r20.z, r20.z, c75.x
        cmp r20.z, r20.z, c68.z, c71.x
        add r20.z, -r20.z, c71.x
        add r23.xyz, r26.xyzx, -r23.xyzx
        mov r26.xyz, r19.xyzx
        cmp r23.xyz, -r20.z, r26.xyzx, r23.xyzx
        mov r19.xyz, r23.xyzx
        add r20.w, -r20.z, c71.x
        if_ne r20.w, -r20.w
            add r26.xyz, r5.xyzx, -r24.xyzx
            add r27.xyz, r25.xyzx, -r5.xyzx
            dp3 r20.w, r26.xyzx, r27.xyzx
            dp3 r21.w, r26.xyzx, r26.xyzx
            dp3 r23.w, r27.xyzx, r27.xyzx
            mul r21.w, r21.w, r23.w
            max r21.w, r21.w, c72.w
            rsq r13.w, r21.w
            mul r20.w, r20.w, r13.w
            add r20.w, r20.w, c75.x
            cmp r20.w, r20.w, c68.z, c71.x
            add r20.w, -r20.w, c71.x
            add r28.xyz, r25.xyzx, -r24.xyzx
            cmp r23.xyz, -r20.w, r23.xyzx, r28.xyzx
            mov r19.xyz, r23.xyzx
            cmp r20.z, -r20.w, r20.z, c71.x
            add r20.z, -r20.z, c71.x
            if_ne r20.z, -r20.z
                mov r14.w, c68.z
                mov r17.x, c68.z
                mov r17.y, c68.z
                mov r17.z, c68.z
                mov r11.w, c68.z
                rep i0.xyzw
                    mov r20.z, r11.w
                    add r20.z, r20.z, c75.y
                    cmp r20.z, r20.z, c68.z, c71.x
                    add r20.z, -r20.z, c71.x
                    if_ne r20.z, -r20.z
                        break
                    else
                    endif
                    mov r20.z, r11.w
                    abs r20.z, r20.z
                    add r20.z, -r20.z, -r20.z
                    cmp r20.z, r20.z, c68.z, c71.x
                    add r20.z, -r20.z, c71.x
                    mov r20.w, r11.w
                    add r20.w, r20.w, c68.x
                    abs r20.w, r20.w
                    add r20.w, -r20.w, -r20.w
                    cmp r20.w, r20.w, c68.z, c71.x
                    add r20.w, -r20.w, c71.x
                    cmp r23.xy, -r20.w, r22.xyxx, r22.zwzz
                    cmp r20.zw, -r20.z, r23.xxxy, r0.xxxy
                    mov r16.xyzw, c70.xyzw
                    mov r16.xy, r20.zwzz
                    mov r23.xyzw, r16.xyzw
                    texldl r23.xyzw, r23.xyzw, s0
                    dp3 r20.z, r23.xyzx, c76.xyzx
                    mov r20.w, r11.w
                    abs r20.w, r20.w
                    add r20.w, -r20.w, -r20.w
                    cmp r20.w, r20.w, c68.z, c71.x
                    add r20.w, -r20.w, c71.x
                    mov r21.w, r11.w
                    add r21.w, r21.w, c68.x
                    abs r21.w, r21.w
                    add r21.w, -r21.w, -r21.w
                    cmp r21.w, r21.w, c68.z, c71.x
                    add r21.w, -r21.w, c71.x
                    mov r23.x, r11.w
                    add r23.x, r23.x, c71.z
                    abs r23.x, r23.x
                    add r23.x, -r23.x, -r23.x
                    cmp r23.x, r23.x, c68.z, c71.x
                    add r23.x, -r23.x, c71.x
                    mov r15.x, r20.w
                    mov r15.y, r21.w
                    mov r15.z, r23.x
                    mov r23.xyz, r15.xyzx
                    mul r23.xyz, r20.z, r23.xyzx
                    mov r28.xyz, r17.xyzx
                    add r23.xyz, r28.xyzx, r23.xyzx
                    mov r17.xyz, r23.xyzx
                    mov r20.z, r11.w
                    add r20.z, r20.z, c71.x
                    mov r11.w, r20.z
                endrep
                mov r22.xyz, r17.xyzx
                mov r23.xyz, r17.xyzx
                add r20.zw, r22.x, -r23.xxyz
                abs r20.zw, r20.xxzw
                mov r22.xyz, r17.xyzx
                mov r23.xyz, r17.xyzx
                add r22.xy, r22.x, r23.yzyy
                add r22.xy, r22.xyxx, c75.zwzz
                rcp r14.x, r22.x
                rcp r14.y, r22.y
                mul r20.zw, r20.xxzw, r14.xxxy
                add r20.z, r20.z, -r20.w
                abs r20.w, r20.z
                add r20.w, -r20.w, c76.w
                cmp r20.w, r20.w, c68.z, c71.x
                cmp r20.z, r20.z, c68.z, c71.x
                add r21.w, r25.z, -r5.z
                abs r21.w, r21.w
                add r22.x, r24.z, -r5.z
                abs r22.x, r22.x
                add r21.w, r21.w, -r22.x
                cmp r21.w, r21.w, c68.z, c71.x
                cmp r20.z, -r20.w, r21.w, r20.z
                abs r20.z, r20.z
                add r20.z, -r20.z, -r20.z
                cmp r20.z, r20.z, c68.z, c71.x
                cmp r22.xyz, -r20.z, r26.xyzx, r27.xyzx
                mov r19.xyz, r22.xyzx
            else
            endif
        else
        endif
        mov r20.z, r14.w
        mov r2.z, r20.z
        mov r22.xyz, r19.xyzx
        mov r13.xyz, r22.xyzx
        mov r20.z, r2.w
        abs r20.z, r20.z
        add r20.z, -r20.z, -r20.z
        cmp r20.z, r20.z, c68.z, c71.x
        add r20.z, -r20.z, c71.x
        mov r23.xyz, r6.xyzx
        mul r24.xyz, r23.zxyz, r22.yzxy
        mul r23.xyz, r23.yzxy, r22.zxyz
        add r23.xyz, r23.xyzx, -r24.xyzx
        cmp r22.xyz, -r20.z, r23.xyzx, r22.xyzx
        mov r6.xyz, r22.xyzx
        mov r20.z, r2.w
        add r20.z, r20.z, c71.x
        mov r2.w, r20.z
    endrep
    mov r0.xyz, r6.xyzx
    mov r1.xyz, r6.xyzx
    mov r2.xyw, r6.xyxz
    dp3 r0.w, r1.xyzx, r2.xywx
    max r0.w, r0.w, c72.w
    rsq r0.w, r0.w
    mov r1.xyz, r0.w
    mul r0.xyz, r0.xyzx, r1.xyzx
    dp3 r0.w, r0.xyzx, -r5.xyzx
    cmp r0.w, r0.w, c68.z, c71.x
    cmp r0.xyz, -r0.w, r0.xyzx, -r0.xyzx
    mul r1.xyz, r0.x, c3.xyzx
    mul r2.xyw, r0.y, c4.xyxz
    add r1.xyz, r1.xyzx, r2.xywx
    mul r0.xyz, r0.z, c5.xyzx
    add r0.xyz, r1.xyzx, r0.xyzx
    mov r0.w, r2.z
    mov r1.xyz, r0.xyzx
    mov r1.w, r0.w
    mov r0.xyzw, r1.xyzw
    mov r4.xyzw, r0.xyzw
else
endif
mov oC0.xyzw, r4.xyzw
