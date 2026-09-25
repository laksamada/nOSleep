#include "header/cpu/gdt.h"

struct GlobalDescriptorTable global_descriptor_table = {
    .table = {
        {
            // [0] Null descriptor: semua field 0
        },
        {
            // [1] Kernel code: isi dari tabel
            .segment_low = 0xFFFF, 
            .base_low = 0x0000,   
            .base_mid = 0x00,     
            .type_bit = 0xA,     
            .non_system = 1,      
            .dpl = 0,            
            .present = 1,         
            .segment_high = 0xF,  
            .available = 0,       
            .long_mode = 0,       
            .default_op_size = 1, 
            .granularity = 1,     
            .base_high = 0x00     
            // ... field lainnya
        },
        {
            // [2] Kernel data
            .segment_low = 0xFFFF,
            .base_low = 0x0000,   
            .base_mid = 0x00,     
            .type_bit = 0x2,       
            .non_system = 1,     
            .dpl = 0,            
            .present = 1,         
            .segment_high = 0xF,  
            .available = 0,       
            .long_mode = 0,       
            .default_op_size = 1,  
            .granularity = 1,     
            .base_high = 0x00     
        }
    }
};

struct GDTR _gdt_gdtr = {
    // TODO: size & address
    .size = sizeof(global_descriptor_table) - 1,
    .address = &global_descriptor_table

};