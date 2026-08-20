param(
    [string]$Gcc = "C:\mingw64\mingw64\bin\gcc.exe"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$appDir = Join-Path $repoRoot "LoraMaster_TD710\App"
$bspDir = Join-Path $repoRoot "LoraMaster_TD710\Bsp"
$fakeDir = Join-Path $PSScriptRoot "fakes"
$outputDir = Join-Path $env:TEMP "lora_master_host_tests"

if (-not (Test-Path -LiteralPath $Gcc)) {
    throw "GCC not found: $Gcc"
}

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$tests = @(
    @{
        Name = "lora_protocol"
        Sources = @(
            (Join-Path $appDir "lora_protocol.c"),
            (Join-Path $PSScriptRoot "test_lora_protocol.c")
        )
    },
    @{
        Name = "lora_stream_parser"
        Sources = @(
            (Join-Path $appDir "lora_protocol.c"),
            (Join-Path $appDir "lora_stream_parser.c"),
            (Join-Path $PSScriptRoot "test_lora_stream_parser.c")
        )
    },
    @{
        Name = "lora_rx_ring"
        Sources = @(
            (Join-Path $appDir "lora_rx_ring.c"),
            (Join-Path $PSScriptRoot "test_lora_rx_ring.c")
        )
    },
    @{
        Name = "lora_transport"
        Sources = @(
            (Join-Path $appDir "lora_rx_ring.c"),
            (Join-Path $bspDir "lora_transport.c"),
            (Join-Path $PSScriptRoot "test_lora_transport.c")
        )
    },
    @{
        Name = "lora_rx_pipeline"
        Sources = @(
            (Join-Path $appDir "lora_protocol.c"),
            (Join-Path $appDir "lora_stream_parser.c"),
            (Join-Path $appDir "lora_rx_ring.c"),
            (Join-Path $bspDir "lora_transport.c"),
            (Join-Path $PSScriptRoot "test_lora_rx_pipeline.c")
        )
    },
    @{
        Name = "master_queues"
        Includes = @($fakeDir)
        Sources = @(
            (Join-Path $fakeDir "fake_queue.c"),
            (Join-Path $appDir "master_queues.c"),
            (Join-Path $PSScriptRoot "test_master_queues.c")
        )
    },
    @{
        Name = "master_temperature"
        Sources = @(
            (Join-Path $appDir "master_identity.c"),
            (Join-Path $appDir "master_ingress.c"),
            (Join-Path $appDir "master_temperature.c"),
            (Join-Path $PSScriptRoot "test_master_temperature.c")
        )
    },
    @{
        Name = "master_runtime"
        Includes = @($fakeDir)
        Sources = @(
            (Join-Path $fakeDir "fake_queue.c"),
            (Join-Path $fakeDir "fake_parameter_store.c"),
            (Join-Path $appDir "lora_protocol.c"),
            (Join-Path $appDir "auto_control.c"),
            (Join-Path $appDir "command_service.c"),
            (Join-Path $appDir "master_identity.c"),
            (Join-Path $appDir "master_ingress.c"),
            (Join-Path $appDir "master_temperature.c"),
            (Join-Path $appDir "master_queues.c"),
            (Join-Path $appDir "master_runtime.c"),
            (Join-Path $PSScriptRoot "test_master_runtime.c")
        )
    },
    @{
        Name = "master_commands"
        Includes = @($fakeDir)
        Sources = @(
            (Join-Path $fakeDir "fake_queue.c"),
            (Join-Path $fakeDir "fake_parameter_store.c"),
            (Join-Path $appDir "lora_protocol.c"),
            (Join-Path $appDir "auto_control.c"),
            (Join-Path $appDir "command_service.c"),
            (Join-Path $appDir "master_identity.c"),
            (Join-Path $appDir "master_ingress.c"),
            (Join-Path $appDir "master_temperature.c"),
            (Join-Path $appDir "master_queues.c"),
            (Join-Path $appDir "master_runtime.c"),
            (Join-Path $PSScriptRoot "test_master_commands.c")
        )
    },
    @{
        Name = "command_service"
        Sources = @(
            (Join-Path $appDir "command_service.c"),
            (Join-Path $PSScriptRoot "test_command_service.c")
        )
    },
    @{
        Name = "vfd_modbus_codec"
        Sources = @(
            (Join-Path $appDir "vfd_modbus_codec.c"),
            (Join-Path $PSScriptRoot "test_vfd_modbus_codec.c")
        )
    },
    @{
        Name = "modbus_async"
        Includes = @($fakeDir)
        Sources = @(
            (Join-Path $appDir "vfd_modbus_codec.c"),
            (Join-Path $bspDir "modbus_async.c"),
            (Join-Path $PSScriptRoot "test_modbus_async.c")
        )
    },
    @{
        Name = "auto_control"
        Sources = @(
            (Join-Path $appDir "auto_control.c"),
            (Join-Path $PSScriptRoot "test_auto_control.c")
        )
    },
    @{
        Name = "parameter_record"
        Sources = @(
            (Join-Path $appDir "parameter_record.c"),
            (Join-Path $PSScriptRoot "test_parameter_record.c")
        )
    }
)

foreach ($optimization in @("O0", "O2")) {
    foreach ($test in $tests) {
        $exePath = Join-Path $outputDir ($test.Name + "_" + $optimization + ".exe")
        $extraIncludes = @()
        if ($test.ContainsKey("Includes")) {
            foreach ($includeDir in $test.Includes) {
                $extraIncludes += @("-I", $includeDir)
            }
        }
        $arguments = @(
            "-fuse-ld=bfd",
            "-std=c11",
            ("-" + $optimization),
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wpedantic",
            "-Wconversion",
            "-Wsign-conversion",
            "-Wshadow",
            "-I", $appDir,
            "-I", $bspDir
        ) + $extraIncludes + $test.Sources + @("-o", $exePath)

        & $Gcc @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Compile failed: $($test.Name) $optimization"
        }

        & $exePath
        if ($LASTEXITCODE -ne 0) {
            throw "Test failed: $($test.Name) $optimization"
        }
    }
}

Write-Host "ALL HOST TESTS PASSED"
