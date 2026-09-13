project(
    'WayClicker',
    'c',
    license: 'GPL-3.0',
    default_options: [
        'warning_level=2'
    ],
)

add_project_arguments(
    '-Wno-incompatible-pointer-types',
    language : 'c'
)


gnome = import('gnome')
deps = [dependency('gtk+-3.0'), dependency('x11'), dependency('xi'), dependency('xtst')]

# Native Wayland input simulation through the wlr-virtual-pointer protocol.
wayland_dep = dependency('wayland-client', required: false)
if wayland_dep.found()
    deps += wayland_dep
    add_project_arguments('-DHAVE_WAYLAND', language: 'c')

    wayland_scanner = find_program('wayland-scanner')
    wlr_vp_xml = files('src/wlr-virtual-pointer-unstable-v1.xml')

    wlr_vp_protocol_c = custom_target('wlr-vp-protocol-c',
        output: 'wlr-virtual-pointer-unstable-v1-protocol.c',
        input: wlr_vp_xml,
        command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

    wlr_vp_protocol_h = custom_target('wlr-vp-protocol-h',
        output: 'wlr-virtual-pointer-unstable-v1-client-protocol.h',
        input: wlr_vp_xml,
        command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])

    # Global hotkeys through the Hyprland native global-shortcuts protocol.
    hs_xml = files('src/hyprland-global-shortcuts-v1.xml')

    hs_protocol_c = custom_target('hs-protocol-c',
        output: 'hyprland-global-shortcuts-v1-protocol.c',
        input: hs_xml,
        command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])

    hs_protocol_h = custom_target('hs-protocol-h',
        output: 'hyprland-global-shortcuts-v1-client-protocol.h',
        input: hs_xml,
        command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
endif

subdir('src')
