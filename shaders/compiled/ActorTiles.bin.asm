ps_3_0
dcl_texcoord0 v0
def c68 = 1.60000000e+01, 1.60000000e+01, 1.60000000e+01, 1.00000000e+00
def c69 = 5.00000000e-01, 5.00000000e-01, 0.00000000e+00, -1.60000000e+01
def c70 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c71 = 2.00000000e+00, 3.00000000e+00, 4.00000000e+00, 5.00000000e+00
def c73 = 6.00000000e+00, 7.00000000e+00, 8.00000000e+00, 9.00000000e+00
def c74 = 1.00000000e+01, 1.10000000e+01, 1.20000000e+01, 1.30000000e+01
def c75 = 1.40000000e+01, 1.50000000e+01, 0.00000000e+00, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s12
mul r0.x, c72.y, c68.x
rcp r0.y, r0.x
rcp r0.z, r0.x
mul r0.xy, v0.xyxx, r0.yzyy
frc r1.xyzw, r0.xyxx
add r0.xy, r0.xyxx, -r1.xyzw
mul r0.xy, r0.xyxx, c68.yzyy
add r0.xy, r0.xyxx, c69.xyxx
mov r0.z, c68.w
mov r0.w, c69.z
rep i0.xyzw
    mov r3.x, r0.w
    add r3.x, r3.x, c69.w
    cmp r3.x, r3.x, c69.z, c68.w
    add r3.x, -r3.x, c68.w
    if_ne r3.x, -r3.x
        break
    else
    endif
    mov r3.x, r0.w
    add r3.x, r0.y, r3.x
    mul r3.x, r3.x, c72.y
    mov r3.y, r0.z
    mul r3.z, r0.x, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c68.w
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c71.x
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c71.y
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c71.z
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c71.w
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c73.x
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c73.y
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c73.z
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c73.w
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c74.x
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c74.y
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c74.z
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c74.w
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c75.x
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.y, r3.y, r4.x
    mov r0.z, r3.y
    add r3.z, r0.x, c75.y
    mul r3.z, r3.z, c72.y
    mov r2.x, r3.z
    mov r2.y, r3.x
    mov r2.z, c69.z
    mov r2.w, c69.z
    mov r4.xyzw, r2.xyzw
    mov r1.xyzw, c70.xyzw
    mov r1.xy, r4.xyxx
    mov r4.xyzw, r1.xyzw
    texldl r4.xyzw, r4.xyzw, s12
    min r3.x, r3.y, r4.x
    mov r0.z, r3.x
    mov r3.x, r0.w
    add r3.x, r3.x, c68.w
    mov r0.w, r3.x
endrep
mov r0.x, r0.z
mov r0.y, r0.z
mov r1.x, r0.x
mov r1.y, r0.y
mov r1.z, r0.z
mov r1.w, c68.w
mov oC0.xyzw, r1.xyzw
