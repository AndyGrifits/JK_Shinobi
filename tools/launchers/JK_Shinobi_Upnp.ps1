# JK_Shinobi: asks the home router (UPnP) to open the game's port to this PC, and says whether
# this connection can host over the internet at all. Used by JK_Shinobi_Host.bat:
#   powershell -ExecutionPolicy Bypass -File JK_Shinobi_Upnp.ps1          (open, report)
#   powershell -ExecutionPolicy Bypass -File JK_Shinobi_Upnp.ps1 -Remove  (close again)
# No extra software: plain SSDP discovery and SOAP calls to the router.
param([switch]$Remove, [int]$Port = 29070)
$ErrorActionPreference = 'Stop'

function Find-Routers {
    $udp = New-Object System.Net.Sockets.UdpClient
    $udp.Client.ReceiveTimeout = 700
    $ep = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Parse('239.255.255.250'), 1900)
    foreach ($st in 'urn:schemas-upnp-org:device:InternetGatewayDevice:1', 'urn:schemas-upnp-org:device:InternetGatewayDevice:2',
                    'urn:schemas-upnp-org:service:WANIPConnection:1', 'urn:schemas-upnp-org:service:WANIPConnection:2') {
        $msg = "M-SEARCH * HTTP/1.1`r`nHOST: 239.255.255.250:1900`r`nMAN: `"ssdp:discover`"`r`nMX: 1`r`nST: $st`r`n`r`n"
        $b = [System.Text.Encoding]::ASCII.GetBytes($msg)
        [void]$udp.Send($b, $b.Length, $ep)
    }
    $locs = @()
    $deadline = (Get-Date).AddSeconds(3)
    while ((Get-Date) -lt $deadline) {
        try {
            $from = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
            $text = [System.Text.Encoding]::ASCII.GetString($udp.Receive([ref]$from))
            if ($text -match '(?im)^location:\s*(\S+)') { $locs += $matches[1] }
        } catch { }
    }
    $udp.Close()
    return $locs | Select-Object -Unique
}

function Find-Service($location) {
    $xml = [xml](Invoke-WebRequest -UseBasicParsing -TimeoutSec 4 -Uri $location).Content
    $uri = [Uri]$location
    $base = "$($uri.Scheme)://$($uri.Host):$($uri.Port)"
    foreach ($svc in $xml.GetElementsByTagName('service')) {
        $type = $svc.serviceType
        if ($type -match 'WANIPConnection|WANPPPConnection') {
            $ctl = $svc.controlURL
            if ($ctl -notmatch '^http') { if (-not $ctl.StartsWith('/')) { $ctl = '/' + $ctl }; $ctl = $base + $ctl }
            return @{ Type = $type; Url = $ctl; Host = $uri.Host; Port = $uri.Port }
        }
    }
    return $null
}

function Soap($svc, $action, $inner) {
    $body = "<?xml version=`"1.0`"?><s:Envelope xmlns:s=`"http://schemas.xmlsoap.org/soap/envelope/`" " +
            "s:encodingStyle=`"http://schemas.xmlsoap.org/soap/encoding/`"><s:Body><u:$action xmlns:u=`"$($svc.Type)`">" +
            "$inner</u:$action></s:Body></s:Envelope>"
    $r = Invoke-WebRequest -UseBasicParsing -TimeoutSec 4 -Uri $svc.Url -Method Post -Body $body `
         -ContentType 'text/xml; charset="utf-8"' -Headers @{ SOAPAction = "`"$($svc.Type)#$action`"" }
    return [xml]$r.Content
}

function Is-Private($ip) {
    if (-not $ip -or $ip -eq '0.0.0.0') { return $true }
    $o = $ip.Split('.') | ForEach-Object { [int]$_ }
    return ($o[0] -eq 10) -or ($o[0] -eq 172 -and $o[1] -ge 16 -and $o[1] -le 31) -or ($o[0] -eq 192 -and $o[1] -eq 168) -or
           ($o[0] -eq 100 -and $o[1] -ge 64 -and $o[1] -le 127)
}

try {
    $svc = $null
    foreach ($loc in Find-Routers) { try { $svc = Find-Service $loc } catch { }; if ($svc) { break } }
    if (-not $svc) {
        Write-Host '  Router: no UPnP answer (UPnP off on the router?) - internet players can only join if UDP' $Port 'is forwarded by hand.'
        exit 1
    }
    if ($Remove) {
        try { [void](Soap $svc 'DeletePortMapping' "<NewRemoteHost></NewRemoteHost><NewExternalPort>$Port</NewExternalPort><NewProtocol>UDP</NewProtocol>") } catch { }
        Write-Host "  Router: port $Port closed again."
        exit 0
    }
    $ext = ''
    try { $ext = (Soap $svc 'GetExternalIPAddress' '').GetElementsByTagName('NewExternalIPAddress')[0].InnerText } catch { }
    if (Is-Private $ext) {
        Write-Host '  ------------------------------------------------------------------------------------------'
        Write-Host '  This internet connection has no public IPv4 address of its own (DS-Lite / carrier NAT):'
        Write-Host '  nobody can join a game hosted here over the internet. Let a friend with a normal'
        Write-Host '  connection host (he starts JK_Shinobi_Host.bat, you join him) - players on this network can still join.'
        Write-Host '  ------------------------------------------------------------------------------------------'
        exit 2
    }
    $probe = New-Object System.Net.Sockets.UdpClient
    $probe.Connect($svc.Host, 1900)
    $me = $probe.Client.LocalEndPoint.Address.ToString()
    $probe.Close()
    $map = "<NewRemoteHost></NewRemoteHost><NewExternalPort>$Port</NewExternalPort><NewProtocol>UDP</NewProtocol>" +
            "<NewInternalPort>$Port</NewInternalPort><NewInternalClient>$me</NewInternalClient><NewEnabled>1</NewEnabled>" +
            "<NewPortMappingDescription>JK_Shinobi</NewPortMappingDescription><NewLeaseDuration>0</NewLeaseDuration>"
    try {
        [void](Soap $svc 'AddPortMapping' $map)
    } catch {
        Write-Host '  Router: it refused to open the port. On a FRITZ!Box: Internet > Permit Access > Port Sharing >'
        Write-Host '  this PC > "Permit independent port sharing for this device" - or forward UDP' $Port 'to' $me 'by hand.'
        exit 3
    }
    Write-Host '  =========================================================================================='
    Write-Host "   Internet hosting is ON. Send your friend this address:   $ext"
    Write-Host "   (he types it into JK_Shinobi_Join.bat; the port $Port is open to this PC - $me - until the game ends)"
    Write-Host '  =========================================================================================='
    exit 0
} catch {
    Write-Host '  UPnP check failed:' $_.Exception.Message
    exit 4
}
