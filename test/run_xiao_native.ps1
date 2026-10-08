# Native fallback for this Windows checkout. Run from the repository root.
# Requires the existing .venv Zig package and PlatformIO native googletest cache.
$ErrorActionPreference = 'Stop'
$compiler = '.\.venv\Scripts\python.exe'
$includes = @('-target','x86_64-windows-gnu','-std=c++17','-O0',
  '-Isrc','-Itest/mocks',
  '-I.pio/libdeps/native/googletest/googletest/include',
  '-I.pio/libdeps/native/googletest/googletest')
$gtest = '.pio/libdeps/native/googletest/googletest/src/gtest-all.cc'
New-Item -ItemType Directory -Path .pio/feature-tests -Force | Out-Null
function Run-Suite($name, $sources, $flags = @()) {
  $exe = ".pio/feature-tests/$name.exe"
  & $compiler -m ziglang c++ @flags @includes @sources $gtest -o $exe
  if ($LASTEXITCODE -ne 0) { throw "$name compilation failed" }
  & $exe
  if ($LASTEXITCODE -ne 0) { throw "$name tests failed" }
}
Run-Suite 'config_serializer' @('test/test_config_serializer/test_config_serializer.cpp',
  'src/helpers/ConfigSerializer.cpp','src/Utils.cpp','src/Packet.cpp')
Run-Suite 'xiao_battery' @('test/test_xiao_battery/test_xiao_battery.cpp')
Run-Suite 'environment' @('test/test_environment/test_environment.cpp')
Run-Suite 'xiao_outputs' @('test/test_xiao_outputs/test_xiao_outputs.cpp')
Run-Suite 'xiao_outputs_uart' @('test/test_xiao_outputs/test_xiao_outputs.cpp') @('-DSERIAL_TX=43','-DSERIAL_RX=44')
Run-Suite 'environment_manager' @('test/test_environment_manager/test_environment_manager.cpp',
  'src/helpers/sensors/EnvironmentSensorManager.cpp') @('-Itest/test_environment_manager/mocks',
  '-DENV_INCLUDE_SHT4X=1','-DESP32=1')

Run-Suite 'xiao_pin_claims' @('test/test_xiao_pin_claims/test_xiao_pin_claims.cpp')

Run-Suite 'battery_cli' @('test/test_battery_cli/test_battery_cli.cpp')

Run-Suite 'companion_battery' @('test/test_companion_battery/test_companion_battery.cpp',
  'src/helpers/ConfigSerializer.cpp','src/Utils.cpp','src/Packet.cpp') @('-DXIAO_WIO_BATTERY_CLI=1')
Run-Suite 'companion_battery_disabled' @('test/test_companion_battery/test_companion_battery.cpp',
  'src/helpers/ConfigSerializer.cpp','src/Utils.cpp','src/Packet.cpp')

Run-Suite 'companion_cli' @('test/test_companion_cli/test_companion_cli.cpp',
  'src/helpers/ConfigSerializer.cpp','src/Utils.cpp','src/Packet.cpp') @('-DXIAO_WIO_BATTERY_CLI=1')
