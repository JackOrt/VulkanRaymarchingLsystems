# Define shader stage mappings based on filename keywords
$shaderStages = @{
    "vert" = "vertex"
    "frag" = "fragment"
    "geom" = "geometry"
    "tesc" = "tesscontrol"
    "tese" = "tesseval"
    "comp" = "compute"
    "mesh" = "mesh"
    "task" = "task"
    "rgen" = "rgen"
    "rint" = "rint"
    "rahit" = "rahit"
    "rchit" = "rchit"
    "rmiss" = "rmiss"
    "rcall" = "rcall"
}

# Get all .glsl files in the current directory
$shaders = Get-ChildItem -Path . -Filter "*.glsl"

# Store job handles
$jobs = @()

# Get absolute path of the working directory
$workingDirectory = Get-Location

foreach ($shader in $shaders) {
    $jobs += Start-Job -ScriptBlock {
        param ($shaderPath, $shaderStages, $workingDirectory)

        # Change to the correct directory
        Set-Location -Path $workingDirectory

        $fileName = [System.IO.Path]::GetFileName($shaderPath)
        $baseName = [System.IO.Path]::GetFileNameWithoutExtension($shaderPath)
        $outputFile = "$baseName.spv"

        # Determine shader stage based on filename suffix
        $stage = $null
        foreach ($key in $shaderStages.Keys) {
            if ($fileName -match "_$key\.glsl$") {
                $stage = $shaderStages[$key]
                break
            }
        }

        if ($stage) {
            Write-Host "Compiling ${fileName} as ${stage} shader..."
            Start-Process -NoNewWindow -Wait -FilePath "glslc" -ArgumentList "-fshader-stage=$stage", "$fileName", "-o", "$outputFile"
        }
        else {
            Write-Host "Skipping ${fileName}: Unknown shader stage."
        }
    } -ArgumentList $shader.FullName, $shaderStages, $workingDirectory
}

# Wait for all jobs to complete
$jobs | ForEach-Object { Receive-Job -Job $_ -Wait; Remove-Job -Id $_.Id }
