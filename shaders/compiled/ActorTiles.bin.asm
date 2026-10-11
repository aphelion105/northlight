ps_3_0
dcl_texcoord0 v0
def c68 = 4.00000000e+00, 4.00000000e+00, 4.00000000e+00, 1.00000000e+00
def c69 = 5.00000000e-01, 5.00000000e-01, 1.00000000e+00, 0.00000000e+00
def c70 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c71 = 2.00000000e+00, 0.00000000e+00, 3.00000000e+00, 0.00000000e+00
def c73 = 0.00000000e+00, 1.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c74 = 2.00000000e+00, 1.00000000e+00, 3.00000000e+00, 1.00000000e+00
def c75 = 0.00000000e+00, 2.00000000e+00, 1.00000000e+00, 2.00000000e+00
def c76 = 2.00000000e+00, 2.00000000e+00, 3.00000000e+00, 2.00000000e+00
def c77 = 3.00000000e+00, 3.00000000e+00, 0.00000000e+00, 0.00000000e+00
dcl_2d s12
mul r0.x, c72.y, c68.x
rcp r0.y, r0.x
rcp r0.z, r0.x
mul r0.xy, v0.xyxx, r0.yzyy
frc r1.xyzw, r0.xyxx
add r0.xy, r0.xyxx, -r1.xyzw
mul r0.xy, r0.xyxx, c68.yzyy
add r0.xy, r0.xyxx, c69.xyxx
mul r0.zw, r0.xxxy, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r0.zwzz
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
add r0.zw, r0.xxxy, c69.xxzw
mul r0.zw, r0.xxzw, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r0.zwzz
mov r3.xyzw, r1.xyzw
texldl r3.xyzw, r3.xyzw, s12
min r0.z, r2.x, r3.x
add r2.xy, r0.xyxx, c71.xyxx
mul r2.xy, r2.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r2.xyxx
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
min r0.z, r0.z, r2.x
add r2.xy, r0.xyxx, c71.zwzz
mul r2.xy, r2.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r2.xyxx
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
min r0.z, r0.z, r2.x
add r2.xy, r0.xyxx, c73.xyxx
mul r2.xy, r2.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r2.xyxx
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
min r0.z, r0.z, r2.x
add r2.xy, r0.xyxx, c73.zwzz
mul r2.xy, r2.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r2.xyxx
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
min r0.z, r0.z, r2.x
add r2.xy, r0.xyxx, c74.xyxx
mul r2.xy, r2.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r2.xyxx
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
min r0.z, r0.z, r2.x
add r2.xy, r0.xyxx, c74.zwzz
mul r2.xy, r2.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r2.xyxx
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
min r0.z, r0.z, r2.x
add r2.xy, r0.xyxx, c75.xyxx
mul r2.xy, r2.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r2.xyxx
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
min r0.z, r0.z, r2.x
add r2.xy, r0.xyxx, c75.zwzz
mul r2.xy, r2.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r2.xyxx
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
min r0.z, r0.z, r2.x
add r2.xy, r0.xyxx, c76.xyxx
mul r2.xy, r2.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r2.xyxx
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
min r0.z, r0.z, r2.x
add r2.xy, r0.xyxx, c76.zwzz
mul r2.xy, r2.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r2.xyxx
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
min r0.z, r0.z, r2.x
add r2.xy, r0.xyxx, c71.yzyy
mul r2.xy, r2.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r2.xyxx
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
min r0.z, r0.z, r2.x
add r2.xy, r0.xyxx, c74.yzyy
mul r2.xy, r2.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r2.xyxx
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
min r0.z, r0.z, r2.x
add r2.xy, r0.xyxx, c76.yzyy
mul r2.xy, r2.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r2.xyxx
mov r2.xyzw, r1.xyzw
texldl r2.xyzw, r2.xyzw, s12
min r0.z, r0.z, r2.x
add r0.xy, r0.xyxx, c77.xyxx
mul r0.xy, r0.xyxx, c72.y
mov r1.xyzw, c70.xyzw
mov r1.xy, r0.xyxx
texldl r1.xyzw, r1.xyzw, s12
min r0.x, r0.z, r1.x
min r0.x, r0.x, c68.w
mov r0.xyz, r0.x
mov r0.w, c68.w
mov oC0.xyzw, r0.xyzw
