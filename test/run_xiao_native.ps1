param([string]$Only = '')
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
  if ($Only -and $Only -ne $name) { return }
  $exe = ".pio/feature-tests/$name.exe"
  & $compiler -m ziglang c++ @flags @includes @sources $gtest -o $exe
  if ($LASTEXITCODE -ne 0) { throw "$name compilation failed" }
  & $exe
  if ($LASTEXITCODE -ne 0) { throw "$name tests failed" }
}
Run-Suite 'config_serializer' @('test/test_config_serializer/test_config_serializer.cpp',
  'src/helpers/ConfigSerializer.cpp','src/Utils.cpp','src/Packet.cpp')
Run-Suite 'utils' @('test/test_utils/test_tohex.cpp','src/Utils.cpp','src/Packet.cpp')
Run-Suite 'utf8' @('test/test_utf8_helpers/test_utf8_helpers.cpp')
Run-Suite 'routing_policy' @('test/test_routing_policy/test_routing_policy.cpp','src/Utils.cpp','src/Packet.cpp')
Run-Suite 'mesh_tables' @('test/test_mesh_tables/test_simple_mesh_tables.cpp','src/Utils.cpp','src/Packet.cpp')
Run-Suite 'companion_prefs' @('test/test_companion_node_prefs/test_companion_node_prefs.cpp',
  'src/helpers/ConfigSerializer.cpp','src/Utils.cpp','src/Packet.cpp')
Run-Suite 'xiao_battery' @('test/test_xiao_battery/test_xiao_battery.cpp')
Run-Suite 'environment' @('test/test_environment/test_environment.cpp')
Run-Suite 'xiao_outputs' @('test/test_xiao_outputs/test_xiao_outputs.cpp')
Run-Suite 'xiao_outputs_uart' @('test/test_xiao_outputs/test_xiao_outputs.cpp') @('-DSERIAL_TX=43','-DSERIAL_RX=44')
Run-Suite 'environment_manager' @('test/test_environment_manager/test_environment_manager.cpp',
  'src/helpers/sensors/EnvironmentSensorManager.cpp') @('-Itest/test_environment_manager/mocks',
  '-DENV_INCLUDE_SHT4X=1','-DESP32=1','-DENV_INCLUDE_DHT11=1')

Run-Suite 'xiao_pin_claims' @('test/test_xiao_pin_claims/test_xiao_pin_claims.cpp')

Run-Suite 'battery_cli' @('test/test_battery_cli/test_battery_cli.cpp')

Run-Suite 'companion_battery' @('test/test_companion_battery/test_companion_battery.cpp',
  'src/helpers/ConfigSerializer.cpp','src/Utils.cpp','src/Packet.cpp') @('-DXIAO_WIO_BATTERY_CLI=1')
Run-Suite 'companion_battery_disabled' @('test/test_companion_battery/test_companion_battery.cpp',
  'src/helpers/ConfigSerializer.cpp','src/Utils.cpp','src/Packet.cpp')

Run-Suite 'companion_cli' @('test/test_companion_cli/test_companion_cli.cpp',
  'src/helpers/ConfigSerializer.cpp','src/Utils.cpp','src/Packet.cpp') @('-DXIAO_WIO_BATTERY_CLI=1')

$historySources = @(Get-ChildItem src/helpers/room_history/*.cpp |
  Where-Object { $_.Name -ne 'SpiffsHistoryStorage.cpp' } |
  ForEach-Object { $_.FullName })
Run-Suite 'room_history' (@('test/test_room_history/test_room_history.cpp', 'test/test_room_history/test_usb_bot.cpp', 'test/test_room_history/test_chat_replay.cpp',
  'src/helpers/ConfigSerializer.cpp','src/Utils.cpp','src/Packet.cpp') + $historySources) @('-DXIAO_WIO_ROOM_HISTORY=1','-DXIAO_WIO_WIFI_TIME=1')
Run-Suite 'room_history_disabled' (@('test/test_room_history/test_room_history.cpp', 'test/test_room_history/test_usb_bot.cpp', 'test/test_room_history/test_chat_replay.cpp',
  'src/helpers/ConfigSerializer.cpp','src/Utils.cpp','src/Packet.cpp') + $historySources) @('-DXIAO_WIO_WIFI_TIME=1')
Run-Suite 'room_delivery' (@('test/test_room_delivery/test_room_delivery.cpp',
  'test/test_room_delivery/ProtocolIdentityMocks.cpp', 'src/helpers/BaseChatMesh.cpp',
  'src/helpers/StaticPoolPacketManager.cpp','src/helpers/AdvertDataHelpers.cpp', 'src/helpers/TxtDataHelpers.cpp',
  'src/Mesh.cpp','src/Dispatcher.cpp','src/Packet.cpp','src/Utils.cpp') + $historySources) @('-DXIAO_WIO_ROOM_ACK_BACKPRESSURE=1')

Run-Suite 'wifi_time' @('test/test_wifi_time/test_wifi_time.cpp',
  'src/helpers/ConfigSerializer.cpp','src/Utils.cpp','src/Packet.cpp') @('-DXIAO_WIO_WIFI_TIME=1')
