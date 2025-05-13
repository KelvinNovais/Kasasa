kasasa_sources = [
  'main.c',
  'kasasa-application.c',
  'kasasa-window.c',
  'kasasa-preferences.c',
  'kasasa-screenshot.c',
  'take-first-screenshot.c'
]

kasasa_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.4'),
  dependency('libportal')
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
