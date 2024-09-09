soslaio_sources = [
  'main.c',
  'soslaio-application.c',
  'soslaio-window.c'
]

soslaio_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.4'),
  dependency('libportal')
]

soslaio_sources += gnome.compile_resources('soslaio-resources',
  'soslaio.gresource.xml',
  c_name: 'soslaio'
)

executable('soslaio', soslaio_sources,
  dependencies: soslaio_deps,
       install: true,
)
