param(
  [string] $output_path = (
    Join-Path $PSScriptRoot "..\package\BackPocket\Interface\BackPocket\category_icon.swf"
  )
)

$ErrorActionPreference = "Stop"

function add_u16 {
  param(
    [System.Collections.Generic.List[byte]] $bytes,
    [uint16] $value
  )

  $bytes.Add([byte] ($value -band 0xff))
  $bytes.Add([byte] (($value -shr 8) -band 0xff))
}

function add_u32 {
  param(
    [System.Collections.Generic.List[byte]] $bytes,
    [uint32] $value
  )

  for ($shift = 0; $shift -lt 32; $shift += 8) {
    $bytes.Add([byte] (($value -shr $shift) -band 0xff))
  }
}

function add_bits {
  param(
    [System.Collections.Generic.List[int]] $bits,
    [long] $value,
    [int] $count
  )

  for ($shift = $count - 1; $shift -ge 0; --$shift) {
    $bits.Add([int] (($value -shr $shift) -band 1))
  }
}

function add_signed_bits {
  param(
    [System.Collections.Generic.List[int]] $bits,
    [long] $value,
    [int] $count
  )

  $mask = (1L -shl $count) - 1
  add_bits $bits ($value -band $mask) $count
}

function signed_bit_count {
  param([long[]] $values)

  for ($count = 2; $count -le 17; ++$count) {
    $minimum = -(1L -shl ($count - 1))
    $maximum = (1L -shl ($count - 1)) - 1
    if (($values | Where-Object { $_ -lt $minimum -or $_ -gt $maximum }).Count -eq 0) {
      return $count
    }
  }
  throw "Shape coordinate exceeds the SWF straight-edge range."
}

function pack_bits {
  param([System.Collections.Generic.List[int]] $bits)

  while (($bits.Count % 8) -ne 0) {
    $bits.Add(0)
  }

  $bytes = [System.Collections.Generic.List[byte]]::new()
  for ($offset = 0; $offset -lt $bits.Count; $offset += 8) {
    $value = 0
    for ($index = 0; $index -lt 8; ++$index) {
      $value = ($value -shl 1) -bor $bits[$offset + $index]
    }
    $bytes.Add([byte] $value)
  }
  return ,$bytes
}

function add_rect {
  param(
    [System.Collections.Generic.List[byte]] $bytes,
    [int] $minimum_x,
    [int] $maximum_x,
    [int] $minimum_y,
    [int] $maximum_y
  )

  $bits = [System.Collections.Generic.List[int]]::new()
  add_bits $bits 10 5
  add_signed_bits $bits $minimum_x 10
  add_signed_bits $bits $maximum_x 10
  add_signed_bits $bits $minimum_y 10
  add_signed_bits $bits $maximum_y 10
  $bytes.AddRange((pack_bits $bits))
}

function add_tag {
  param(
    [System.Collections.Generic.List[byte]] $bytes,
    [int] $code,
    [System.Collections.Generic.List[byte]] $body
  )

  if ($body.Count -lt 63) {
    add_u16 $bytes ([uint16] (($code -shl 6) -bor $body.Count))
  } else {
    add_u16 $bytes ([uint16] (($code -shl 6) -bor 63))
    add_u32 $bytes ([uint32] $body.Count)
  }
  $bytes.AddRange($body)
}

function add_style_change {
  param(
    [System.Collections.Generic.List[int]] $bits,
    [int] $x,
    [int] $y,
    [bool] $set_fill
  )

  add_bits $bits 0 1
  add_bits $bits (1 + $(if ($set_fill) { 4 } else { 0 })) 5
  $count = signed_bit_count @($x, $y)
  add_bits $bits $count 5
  add_signed_bits $bits $x $count
  add_signed_bits $bits $y $count
  if ($set_fill) {
    add_bits $bits 1 1
  }
}

function add_straight_edge {
  param(
    [System.Collections.Generic.List[int]] $bits,
    [int] $delta_x,
    [int] $delta_y
  )

  $count = signed_bit_count @($delta_x, $delta_y)
  add_bits $bits 1 1
  add_bits $bits 1 1
  add_bits $bits ($count - 2) 4
  add_bits $bits 1 1
  add_signed_bits $bits $delta_x $count
  add_signed_bits $bits $delta_y $count
}

function make_shape {
  param(
    [uint16] $character_id,
    [byte] $red,
    [byte] $green,
    [byte] $blue,
    [hashtable[]] $polygons
  )

  $body = [System.Collections.Generic.List[byte]]::new()
  add_u16 $body $character_id
  add_rect $body 0 240 0 240
  $body.Add(1)
  $body.Add(0)
  $body.Add($red)
  $body.Add($green)
  $body.Add($blue)
  $body.Add(255)
  $body.Add(0)
  $body.Add(0x10)

  $records = [System.Collections.Generic.List[int]]::new()
  $first_polygon = $true
  foreach ($polygon in $polygons) {
    $points = $polygon.points
    $first = $points[0]
    add_style_change $records $first.x $first.y $first_polygon
    $first_polygon = $false
    $previous = $first
    for ($index = 1; $index -lt $points.Count; ++$index) {
      $point = $points[$index]
      add_straight_edge $records ($point.x - $previous.x) ($point.y - $previous.y)
      $previous = $point
    }
    add_straight_edge $records ($first.x - $previous.x) ($first.y - $previous.y)
  }
  add_bits $records 0 6
  $body.AddRange((pack_bits $records))
  return ,$body
}

function point {
  param([int] $x, [int] $y)
  return [pscustomobject]@{ x = $x; y = $y }
}

function place_shape {
  param(
    [System.Collections.Generic.List[byte]] $content,
    [uint16] $depth,
    [uint16] $character_id
  )

  $place = [System.Collections.Generic.List[byte]]::new()
  $place.Add(0x06)
  add_u16 $place $depth
  add_u16 $place $character_id
  $place.Add(0)
  add_tag $content 26 $place
}

$satchel = @(
  @{
    points = @(
      (point 78 54), (point 87 24), (point 153 24), (point 166 54),
      (point 146 54), (point 139 40), (point 101 40), (point 96 54)
    )
  },
  @{ points = @((point 40 59), (point 201 59), (point 188 105), (point 52 105)) },
  @{
    points = @(
      (point 47 112), (point 194 112), (point 206 192), (point 185 217),
      (point 55 217), (point 34 192)
    )
  },
  # Counter-wound paths become transparent cutouts in the satchel body.
  @{ points = @((point 108 112), (point 111 140), (point 129 140), (point 132 112)) },
  @{ points = @((point 120 156), (point 104 170), (point 120 186), (point 136 170)) }
)

$content = [System.Collections.Generic.List[byte]]::new()
foreach ($character in [System.Text.Encoding]::ASCII.GetBytes("FWS")) {
  $content.Add($character)
}
$content.Add(8)
add_u32 $content 0
add_rect $content 0 240 0 240
add_u16 $content 0x1800
add_u16 $content 1

add_tag $content 32 (make_shape 1 229 229 229 $satchel)
place_shape $content 1 1

$frame_label = [System.Collections.Generic.List[byte]]::new()
foreach ($character in [System.Text.Encoding]::ASCII.GetBytes("back_pocket")) {
  $frame_label.Add($character)
}
$frame_label.Add(0)
add_tag $content 43 $frame_label
add_tag $content 1 ([System.Collections.Generic.List[byte]]::new())
add_tag $content 0 ([System.Collections.Generic.List[byte]]::new())

$length = [uint32] $content.Count
for ($index = 0; $index -lt 4; ++$index) {
  $content[4 + $index] = [byte] (($length -shr ($index * 8)) -band 0xff)
}

$resolved_output = [IO.Path]::GetFullPath($output_path)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($resolved_output)) | Out-Null
[IO.File]::WriteAllBytes($resolved_output, $content.ToArray())
Write-Host "Generated $resolved_output ($($content.Count) bytes)"
