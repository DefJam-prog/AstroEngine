param([string]$Value)
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
[Console]::Write($Value)
