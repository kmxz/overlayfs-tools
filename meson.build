project('overlayfs-tools', 'c',
    default_options : ['c_std=gnu11'],
    version : '2026.11')

# Source files for executables
overlay_src = ['main.c', 'logic.c', 'sh.c', 'common.c']
fsck_src = ['fsck.c', 'common.c', 'lib.c', 'check.c', 'mount.c', 'path.c', 'overlayfs.c']

# Dependencies for executables
# We have to link against fts on musl based systems, but this is a soft dependency
# If it's not found on the glibc systems, it's just ignored
musl_fts = dependency('musl-fts', method : 'pkg-config', required : false)
fsck_dep = meson.get_compiler('c').find_library('m', required : false)

# Executables
overlay = executable('overlay', overlay_src,
    install : true,
    c_args : '-DOVERLAYFS_TOOLS_VERSION="@0@"'.format(meson.project_version()),
    dependencies : musl_fts)
executable('fsck.overlay', fsck_src,
    install : true,
    c_args : '-DOVERLAYFS_TOOLS_VERSION="@0@"'.format(meson.project_version()),
    dependencies : [fsck_dep, musl_fts])

if get_option('tests')
# The tests need root, e.g. "sudo meson test -C build --no-rebuild".
# Without root, the test script exits with 77 and meson reports it as skipped.
find_program('setfattr')
test('run-tests',
    find_program('test_cases/run-tests.sh'),
    args : [overlay],
    timeout : 120)
endif
