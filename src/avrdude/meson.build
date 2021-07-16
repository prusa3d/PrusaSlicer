avrdude_sources = [
    'arduino.c',
    'avr.c',
    # 'avrftdi.c',
    # 'avrftdi_tpi.c',
    'avrpart.c',
    'avr910.c',
    'bitbang.c',
    'buspirate.c',
    'butterfly.c',
    'config.c',
    'config_gram.c',
    # 'confwin.c',
    'crc16.c',
    # 'dfu.c',
    'fileio.c',
    # 'flip1.c',
    # 'flip2.c',
    # 'ft245r.c',
    # 'jtagmkI.c',
    # 'jtagmkII.c',
    # 'jtag3.c',
    'lexer.c',
    'linuxgpio.c',
    'lists.c',
    # 'par.c',
    'pgm.c',
    'pgm_type.c',
    'pickit2.c',
    'pindefs.c',
    # 'ppi.c',
    # 'ppiwin.c',
    'safemode.c',
    'ser_avrdoper.c',
    'serbb_posix.c',
    'serbb_win32.c',
    'ser_posix.c',
    'ser_win32.c',
    'stk500.c',
    'stk500generic.c',
    'stk500v2.c',
    'term.c',
    'update.c',
    # 'usbasp.c',
    # 'usb_hidapi.c',
    # 'usb_libusb.c',
    # 'usbtiny.c',
    'wiring.c',

    'main.c',
    'avrdude-slic3r.hpp',
    'avrdude-slic3r.cpp',
]

if os_windows
    avrdude_sources += [
        'windows/utf8.c',
        'windows/unistd.cpp',
        'windows/getopt.c',
    ]
endif


avrdude = static_library('avrdude',
    avrdude_sources,
)

avrdude_slic3r = executable('avrdude-slic3r',
    'main-standalone.cpp',
    link_with: avrdude,

)
