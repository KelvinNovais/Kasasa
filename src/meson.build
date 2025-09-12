kasasa_sources = [
  'main.c',
  'kasasa-application.c',
  'kasasa-window.c',
  'kasasa-preferences.c',
  'kasasa-content.c',
  'kasasa-screenshot.c',
  'kasasa-screencast.c',
  'kasasa-picture-container.c',
  'routines.c',
]

kasasa_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.7.4'),
  dependency('libportal'),
  dependency('gstreamer-1.0'),
  dependency('gstreamer-app-1.0')
]

kasasa_deps += cc.find_library('m', required : true)

kasasa_sources += gnome.compile_resources('kasasa-resources',
  'kasasa.gresource.xml',
  c_name: 'kasasa'
)

executable('kasasa', kasasa_sources,
  dependencies: kasasa_deps,
       install: true,
)
