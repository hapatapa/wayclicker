wayclicker_resources = gnome.compile_resources(
    'wayclicker-resources',
    join_paths(
        'resources', 'wayclicker.gresource.xml'
    ),
    source_dir: 'resources'
)

sources = [
    'main.c',
    'wayclicker-app.c',
    'config.c',
    'mainwin.c',
    'x11api.c',
    'input.c',
    'waylandinput.c',
    'globalshortcut.c',
    'settings.c',
    'utils.c',
    wayclicker_resources,
]

if wayland_dep.found()
    sources += [wlr_vp_protocol_c, wlr_vp_protocol_h, hs_protocol_c, hs_protocol_h]
endif

executable('wayclicker',
    sources,
    dependencies: deps,
)
