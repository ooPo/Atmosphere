#include <switch.h>
#include <algorithm>
#include <cstdio>
#include "ldr_npdm.hpp"
#include "ldr_registration.hpp"

static NpdmUtils::NpdmCache g_npdm_cache = {0};

Result NpdmUtils::LoadNpdmFromCache(u64 tid, NpdmInfo *out) {
    if (g_npdm_cache.info.title_id != tid) {
        return LoadNpdm(tid, out);
    }
    *out = g_npdm_cache.info;
    return 0;
}

Result NpdmUtils::LoadNpdm(u64 tid, NpdmInfo *out) {
    Result rc;
    
    g_npdm_cache.info = (const NpdmUtils::NpdmInfo){0};
    
    FILE *f_npdm = fopen("code:/main.npdm", "rb");
    if (f_npdm == NULL) {
        /* For generic "Couldn't open the file" error, just say the file doesn't exist. */
        return 0x202;
    }
    
    fseek(f_npdm, 0, SEEK_END);
    size_t npdm_size = ftell(f_npdm);
    fseek(f_npdm, 0, SEEK_SET);
    
    if (npdm_size > sizeof(g_npdm_cache.buffer) || fread(g_npdm_cache.buffer, 1, npdm_size, f_npdm) != npdm_size) {
        return 0x609;
    }
    
    fclose(f_npdm);
    
    rc = 0x809;
    if (npdm_size < sizeof(NpdmUtils::NpdmHeader)) {
        return rc;
    }
    
    /* For ease of access... */
    g_npdm_cache.info.header = (NpdmUtils::NpdmHeader *)(g_npdm_cache.buffer);
    NpdmInfo *info = &g_npdm_cache.info;
    
    if (info->header->magic != MAGIC_META) {
        return rc;
    }
    
    if (info->header->mmu_flags > 0xF) {
        return rc;
    }
    
    if (info->header->aci0_offset < sizeof(NpdmUtils::NpdmHeader) || info->header->aci0_size < sizeof(NpdmUtils::NpdmAci0) || info->header->aci0_offset + info->header->aci0_size > npdm_size) {
        return rc;
    }
    
    info->aci0 = (NpdmAci0 *)(g_npdm_cache.buffer + info->header->aci0_offset);
    
    if (info->aci0->magic != MAGIC_ACI0) {
        return rc;
    }
    
    if (info->aci0->fah_size > info->header->aci0_size || info->aci0->fah_offset < sizeof(NpdmUtils::NpdmAci0) || info->aci0->fah_offset + info->aci0->fah_size > info->header->aci0_size) {
        return rc;
    }
    
    info->aci0_fah = (void *)((uintptr_t)info->aci0 + info->aci0->fah_offset);
    
    if (info->aci0->sac_size > info->header->aci0_size || info->aci0->sac_offset < sizeof(NpdmUtils::NpdmAci0) || info->aci0->sac_offset + info->aci0->sac_size > info->header->aci0_size) {
        return rc;
    }
    
    info->aci0_sac = (void *)((uintptr_t)info->aci0 + info->aci0->sac_offset);
    
    if (info->aci0->kac_size > info->header->aci0_size || info->aci0->kac_offset < sizeof(NpdmUtils::NpdmAci0) || info->aci0->kac_offset + info->aci0->kac_size > info->header->aci0_size) {
        return rc;
    }
    
    info->aci0_kac = (void *)((uintptr_t)info->aci0 + info->aci0->kac_offset);
    
    if (info->header->acid_offset < sizeof(NpdmUtils::NpdmHeader) || info->header->acid_size < sizeof(NpdmUtils::NpdmAcid) || info->header->acid_offset + info->header->acid_size > npdm_size) {
        return rc;
    }
    
    info->acid = (NpdmAcid *)(g_npdm_cache.buffer + info->header->acid_offset);
    
    if (info->acid->magic != MAGIC_ACID) {
        return rc;
    }
    
    /* TODO: Check if retail flag is set if not development hardware. */
    
    if (info->acid->fac_size > info->header->acid_size || info->acid->fac_offset < sizeof(NpdmUtils::NpdmAcid) || info->acid->fac_offset + info->acid->fac_size > info->header->acid_size) {
        return rc;
    }
    
    info->acid_fac = (void *)((uintptr_t)info->acid + info->acid->fac_offset);
    
    if (info->acid->sac_size > info->header->acid_size || info->acid->sac_offset < sizeof(NpdmUtils::NpdmAcid) || info->acid->sac_offset + info->acid->sac_size > info->header->acid_size) {
        return rc;
    }
    
    info->acid_sac = (void *)((uintptr_t)info->acid + info->acid->sac_offset);
    
    if (info->acid->kac_size > info->header->acid_size || info->acid->kac_offset < sizeof(NpdmUtils::NpdmAcid) || info->acid->kac_offset + info->acid->kac_size > info->header->acid_size) {
        return rc;
    }
    
    info->acid_kac = (void *)((uintptr_t)info->acid + info->acid->kac_offset);
    
    
    /* We validated! */
    info->title_id = tid;
    *out = *info;
    rc = 0;
    
    return rc;
}

Result NpdmUtils::ValidateCapabilityAgainstRestrictions(u32 *restrict_caps, size_t num_restrict_caps, u32 *&cur_cap, size_t &caps_remaining) {
    Result rc = 0;
    u32 desc = *cur_cap++;
    caps_remaining--;
    unsigned int low_bits = 0;
    while (desc & 1) {
        desc >>= 1;
        low_bits++;
    }
    desc >>= 1;
    u32 r_desc = 0;
    switch (low_bits) {
        case 3: /* Kernel flags. */
            rc = 0xCE09;
            for (size_t i = 0; i < num_restrict_caps; i++) {
                if ((restrict_caps[i] & 0xF) == 0x7) {
                    r_desc = restrict_caps[i] >> 4;
                    u32 highest_thread_prio = desc & 0x3F;
                    u32 r_highest_thread_prio = r_desc & 0x3F;
                    desc >>= 6;
                    r_desc >>= 6;
                    u32 lowest_thread_prio = desc & 0x3F;
                    u32 r_lowest_thread_prio = r_desc & 0x3F;
                    desc >>= 6;
                    r_desc >>= 6;
                    u32 lowest_cpu_id = desc & 0xFF;
                    u32 r_lowest_cpu_id = r_desc & 0xFF;
                    desc >>= 8;
                    r_desc >>= 8;
                    u32 highest_cpu_id = desc & 0xFF;
                    u32 r_highest_cpu_id = r_desc & 0xFF;
                    if (highest_thread_prio > r_highest_thread_prio) {
                       break;
                    }
                    if (lowest_thread_prio > highest_thread_prio) {
                       break;
                    }
                    if (lowest_thread_prio < r_lowest_thread_prio) {
                       break;
                    }
                    if (lowest_cpu_id < r_lowest_cpu_id) {
                       break;
                    }
                    if (lowest_cpu_id > r_highest_cpu_id) {
                       break;
                    }
                    if (highest_cpu_id > r_highest_cpu_id) {
                       break;
                    }       
                    /* Valid! */
                    rc = 0;
                    break;
                }
            }
            break;
        case 4: /* Syscall mask. */
            rc = 0xD009;
            for (size_t i = 0; i < num_restrict_caps; i++) {
                if ((restrict_caps[i] & 0x1F) == 0xF) {
                    r_desc = restrict_caps[i] >> 5;              
                    u32 syscall_base = (desc >> 24);
                    u32 r_syscall_base = (r_desc >> 24);
                    if (syscall_base != r_syscall_base) {
                        continue;
                    }
                    u32 syscall_mask = desc & 0xFFFFFF;
                    u32 r_syscall_mask = r_desc & 0xFFFFFF;
                    if ((r_syscall_mask & syscall_mask) != syscall_mask) {
                        break;
                    }
                    /* Valid! */
                    rc = 0;
                    break;
                }
            }
            break;
        case 6: /* Map IO/Normal. */ {
                rc = 0xD409;
                if (caps_remaining == 0) {
                    break;
                }
                u32 next_cap = *cur_cap++;
                caps_remaining--;
                if ((next_cap & 0x7F) != 0x3F) {
                    break;
                }
                u32 next_desc = next_cap >> 7;
                u32 base_addr = desc & 0xFFFFFF;
                u32 base_size = next_desc & 0xFFFFFF;
                /* Size check the mapping. */
                if (base_size >> 20) {
                    break;
                }
                u32 base_end = base_addr + base_size;
                /* Validate it's possible to validate this mapping. */
                if (num_restrict_caps < 2) {
                    break;
                }
                for (size_t i = 0; i < num_restrict_caps - 1; i++) {
                    if ((restrict_caps[i] & 0x7F) == 0x3F) {
                        r_desc = restrict_caps[i] >> 7;
                        if  ((restrict_caps[i+1] & 0x7F) != 0x3F) {
                            break;
                        }
                        u32 r_next_desc = restrict_caps[i++] >> 7;
                        u32 r_base_addr = r_desc & 0xFFFFFF;
                        u32 r_base_size = r_next_desc & 0xFFFFFF;
                        /* Size check the mapping. */
                        if (r_base_size >> 20) {
                            break;
                        }
                        u32 r_base_end = r_base_addr + r_base_size;
                        /* Validate is_io matches. */
                        if (((r_desc >> 24) & 1) ^ ((desc >> 24) & 1)) {
                            continue;
                        }
                        /* Validate is_ro matches. */
                        if (((r_next_desc >> 24) & 1) ^ ((next_desc >> 24) & 1)) {
                            continue;
                        }
                        /* Validate bounds. */
                        if (base_addr < r_base_addr || base_end > r_base_end) {
                            continue;
                        }
                        /* Valid! */
                        rc = 0;
                        break;
                    }
                }
            }
            break;
        case 7: /* Map Normal Page. */
            rc = 0xD609;
            for (size_t i = 0; i < num_restrict_caps; i++) {
                if ((restrict_caps[i] & 0xFF) == 0x7F) {
                    r_desc = restrict_caps[i] >> 8;              
                    if (r_desc != desc) {
                        continue;
                    }
                    /* Valid! */
                    rc = 0;
                    break;
                }
            }
            break;
        case 11: /* IRQ Pair. */ 
            rc = 0x0;
            for (unsigned int irq_i = 0; irq_i < 2; irq_i++) {
                u32 irq = desc & 0x3FF;
                desc >>= 10;
                if (irq != 0x3FF) {
                    bool found = false;
                    for (size_t i = 0; i < num_restrict_caps && !found; i++) {
                        if ((restrict_caps[i] & 0xFFF) == 0x7FF) {
                            r_desc = restrict_caps[i] >> 12;
                            u32 r_irq_0 = r_desc & 0x3FF;
                            r_desc >>= 10;
                            u32 r_irq_1 = r_desc & 0x3FF;
                            found |= irq == r_irq_0 || irq == r_irq_1;
                            found |= r_irq_0 == 0x3FF && r_irq_1 == 0x3FF;
                        }
                    }
                    if (!found) {
                        rc = 0xDE09;
                        break;
                    }
                }
            }
            break;
        case 13: /* App Type. */
            rc = 0xE209;
            if (num_restrict_caps) {
                for (size_t i = 0; i < num_restrict_caps; i++) {
                    if ((restrict_caps[i] & 0x3FFF) == 0x1FFF) {
                        r_desc = restrict_caps[i] >> 14;              
                        break;
                    }
                }
            } else {
                r_desc = 0;
            }
            if (desc == r_desc) {
                /* Valid! */
                rc = 0;
            }
            break;
        case 14: /* Kernel Release Version. */
            rc = 0xE409;
            if (num_restrict_caps) {
                for (size_t i = 0; i < num_restrict_caps; i++) {
                    if ((restrict_caps[i] & 0x7FFF) == 0x3FFF) {
                        r_desc = restrict_caps[i] >> 15;              
                        break;
                    }
                }
            } else {
                r_desc = 0;
            }
            if (desc == r_desc) {
                /* Valid! */
                rc = 0;
            }
            break;
        case 15: /* Handle Table Size. */
            rc = 0xE609;
            for (size_t i = 0; i < num_restrict_caps; i++) {
                if ((restrict_caps[i] & 0xFFFF) == 0x7FFF) {
                    r_desc = restrict_caps[i] >> 16;    
                    desc &= 0x3FF;
                    r_desc &= 0x3FF;
                    if (desc <= r_desc) {
                        break;
                    }
                    /* Valid! */
                    rc = 0;
                    break;
                }
            }
            break;
        case 16: /* Debug Flags. */
            rc = 0xE809;
            if (num_restrict_caps) {
                for (size_t i = 0; i < num_restrict_caps; i++) {
                    if ((restrict_caps[i] & 0x1FFFF) == 0xFFFF) {
                        r_desc = restrict_caps[i] >> 17;              
                        break;
                    }
                }
            } else {
                r_desc = 0;
            }
            if ((desc & ~r_desc) == 0) {
                /* Valid! */
                rc = 0;
            }
            break;
        case 32: /* Empty Descriptor. */
            rc = 0;
            break;
        default: /* Unrecognized Descriptor. */
            rc = 0xC809;
            break;
    }
    return rc;
}

Result NpdmUtils::ValidateCapabilities(u32 *acid_caps, size_t num_acid_caps, u32 *aci0_caps, size_t num_aci0_caps) {
    Result rc = 0;
    size_t remaining = num_aci0_caps;
    u32 *cur_cap = aci0_caps;
    while (remaining) {
        if (R_FAILED((rc = ValidateCapabilityAgainstRestrictions(acid_caps, num_acid_caps, cur_cap, remaining)))) {
            break;
        }
    }

    return rc;
}

u32 NpdmUtils::GetApplicationType(u32 *caps, size_t num_caps) {
    u32 application_type = 0;
    for (unsigned int i = 0; i < num_caps; i++) {
        if ((caps[i] & 0x3FFF) == 0x1FFF) {
            u16 app_type = (caps[i] >> 14) & 7;
            if (app_type == 1) {
                application_type |= 1;
            } else if (app_type == 2) {
                application_type |= 2;
            }
        }
        /* After 1.0.0, allow_debug is used as bit 4. */
        if (kernelAbove200() && (caps[i] & 0x1FFFF) == 0xFFFF) {
            application_type |= (caps[i] >> 15) & 4;
        }
    }
    return application_type;
}