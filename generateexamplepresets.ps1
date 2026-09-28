# Define the list of preset and switch names
$ampManufacturers = @("Fender", "Marshall", "Vox", "Peavey", "Orange", "Mesa Boogie", "Roland", "Line 6", "Ampeg")
$switchNames = @("DIST", "PITCH", "WAH", "DLAY", "REV", "FUZZ")
$rng = [System.Random]::new()

# Create a directory to save the JSON files if it doesn't exist
$directory = "JSON_Variations"
if (-not (Test-Path -Path $directory -PathType Container)) {
    New-Item -Path $directory -ItemType Directory | Out-Null
}

# Generate and save 100 JSON variations
for ($i = 1; $i -le 100; $i++) {
    # Randomly select preset and switch names using one RNG instance.
    $presetName = $ampManufacturers[$rng.Next($ampManufacturers.Count)]
    $switch1Name = $switchNames[$rng.Next($switchNames.Count)]
    $switch2Name = $switchNames[$rng.Next($switchNames.Count)]
    $switch3Name = $switchNames[$rng.Next($switchNames.Count)]

    # Create an object representing the JSON structure
    $jsonStructure = @{
        "Name" = $presetName
        "OnLoad" = @{
            "CC" = @(
                @{
                    "CC" = 1
                    "Value" = 127
                    "Channel" = 11
                }
            )
            "PC" = @(
                @{
                    "PC" = 2
                    "Channel" = 11
                }
            )
        }
        "Switch1" = @{
            "Name" = $switch1Name
            "CC" = @(
                @{
                    "CC" = 1
                    "Value" = 127
                    "Channel" = 11
                },
                @{
                    "CC" = 4
                    "Value" = 127
                    "Channel" = 11
                }
            )
            "PC" = $null
        }
        "Switch2" = @{
            "Name" = $switch2Name
            "CC" = @(
                @{
                    "CC" = 3
                    "Value" = 127
                    "Channel" = 11
                },
                @{
                    "CC" = 5
                    "Value" = 0
                    "Channel" = 11
                }
            )
            "PC" = $null
        }
        "Switch3" = @{
            "Name" = $switch3Name
            "CC" = $null
            "PC" = @(
                @{
                    "PC" = 2
                    "Channel" = 11
                }
            )
        }
    }

    # Convert the object to JSON and save it to a file
    $jsonText = $jsonStructure | ConvertTo-Json -Depth 6
    Set-Content -Path (Join-Path -Path $directory -ChildPath "$i.json") -Value $jsonText -Encoding utf8
}

Write-Host "Generated 100 JSON variations."