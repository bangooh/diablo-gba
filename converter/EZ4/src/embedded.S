#ifdef _WIN32
    .section .rdata,"dr"
#else
    .section .rodata
#endif
    .balign 16
    .global embedded_package
embedded_package:
    .incbin "build/v095.dgb"
    .balign 4
    .global embedded_package_size
embedded_package_size:
    .long 33554512
#ifndef _WIN32
    .section .note.GNU-stack,"",@progbits
#endif
