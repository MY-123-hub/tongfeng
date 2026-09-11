param(
    [string]$Gcc = "C:\mingw64\mingw64\bin\gcc.exe"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$appDir = Join-Path $repoRoot "主机_TD710\App"
$bspDir = Join-Path $repoRoot "主机_TD710\Bsp"
$slaveAppDir = Join-Path $repoRoot "从机\App"
$controlBspDir = Join-Path $repoRoot "控制室\控制室\Bsp"
$fakeDir = Join-Path $PSScriptRoot "fakes"
$outputDir = Join-Path $env:TEMP "lora_master_host_tests"

$useGcc = Test-Path -LiteralPath $Gcc
$cl = $null

if (-not $useGcc) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vsInstall = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($vsInstall) {
            $cl = Get-ChildItem -LiteralPath (Join-Path $vsInstall "VC\Tools\MSVC") `
                -Filter "cl.exe" -File -Recurse -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -like "*\bin\Hostx64\x64\cl.exe" } |
                Select-Object -Last 1
            $vcvars = Join-Path $vsInstall "VC\Auxiliary\Build\vcvars64.bat"
            if (($null -ne $cl) -and (Test-Path -LiteralPath $vcvars)) {
                & cmd.exe /d /c "call `"$vcvars`" >nul && set" |
                    ForEach-Object {
                        if ($_ -match '^([^=]+)=(.*)$') {
                            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
                        }
                    }
            }
            else {
                $cl = $null
            }
        }
    }
}

if ((-not $useGcc) -and ($null -eq $cl)) {
    throw "Neither GCC nor a Visual Studio C compiler was found."
}

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$tests = @(
    @{
        Name = "slave_protocol_runtime"
        Includes = @($fakeDir)
        Sources = @(
            (Join-Path $slaveAppDir "slave_protocol_runtime.c"),
            (Join-Path $PSScriptRoot "test_slave_protocol_runtime.c")
        )
    },
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
        Name = "control_room_gateway_runtime"
        Includes = @($controlBspDir)
        Sources = @(
            (Join-Path $controlBspDir "gateway_runtime.c"),
            (Join-Path $repoRoot "tests\test_control_room_gateway_runtime.c")
        )
    },
    @{
        Name = "unified_temperature_chain"
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
            (Join-Path $slaveAppDir "slave_protocol_runtime.c"),
            (Join-Path $controlBspDir "gateway_runtime.c"),
            (Join-Path $PSScriptRoot "test_unified_temperature_chain.c")
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
        if ($useGcc) {
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
                "-Wshadow"
            ) + $extraIncludes + @(
                "-I", $appDir,
                "-I", $bspDir,
                "-I", $slaveAppDir
            ) + $test.Sources + @("-o", $exePath)
            & $Gcc @arguments
        }
        else {
            $msvcOptimization = if ($optimization -eq "O0") { "/Od" } else { "/O2" }
            $arguments = @(
                "/nologo",
                "/utf-8",
                "/std:c11",
                $msvcOptimization,
                "/W4",
                "/WX",
                "/wd4127",
                ("/Fo" + $outputDir + "\\")
            )
            foreach ($includeDir in $test.Includes) {
                $arguments += "/I$includeDir"
            }
            $arguments += @("/I$appDir", "/I$bspDir", "/I$slaveAppDir")
            $arguments += $test.Sources
            $arguments += "/Fe$exePath"
            & $cl.FullName @arguments
        }
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
