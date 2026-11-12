# northlight-test: requires=windows-gpu,zig,client
"""Production hdr_sun_glare_gpu.cpp: daylight headroom, moon, clouds and occlusion."""
from verify_hdr_sun_gpu import main

if __name__ == '__main__':
    main()
