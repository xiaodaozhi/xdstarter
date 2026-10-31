function Test-RestoredAccess([string]$Original, [string]$Actual) {
    $before = [Security.AccessControl.RawSecurityDescriptor]::new($Original)
    $after = [Security.AccessControl.RawSecurityDescriptor]::new($Actual)
    # Windows can set this metadata flag while restoring inheritance. Preserve
    # every other control flag and compare the ordered DACL bytes exactly.
    $mask = -bnot [int][Security.AccessControl.ControlFlags]::DiscretionaryAclAutoInherited
    if (([int]$before.ControlFlags -band $mask) -ne ([int]$after.ControlFlags -band $mask)) {
        return $false
    }
    if ($null -eq $before.DiscretionaryAcl -or $null -eq $after.DiscretionaryAcl) {
        return $null -eq $before.DiscretionaryAcl -and $null -eq $after.DiscretionaryAcl
    }
    $beforeBytes = [byte[]]::new($before.DiscretionaryAcl.BinaryLength)
    $afterBytes = [byte[]]::new($after.DiscretionaryAcl.BinaryLength)
    $before.DiscretionaryAcl.GetBinaryForm($beforeBytes, 0)
    $after.DiscretionaryAcl.GetBinaryForm($afterBytes, 0)
    return [Convert]::ToHexString($beforeBytes) -ceq [Convert]::ToHexString($afterBytes)
}
