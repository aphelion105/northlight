// Build 12340 SetupLighting: ESI=model, ECX=scene, [ESP+4]=lighting.
// Standalone native thunk: no DLL calls or allocations. Publishes only identity.
.intel_syntax noprefix
.text
.globl _selection
.globl _extra_entry
.globl _observer_entry
.globl _observer_clear
_selection:
    push ebp
    mov ebp, esp
    sub esp, 112
    push ebx
    push esi
    push edi
    mov [ebp-8], ecx
    mov edi, [ebp+8]
    push edi
    mov eax, 0x81e400
    call eax

    // World models keep the original selection, including the eight-light ring.
    mov edx, [ebp-8]
    cmp edx, [0xcd754c]
    je world_scene
    mov esi, [edx+8]
    mov ecx, 256
find_backdrop:
    cmp esi, 0x10000
    jb done
    test esi, 3
    jnz done
    cmp esi, 0xfffe0000
    jae done
    cmp [esi+0x28], edx
    jne done
    test byte ptr [esi+0x10], 1
    jz next_model
    mov eax, [esi+0x2c]
    mov eax, [eax+0x150]
    cmp dword ptr [eax+8], 4
    jb next_model
    mov ebx, [eax+12]
    cmp ebx, 0x10000
    jb next_model
    cmp ebx, 0xffeffffc
    ja next_model
    mov ebx, [ebx]
    or ebx, 0x2020
    and ebx, 0xffffff
    cmp ebx, 0x5f6975
    je backdrop
next_model:
    mov esi, [esi+12]
    dec ecx
    jnz find_backdrop
    jmp done
backdrop:
    // Preserve the loaded backdrop identity independently of its light count.
    // The producer validates camera, model generation and current draw domain.
    push eax
    .byte 0xb8
_observer_entry:
    .long 0
    test eax, eax
    jz observation_done
    inc dword ptr [eax]
    mov [eax+4], edx
    mov [eax+8], esi
    inc dword ptr [eax]
observation_done:
    pop eax
    mov ebx, [eax+264]
    test ebx, ebx
    jz done
    cmp ebx, 4
    ja done
    mov [ebp-20], ebx
    cmp dword ptr [edi+164], 4
    ja done
    // The optional getter belongs to the validated native MoreLights thunk.
    .byte 0xb8
_extra_entry:
    .long 0
    test eax, eax
    jz no_extra
    mov ecx, edi
    call eax
    test eax, eax
    jz no_extra
    cmp dword ptr [eax+4], 4
    ja done
no_extra:
    mov [ebp-12], eax
    mov ecx, [edi+164]
    xor edx, edx
save_primary:
    cmp edx, ecx
    jae save_extra
    mov eax, [edi+edx*4+132]
    mov [ebp+edx*8-96], eax
    mov eax, [edi+edx*4+148]
    mov [ebp+edx*8-92], eax
    inc edx
    jmp save_primary
save_extra:
    mov ebx, [ebp-12]
    test ebx, ebx
    jz saved
    xor ecx, ecx
extra_loop:
    cmp ecx, [ebx+4]
    jae saved
    mov eax, [ebx+ecx*4+8]
    mov [ebp+edx*8-96], eax
    mov eax, [ebx+ecx*4+24]
    mov [ebp+edx*8-92], eax
    inc ecx
    inc edx
    jmp extra_loop
saved:
    mov [ebp-4], edx
    test ebx, ebx
    jz primary_only
    mov dword ptr [ebx+4], 0
primary_only:
    mov dword ptr [edi+164], 0
    mov esi, [esi+0x1d0]
    add esi, 104
    mov ebx, [ebp-20]
own_loop:
    cmp dword ptr [esi+8], 1
    jne next_light
    cmp dword ptr [esi+96], 0
    je next_light
    mov eax, [esi+60]
    or eax, [esi+64]
    or eax, [esi+68]
    and eax, 0x7fffffff
    jz next_light
    mov ecx, [edi+164]
    mov [edi+ecx*4+132], esi
    mov dword ptr [edi+ecx*4+148], 0xbf800000
    inc dword ptr [edi+164]
next_light:
    add esi, 212
    dec ebx
    jnz own_loop
    xor ebx, ebx
fill_loop:
    cmp ebx, [ebp-4]
    jae done
    mov eax, [ebp+ebx*8-96]
    xor ecx, ecx
unique_primary:
    cmp ecx, [edi+164]
    jae fill_primary
    cmp eax, [edi+ecx*4+132]
    je next_saved
    inc ecx
    jmp unique_primary
fill_primary:
    cmp ecx, 4
    jae fill_extra
    mov [edi+ecx*4+132], eax
    mov eax, [ebp+ebx*8-92]
    mov [edi+ecx*4+148], eax
    inc dword ptr [edi+164]
    jmp next_saved
fill_extra:
    mov esi, [ebp-12]
    test esi, esi
    jz done
    xor ecx, ecx
unique_extra:
    cmp ecx, [esi+4]
    jae append_extra
    cmp eax, [esi+ecx*4+8]
    je next_saved
    inc ecx
    jmp unique_extra
append_extra:
    cmp ecx, 4
    jae done
    mov [esi+ecx*4+8], eax
    mov eax, [ebp+ebx*8-92]
    mov [esi+ecx*4+24], eax
    inc dword ptr [esi+4]
next_saved:
    inc ebx
    jmp fill_loop
world_scene:
    .byte 0xb8
_observer_clear:
    .long 0
    test eax, eax
    jz done
    inc dword ptr [eax]
    mov dword ptr [eax+4], 0
    mov dword ptr [eax+8], 0
    inc dword ptr [eax]
done:
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret 4
