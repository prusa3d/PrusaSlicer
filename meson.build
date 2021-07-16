
project('prusaslicer',
    'c', 'cpp',
    version: '2.4.0-alpha0',
    meson_version: '>=0.50.0',
    default_options: [
        'cpp_std=gnu++17',
    ],
)

project_full_version = '2.4.0-alpha0'

# Used for config.h
config = configuration_data()
# Used for configuration files, unquoted
data_config = configuration_data()

cmake = import('cmake')
i18n  = import('i18n')
cc  = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')


###############################################################################
# Project configuration

config.set('SLIC3R_GUI', get_option('gui'))

config.set('SLIC3R_APP_KEY',     'PrusaSlicer')
config.set('SLIC3R_APP_NAME',    'PrusaSlicer')
config.set('SLIC3R_BUILD_ID',    'PrusaSlicer')
config.set('SLIC3R_VERSION',     meson.project_version())



###############################################################################
# Platform detection and options

os = host_machine.system()
os_windows  = os in [ 'cygwin', 'windows' ]
os_apple    = os in [ 'darwin' ]
os_linux    = os in [ 'freebsd', 'gnu', 'linux', 'netbsd', 'openbsd', 'dragonfly' ]



###############################################################################
# Build options

static_build = get_option('default_library') == 'static'

cpp_args = [
    '-Wno-reorder', '-Wno-non-virtual-dtor',
]

if not static_build
    cpp_args += '-DBOOST_LOG_DYN_LINK'
endif

add_project_arguments(cpp_args, language: 'cpp')


###############################################################################
# Paths

resources_dir = ''

config.set('SLIC3R_FHS_RESOURCES', get_option('prefix') / get_option('datadir') / 'PrusaSlicer')



###############################################################################
# Dependencies
no_dep = dependency('', required: false)

threads = os_linux ? dependency('threads')  : no_dep
dbus    = os_linux ? dependency('dbus-1')   : no_dep
libm    = os_linux ? cc.find_library('m')   : no_dep
libdl   = os_linux ? cc.find_library('dl')  : no_dep

boost_modules = 'atomic;chrono;date_time;filesystem;iostreams;locale;log;regex;system;thread'.split(';')
boost   = dependency('boost', version: '>=1.66.0', modules: boost_modules)
curl    = dependency('libcurl')
tbb     = dependency('TBB', method: 'cmake')
zlib    = dependency('zlib')
eigen   = static_build ? no_dep : dependency('eigen3')
expat   = dependency('expat')
libpng  = dependency('libpng')
opengl  = dependency('opengl')
glew    = dependency('glew')
cereal  = dependency('cereal')
nlopt   = dependency('nlopt', version: '>=1.4.0')
cgal    = dependency('CGAL', method: 'cmake')
# openvdb = dependency('OpenVDB', method: 'cmake', required: false)

openvdb = cc.find_library('openvdb')
openexr = dependency('OpenEXR')

if get_option('sandboxes')
    opencsg = cc.find_library('opencsg', has_headers: 'opencsg.h')
endif


# WxWidgets
if get_option('gui')
    if os_linux and get_option('wx-stable')
        wx_version = '3.0.0'
    else
        wx_version = '3.1.0'
    endif

    wxwidgets = dependency('wxwidgets', version: '>=@0@'.format(wx_version),
        modules: [ 'base', 'core', 'adv', 'html', 'gl', ],
    )

    add_project_arguments(
        '-DwxUSE_UNICODE',
        '-D_UNICODE',
        '-DUNICODE',
        '-DWXINTL_NO_GETTEXT_MACRO',
        language: 'cpp',
    )

    if os_linux
        gtk = dependency('gtk+-@0@.0'.format(get_option('wx-gtk')))
        wxwidgets = [ wxwidgets, gtk ]
    endif



    osx_diskarbitration = os_apple ? cc.find_library('DiskArbitration') : no_dep


endif


###############################################################################
#


###############################################################################
#

include_directories = include_directories('src', 'src/platform', 'src/clipper')

subdir('src')
subdir('resources')


# Perl bindings, currently only used for the unit / integration tests of libslic3r.
# Also runs the unit / integration tests.
#FIXME Port the tests into C++ to finally get rid of the Perl!
if get_option('perl-xs')
    subdir('xs')
endif

if get_option('sandboxes')
    subdir('sandboxes')
endif

if get_option('tests')
    subdir('tests')
endif
