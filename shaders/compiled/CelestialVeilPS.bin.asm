ps_3_0
dcl_texcoord0 v0
def c49 = 5.00000000e-01, 5.00000000e-01, -1.00000000e+00, -1.00000000e+00
def c50 = 0.00000000e+00, 0.00000000e+00, -9.99999940e-01, 1.00000000e+00
def c51 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c52 = 3.00000003e-03, 2.00000000e+00, -2.00000000e+00, -9.99999997e-07
def c53 = -1.00000000e+00, 1.00000000e+00, -5.00000000e-01, 3.49999994e-01
def c54 = 3.00000000e+00, -1.19999997e-01, -2.50000004e-02, -8.00000038e-03
def c55 = 5.00000000e-01, 5.00000000e-01, 0.00000000e+00, 0.00000000e+00
def c56 = 2.50000000e-01, 3.49999994e-01, 4.00000006e-01, -1.11000001e+00
def c57 = 1.18257001e-01, 2.99457997e-01, 3.80526990e-01, -1.00000005e-03
def c58 = -1.44269502e+00, 1.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c59 = 4.04499993e-02, 4.04499993e-02, 4.04499993e-02, 0.00000000e+00
def c60 = 7.73993805e-02, 7.73993805e-02, 7.73993805e-02, 0.00000000e+00
def c61 = 5.49999997e-02, 5.49999997e-02, 5.49999997e-02, 0.00000000e+00
def c62 = 9.47867334e-01, 9.47867334e-01, 9.47867334e-01, 0.00000000e+00
def c63 = 2.40000010e+00, 2.40000010e+00, 2.40000010e+00, 0.00000000e+00
dcl_2d s0
dcl_2d s3
dcl_2d s5
dcl_2d s4
dcl_2d s2
rcp r0.x, c0.x
rcp r0.y, c0.y
add r0.xy, r0.xyxx, c49.xyxx
frc r1.xyzw, r0.xyxx
add r0.xy, r0.xyxx, -r1.xyzw
mul r0.zw, v0.xxxy, r0.xxxy
frc r1.xyzw, r0.zwzz
add r0.zw, r0.xxzw, -r1.xxxy
add r0.xy, r0.xyxx, c49.zwzz
max r0.zw, r0.xxzw, c50.xxxy
min r0.xy, r0.zwzz, r0.xyxx
add r0.xy, r0.xyxx, c49.xyxx
mul r0.xy, r0.xyxx, c0.xyxx
mov r1.xyzw, c51.xyzw
mov r1.xy, r0.xyxx
texldl r1.xyzw, r1.xyzw, s0
add r0.z, r1.x, -c0.z
mul r0.z, r0.z, c0.w
mov_sat r0.z, r0.z
add r0.z, r0.z, c50.z
cmp r0.z, r0.z, c50.x, c50.w
add r0.z, -r0.z, c50.w
add r0.w, c11.x, -c11.z
add r0.w, r1.x, -r0.w
cmp r0.w, r0.w, c50.x, c50.w
add r0.w, -r0.w, c50.w
min r0.z, r0.z, r0.w
cmp r0.z, -r0.z, c50.x, c50.w
mov r0.w, r0.z
add r1.x, -c1.w, c49.x
cmp r1.x, r1.x, c50.x, c50.w
if_ne r1.x, -r1.x
    mov r1.xyzw, c51.xyzw
    mov r1.xy, r0.xyxx
    texldl r1.xyzw, r1.xyzw, s2
    add r0.x, c10.y, -r1.x
    cmp r0.x, r0.x, c50.x, c50.w
    add r0.y, c10.z, -r1.x
    cmp r0.y, r0.y, c50.x, c50.w
    add r0.y, -r0.y, c50.w
    min r0.x, r0.x, r0.y
    add r0.y, -r1.y, c52.x
    cmp r0.y, r0.y, c50.x, c50.w
    min r0.x, r0.x, r0.y
    cmp r0.x, -r0.x, r0.z, c50.x
    mov r0.w, r0.x
else
endif
mul r0.xy, c0.xyxx, c49.xyxx
add r0.xy, v0.xyxx, -r0.xyxx
mul r0.xy, r0.xyxx, c52.yzyy
add r0.xy, r0.xyxx, c53.xyxx
rcp r1.x, c1.x
rcp r1.y, c1.y
mul r0.xy, r0.xyxx, r1.xyxx
mul r1.xyz, r0.x, c2.xyzx
mul r0.xyz, r0.y, c3.xyzx
add r0.xyz, r1.xyzx, r0.xyzx
mul r1.xyz, c1.z, c4.xyzx
add r0.xyz, r0.xyzx, r1.xyzx
dp3 r1.x, r0.xyzx, c6.xyzx
add r1.y, r1.x, c52.w
texkill r1.y
add r1.y, c46.x, c53.z
cmp r1.y, r1.y, c50.x, c50.w
mov r1.z, r1.w
cmp r1.z, -r1.y, r1.z, c50.w
mov r1.w, r1.z
add r2.x, -r1.y, c50.w
if_ne r2.x, -r2.x
    cmp r2.x, -r1.x, c50.x, c50.w
    add r2.x, -r2.x, c50.w
    cmp r1.z, -r2.x, r1.z, c50.w
    mov r1.w, r1.z
    cmp r1.y, -r2.x, r1.y, c50.w
    add r2.x, -r1.y, c50.w
    if_ne r2.x, -r2.x
        dp3 r2.x, r0.xyzx, c7.xyzx
        dp3 r2.y, r0.xyzx, c8.xyzx
        mov r2.z, -r2.y
        mov r2.xy, r2.xzxx
        mul r2.z, r1.x, c6.w
        mul r2.z, r2.z, c46.y
        rcp r3.x, r2.z
        rcp r3.y, r2.z
        mul r2.xy, r2.xyxx, r3.xyxx
        abs r2.zw, r2.xxxy
        max r2.z, r2.z, r2.w
        add r2.z, -r2.z, c50.w
        cmp r2.z, r2.z, c50.x, c50.w
        cmp r1.z, -r2.z, r1.z, c50.w
        mov r1.w, r1.z
        cmp r1.y, -r2.z, r1.y, c50.w
        add r1.y, -r1.y, c50.w
        if_ne r1.y, -r1.y
            mul r1.yz, r2.xxyx, c49.xxyx
            add r1.yz, r1.xyzx, c49.xxyx
            mov r2.xyzw, c51.xyzw
            mov r2.xy, r1.yzyy
            texldl r2.xyzw, r2.xyzw, s4
            mov_sat r1.y, r2.x
            add r1.y, -r1.y, c50.w
            mov r1.w, r1.y
        else
        endif
    else
    endif
else
endif
mov r1.y, r1.w
mul r0.w, r0.w, r1.y
add r0.w, -r0.w, c50.w
dp3 r1.y, r0.xyzx, c7.xyzx
dp3 r0.x, r0.xyzx, c8.xyzx
mov r0.y, r1.y
mov r0.z, -r0.x
mov r0.xy, r0.yzyy
mul r0.z, r1.x, c6.w
rcp r1.x, r0.z
rcp r1.y, r0.z
mul r0.xy, r0.xyxx, r1.xyxx
dp2add r0.x, r0.xyxx, r0.xyxx, c50.x
rsq r0.x, r0.x
rcp r0.x, r0.x
mul r0.y, r0.x, r0.x
mul r0.z, c12.y, c53.w
add r1.x, r0.x, -r0.z
add r0.z, c12.y, -r0.z
rcp r0.z, r0.z
mul r0.z, r1.x, r0.z
mov_sat r0.z, r0.z
mul r1.x, r0.z, r0.z
mul r0.z, r0.z, c52.y
add r0.z, -r0.z, c54.x
mul r0.z, r1.x, r0.z
add r0.z, -r0.z, c50.w
texldl r1.xyzw, c55.xyzw, s3
mov_sat r1.x, r1.x
texldl r2.xyzw, c55.xyzw, s5
mov_sat r1.y, r2.x
mul r1.y, c48.w, r1.y
mul r1.z, r0.y, c54.y
exp r1.z, r1.z
mul r1.w, r0.y, c54.z
exp r1.w, r1.w
mul r2.x, r0.y, c54.w
exp r2.x, r2.x
mov r2.y, r1.z
mov r2.z, r1.w
mov r2.w, r2.x
mov r3.xyz, r2.yzwy
dp3 r1.z, r3.xyzx, c56.xyzx
mul r1.z, r1.x, r1.z
mov r2.xyz, r2.yzwy
dp3 r1.w, r2.xyzx, c57.xyzx
mul r1.y, r1.y, r1.w
max r1.y, r1.z, r1.y
add r1.z, -c11.w, c49.x
cmp r1.z, r1.z, c50.x, c50.w
mul r0.y, -c12.x, r0.y
exp r0.y, r0.y
mul r0.y, c12.z, r0.y
mul r0.y, r0.y, r1.x
mul r1.y, c47.w, r1.y
add r0.y, r0.y, r1.y
mul r0.y, r0.y, r0.z
mul r1.y, c12.z, c53.w
add r1.w, r0.x, c49.z
max r1.w, r1.w, c50.x
mul r1.w, r1.w, c56.w
exp r1.w, r1.w
mul r1.y, r1.y, r1.w
mul r0.z, r1.y, r0.z
mul r0.z, r0.z, r1.x
cmp r0.y, -r1.z, r0.z, r0.y
mul r0.y, r0.w, r0.y
add r0.z, r0.y, c57.w
texkill r0.z
mul r0.z, -c12.x, r0.x
mul r0.x, r0.z, r0.x
exp r0.x, r0.x
mov_sat r0.x, r0.x
mul r0.z, r0.x, r0.x
mul r0.x, r0.x, c52.y
add r0.x, -r0.x, c54.x
mul r0.x, r0.z, r0.x
add r1.xyz, c48.xyzx, -c47.xyzx
mul r0.xzw, r0.x, r1.xxyz
add r0.xzw, c47.xxyz, r0.xxzw
mov_sat r0.xzw, r0.xxzw
mul r1.x, r0.y, c58.x
mul r0.xzw, r1.x, r0.xxzw
exp r1.x, r0.x
exp r1.y, r0.z
exp r1.z, r0.w
mov r0.xzw, -r1.xxyz
add r0.xzw, r0.xxzw, c58.yxzw
add r1.xyz, -r0.xzwx, c59.xyzx
cmp r1.xyz, r1.xyzx, c51.xyzx, c58.yzwy
add r1.xyz, -r1.xyzx, c58.yzwy
mul r2.xyz, r0.xzwx, c60.xyzx
add r3.xyz, r0.xzwx, c61.xyzx
mul r3.xyz, r3.xyzx, c62.xyzx
log r4.x, r3.x
log r4.y, r3.y
log r4.z, r3.z
mul r3.xyz, r4.xyzx, c63.xyzx
exp r4.x, r3.x
exp r4.y, r3.y
exp r4.z, r3.z
cmp r1.xyz, -r1.xyzx, r4.xyzx, r2.xyzx
add r1.w, -c10.x, c49.x
cmp r1.w, r1.w, c50.x, c50.w
cmp r0.xzw, -r1.w, r0.xxzw, r1.xxyz
mov r1.xyz, r0.xzwx
mov r1.w, r0.y
mov oC0.xyzw, r1.xyzw
