project('overlayfs-tools', 'c',
    default_options : ['c_std=gnu11'],
    version : '2025.01')

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

# Custom targets for testing overlay functionality
overlayed_tar = 'test_cases/overlayed.tar'
lower_tar = 'test_cases/lower.tar'
upper_tar = 'test_cases/upper.tar'

overlayed = custom_target('overlayed',
    input : overlayed_tar,
    output : 'overlayed',
    command : [
        'sh', '-c', 'tar xf @INPUT@ && sudo cp -a permanent/removed_tests/removed_dir overlayed/renamed_dir'
    ]
)

permanent = custom_target('permanent',
    input : lower_tar,
    output : 'permanent',
    command : ['sh', '-c', 'tar xf @INPUT@']
)

changes = custom_target('changes',
    input : upper_tar,
    output : 'changes',
    command : [
        'sh', '-c',
        'tar xf @INPUT@ --strip-components=2 && ' +
        'setfattr -n trusted.overlay.opaque -v y changes/removed_tests/replaced_by_opaque && ' +
        'setfattr -n trusted.overlay.opaque -v y changes/normal_merged_dir/more_test/by_opaque && ' +
        'setfattr -n trusted.overlay.metacopy -v y changes/permission/file && ' +
        'truncate -s 0 changes/permission/file && ' +
        'truncate -s 95 changes/permission/file && ' +
        'mkdir changes/renamed_dir && ' +
        'setfattr -n trusted.overlay.redirect -v "/removed_tests/removed_dir" changes/renamed_dir'
    ]
)

brief_expected = custom_target('brief.expected',
    output : 'brief.expected',
    command : [
        'sh', '-c',
        'diff -qr --no-dereference $(pwd)/permanent $(pwd)/overlayed 2>/dev/null | sed \'s/overlayed/changes/g\' | sort -u > @OUTPUT@'
    ]
)

diff_out = custom_target('diff.out',
    output : 'diff.out',
    command : [
        'sh', '-c',
        'sudo ' + overlay.full_path() + ' -l permanent -u changes diff | sort -u > @OUTPUT@'
    ]
)

verbose_out = custom_target('verbose.out',
    output : 'verbose.out',
    command : [
        'sh', '-c',
        'sudo ' + overlay.full_path() + ' -l permanent -u changes diff -v | sort -u > @OUTPUT@'
    ]
)

brief_out = custom_target('brief.out',
    output : 'brief.out',
    command : [
        'sh', '-c',
        'sudo ' + overlay.full_path() + ' -l permanent -u changes diff -b | sort -u > @OUTPUT@'
    ]
)

test('run_tests', find_program('test_cases/run_tests.py'))

custom_target('clean.tests',
    output : 'clean.tests',
    command : ['sudo', 'rm', '-rf', 'permanent', 'changes', 'overlayed', 'brief.expected', 'brief.out', 'diff.out', 'verbose.out']
)
