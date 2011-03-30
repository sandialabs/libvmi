/*
 * The LibVMI Library is an introspection library that simplifies access to 
 * memory in a target virtual machine or in a file containing a dump of 
 * a system's physical memory.  LibVMI is based on the XenAccess Library.
 *
 * Copyright (C) 2010 Sandia National Laboratories
 * Author: Bryan D. Payne (bpayne@sandia.gov)
 */

#include "libvmi.h"
#include "private.h"

/* Tries to find the kernel page directory by doing an exhaustive search
 * through the memory space for the System process.  The page directory
 * location is then pulled from this eprocess struct.
 */
status_t get_kpgd_method2 (vmi_instance_t vmi, uint32_t *sysproc)
{
    /* get address for Idle process */
    if ((*sysproc = windows_find_eprocess(vmi, "Idle")) == 0){
        dbprint("--failed to find System process.\n");
        goto error_exit;
    }
    dbprint("--got PA to PsInititalSystemProcess (0x%.8x).\n", *sysproc);

    /* get address for page directory (from system process) */
    /*TODO this 0x18 offset should not be hard coded below */
    if (VMI_FAILURE == vmi_read_32_pa(vmi, *sysproc + 0x18, &vmi->kpgd)){
        dbprint("--failed to resolve PD for Idle process\n");
        goto error_exit;
    }
    vmi->kpgd += vmi->page_offset; /* store vaddr */

    if (vmi->kpgd == vmi->page_offset){
        dbprint("--kpgd was zero\n");
        goto error_exit;
    }

    return VMI_SUCCESS;
error_exit:
    return VMI_FAILURE;
}

uint32_t windows_find_cr3 (vmi_instance_t vmi)
{
    uint32_t sysproc = 0;
    get_kpgd_method2(vmi, &sysproc);
    return vmi->kpgd - vmi->page_offset;
}


/* Tries to find the kernel page directory using the RVA value for
 * PSInitialSystemProcess and the ntoskrnl value to lookup the System
 * process, and the extract the page directory location from this
 * eprocess struct.
 */
status_t get_kpgd_method1 (vmi_instance_t vmi, uint32_t *sysproc)
{
    if (VMI_FAILURE == vmi_read_32_ksym(vmi, "PsInitialSystemProcess", sysproc)){
        dbprint("--failed to read pointer for system process\n");
        goto error_exit;
    }
    *sysproc = vmi_translate_kv2p(vmi, *sysproc);
    dbprint("--got PA to PsInititalSystemProcess (0x%.8x).\n", *sysproc);

    if (VMI_FAILURE == vmi_read_32_pa(vmi, *sysproc + vmi->os.windows_instance.pdbase_offset, &vmi->kpgd)){
        dbprint("--failed to resolve pointer for system process\n");
        goto error_exit;
    }
    vmi->kpgd += vmi->page_offset; /* store vaddr */

    if (vmi->kpgd == vmi->page_offset){
        dbprint("--kpgd was zero\n");
        goto error_exit;
    }

    return VMI_SUCCESS;
error_exit:
    return VMI_FAILURE;
}

static status_t get_kpgd_method0 (vmi_instance_t vmi, uint32_t *sysproc)
{
    if (VMI_FAILURE == windows_symbol_to_address(vmi, "PsActiveProcessHead", sysproc)){
        dbprint("--failed to resolve PsActiveProcessHead\n");
        goto error_exit;
    }
    if (VMI_FAILURE == vmi_read_32_va(vmi, *sysproc, 0, sysproc)){
        dbprint("--failed to translate PsActiveProcessHead\n");
        goto error_exit;
    }
    *sysproc = vmi_translate_kv2p(vmi, *sysproc) - vmi->os.windows_instance.tasks_offset;
    dbprint("--got PA to PsActiveProcessHead (0x%.8x).\n", *sysproc);

    if (VMI_FAILURE == vmi_read_32_pa(vmi, *sysproc + vmi->os.windows_instance.pdbase_offset, &vmi->kpgd)){
        dbprint("--failed to resolve pointer for system process\n");
        goto error_exit;
    }
    vmi->kpgd += vmi->page_offset; /* store vaddr */

    if (vmi->kpgd == vmi->page_offset){
        dbprint("--kpgd was zero\n");
        goto error_exit;
    }

    return VMI_SUCCESS;
error_exit:
    return VMI_FAILURE;
}

status_t windows_init (vmi_instance_t vmi)
{
    uint32_t sysproc = 0;

    /* get base address for kernel image in memory */
    if (VMI_FAILURE == windows_symbol_to_address(vmi, "KernBase", &vmi->os.windows_instance.ntoskrnl)){
        dbprint("--address translation failure, switching PAE mode\n");
        vmi->pae = !vmi->pae;

        if (VMI_FAILURE == windows_symbol_to_address(vmi, "KernBase", &vmi->os.windows_instance.ntoskrnl)){
            errprint("Address translation failure.\n");
            goto error_exit;
        }
    }
    vmi->os.windows_instance.ntoskrnl -= vmi->page_offset;
    dbprint("**set ntoskrnl (0x%.8x).\n", vmi->os.windows_instance.ntoskrnl);

    /* get the kernel page directory location */
    if (VMI_FAILURE == get_kpgd_method0(vmi, &sysproc)){
        dbprint("--kpgd method0 failed, trying method1\n");
        if (VMI_FAILURE == get_kpgd_method1(vmi, &sysproc)){
            dbprint("--kpgd method1 failed, trying method2\n");
            if (VMI_FAILURE == get_kpgd_method2(vmi, &sysproc)){
                errprint("Failed to find kernel page directory.\n");
                goto error_exit;
            }
        }
    }
    dbprint("**set kpgd (0x%.8x).\n", vmi->kpgd);

    /* get address start of process list */
    vmi_read_32_pa(vmi, sysproc + vmi->os.windows_instance.tasks_offset, &vmi->init_task);
    dbprint("**set init_task (0x%.8x).\n", vmi->init_task);

    return VMI_SUCCESS;
error_exit:
    return VMI_FAILURE;
}