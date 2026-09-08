project('mebuki', 'c',
  version : '0.1.0',
  meson_version : '>=1.10.0',
  default_options : ['c_std=c23', 'buildtype=minsize']
)

objcopy = find_program('objcopy')
objdump = find_program('objdump')
rfp_cli = find_program('rfp-cli')
python_exe = find_program('python3', 'python', required: true)

serial_port_option = get_option('serial_port')

app_common_sources = files(
  'src/uart.c',
  'src/system.c',
  'src/test_code_flash.c',
  'src/test_data_flash.c',
)

app_sources = files(
  'src/main.c',
  'src/startup.c',
)
app_includes = include_directories('src')

vendor_sources = files(
  'vendor/flash_type4.c',
)
vendor_includes = include_directories('vendor')
machine_linker = files('src/flash.ld')

app_exe = executable(
  'app',
  [app_sources, app_common_sources, vendor_sources],
  include_directories: [app_includes, vendor_includes],
  link_args: [
    '-nostdlib',
    '-T' + machine_linker[0].full_path(),
    '-Wl,--print-map-locals,-Map=' + meson.current_build_dir() / 'app.map',
  ],
  link_depends: machine_linker,
  name_suffix: 'elf',
)

app_hex = custom_target(
  'app_hex',
  input: app_exe,
  output: app_exe.name() + '.s32',
  command: [objcopy, '-O', 'srec', '@INPUT@', '@OUTPUT@'],
  build_by_default: true,
)

app_txt = custom_target(
  'app_txt',
  input: app_exe,
  output: app_exe.name() + '.txt',
  command: [objdump, '-d', '-S', '-l', '@INPUT@'],
  capture : true,
  build_by_default: true,
)

run_target(
  'deploy',
  command: [
    rfp_cli,
    '-d', 'RX65x',
    '-t', 'e2l',
    '-if', 'fine',
    '-auth', 'id', 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF',
    '-a',
    app_hex,
  ]
)

if serial_port_option == ''
  error('serial_port option must be specified. Use -Dserial_port=<port>')
endif

run_script = meson.current_source_dir() / 'scripts' / 'run.py'

run_target(
  'run',
  command: [
    python_exe,
    run_script,
    serial_port_option,
    '--build-dir', meson.current_build_dir(),
    '--rfp-cli', rfp_cli,
    '--display',
  ]
)

run_target(
  'run-capture',
  command: [
    python_exe,
    run_script,
    serial_port_option,
    '--build-dir', meson.current_build_dir(),
    '--rfp-cli', rfp_cli,
  ]
)
