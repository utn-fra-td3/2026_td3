$ErrorActionPreference = 'Stop'

$firmwareDirectory = Split-Path -Parent $PSScriptRoot
$workspaceDirectory = Split-Path -Parent $firmwareDirectory
$firmwareSource = Get-Content -Raw -Encoding UTF8 (Join-Path $firmwareDirectory 'firmware.c')
$firmwareCMake = Get-Content -Raw -Encoding UTF8 (Join-Path $firmwareDirectory 'CMakeLists.txt')
$settingsSource = Get-Content -Raw -Encoding UTF8 (Join-Path $firmwareDirectory 'settings_storage.c')
$freeRtosConfig = Get-Content -Raw -Encoding UTF8 (Join-Path $workspaceDirectory 'freertos\FreeRTOSConfig.h')

function Get-IntegerMacro {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $match = [regex]::Match(
        $Source,
        "(?m)^#define\s+$([regex]::Escape($Name))\s+(\d+)(?:U)?\s*$"
    )
    if (-not $match.Success) {
        throw "No se encontro la macro entera $Name"
    }
    return [int]$match.Groups[1].Value
}

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

$taskMatches = [regex]::Matches(
    $firmwareSource,
    'xTaskCreate\([^,]+,\s*"[^"]+",\s*(TASK_STACK_[A-Z0-9_]+)'
)
Assert-True ($taskMatches.Count -gt 0) 'No se encontraron tareas del firmware'

$taskStackWords = 0
foreach ($taskMatch in $taskMatches) {
    $taskStackWords += Get-IntegerMacro $firmwareSource $taskMatch.Groups[1].Value
}

$minimumStackWords = Get-IntegerMacro $freeRtosConfig 'configMINIMAL_STACK_SIZE'
$configuredHeapBytes = Get-IntegerMacro $freeRtosConfig 'configTOTAL_HEAP_SIZE'

# RP2350 usa StackType_t de 32 bits. Se reservan, ademas, las pilas de
# IDLE y TIMER y 8 KiB para TCB, mutex, cola de timers y alineamiento.
$requiredHeapBytes = ($taskStackWords + 2 * $minimumStackWords) * 4 + 8192
Assert-True ($configuredHeapBytes -ge $requiredHeapBytes) `
    "Heap insuficiente: $configuredHeapBytes < $requiredHeapBytes bytes"

Assert-True `
    ([regex]::IsMatch($freeRtosConfig, '(?m)^#define\s+configUSE_MALLOC_FAILED_HOOK\s+1\s*$')) `
    'configUSE_MALLOC_FAILED_HOOK no esta habilitado'
Assert-True `
    ([regex]::IsMatch($freeRtosConfig, '(?m)^#define\s+configCHECK_FOR_STACK_OVERFLOW\s+2\s*$')) `
    'configCHECK_FOR_STACK_OVERFLOW no esta configurado en nivel 2'

$uncheckedTaskCreations = [regex]::Matches(
    $firmwareSource,
    '(?m)^\s*xTaskCreate\([^;]+;\s*$'
)
Assert-True ($uncheckedTaskCreations.Count -eq 0) 'Hay llamadas xTaskCreate sin comprobar'

Assert-True ($firmwareSource.Contains('state_pos = "NA"')) `
    'La salida positiva puede mostrarse ON sin realimentacion valida'
Assert-True ($firmwareSource.Contains('state_neg = "NA"')) `
    'La salida negativa puede mostrarse ON sin realimentacion valida'
Assert-True ($firmwareSource.Contains('"ERR SENSOR\r\n"')) `
    'UART no rechaza el encendido cuando falta la realimentacion'
Assert-True ($firmwareSource.Contains('VLP=%.2f VLN=%.2f')) `
    'GET no publica los limites de sobretension requeridos por la web'
Assert-True ($firmwareSource.Contains('strcmp(setting, "ILP")')) `
    'UART perdio la edicion del limite de corriente positivo'
Assert-True ($firmwareSource.Contains('strcmp(setting, "ILN")')) `
    'UART perdio la edicion del limite de corriente negativo'
Assert-True ($firmwareSource.Contains('strcmp(setting, "VLP")')) `
    'UART perdio la edicion del limite de sobretension positivo'
Assert-True ($firmwareSource.Contains('strcmp(setting, "VLN")')) `
    'UART perdio la edicion del limite de sobretension negativo'
Assert-True ($firmwareSource.Contains('sscanf(command, "RESET %c"')) `
    'UART perdio el restablecimiento independiente de fallas'
Assert-True ($firmwareSource.Contains('"ERR LINE\r\n"')) `
    'UART no rechaza lineas demasiado largas'

Assert-True (-not $firmwareSource.Contains('adc_fifo_channel')) `
    'El ADC todavia etiqueta muestras con un contador de canal desincronizable'
Assert-True ($firmwareSource.Contains('adc_read_channel_raw(VIN_ADC_CHANNEL)')) `
    'Vin no se lee seleccionando explicitamente ADC0'
Assert-True ($firmwareSource.Contains('adc_read_channel_raw(IOUT2_ADC_CHANNEL)')) `
    'I- no se lee seleccionando explicitamente ADC1'
Assert-True ($firmwareSource.Contains('adc_read_channel_raw(IOUT1_ADC_CHANNEL)')) `
    'I+ no se lee seleccionando explicitamente ADC2'
Assert-True ($firmwareSource.Contains('(void)adc_read();')) `
    'No se descarta la primera conversion despues de cambiar el multiplexor ADC'
Assert-True (-not $firmwareSource.Contains('adc_set_round_robin(ADC_ROUND_ROBIN_MASK)')) `
    'El firmware todavia usa el round-robin que cruza los canales al arrancar'
Assert-True ($firmwareSource.Contains('#define CURRENT_ZERO_CALIBRATION_SAMPLES 32U')) `
    'No se promedian suficientes muestras con las salidas apagadas'
Assert-True ($firmwareSource.Contains('current_zero_calibrated = true')) `
    'La corriente no tiene calibracion automatica de cero al arrancar'
Assert-True ($firmwareSource.Contains('current_from_adc_raw(')) `
    'La corriente no descuenta el cero medido al arrancar'
Assert-True (-not $firmwareSource.Contains('CURRENT_POS_ZERO_VOLTS')) `
    'La corriente positiva todavia usa un offset fijo'
Assert-True (-not $firmwareSource.Contains('CURRENT_NEG_ZERO_VOLTS')) `
    'La corriente negativa todavia usa un offset fijo'
Assert-True ($firmwareSource.Contains('#define VOLTAGE_SETPOINT_INITIAL 5.0f')) `
    'La referencia inicial de salida no es 5 V'
Assert-True ($firmwareSource.Contains('"Vi:%5.2f -%s +%s')) `
    'La pantalla no muestra la salida negativa antes que la positiva'
Assert-True ($firmwareSource.Contains('"V-:%s V+:%s')) `
    'Las columnas de tension siguen en el orden positivo/negativo'
Assert-True ($firmwareSource.Contains('"I-:%s I+:%s')) `
    'Las columnas de corriente siguen en el orden positivo/negativo'
Assert-True `
    ([regex]::IsMatch($settingsSource, '(?m)^#define\s+SETTINGS_VERSION\s+2U\s*$')) `
    'La configuracion guardada de 10 V no se invalida al instalar esta version'

Assert-True `
    ([regex]::IsMatch($firmwareCMake, '(?m)^set\(FREERTOS_SUPPORT_DYNAMIC_ALLOCATION ON')) `
    'CMake no habilita la implementacion de heap dinamico de FreeRTOS'

Write-Output "OK: heap=$configuredHeapBytes bytes, minimo=$requiredHeapBytes bytes, tareas=$($taskMatches.Count)"
