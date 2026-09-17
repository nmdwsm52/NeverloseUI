<#
.SYNOPSIS
    把本目录的项目上传到你的 GitHub 账号（自动创建仓库 + 首次推送 + 结果校验）。

.DESCRIPTION
    完全走 GitHub REST API + git，不需要安装 gh CLI。
    令牌优先级：-Token 参数 > $env:GITHUB_TOKEN > Git Credential Manager 里已存的凭据
    > -Login 交互登录（设备码 / 浏览器）。
    令牌只用于本次进程内的 URL 重写（git -c url.*.insteadOf），不会写进 .git/config。

.PARAMETER Repo
    GitHub 仓库名，默认 NeverloseUI。

.PARAMETER Owner
    目标账号 / 组织。默认取令牌对应用户。

.PARAMETER Token
    Personal Access Token（classic 需要 repo 权限；fine-grained 需要 Administration: Read and write
    才能建仓库 —— 若只给了 Contents 权限，请先在网页端手动建好空仓库再运行本脚本）。

.PARAMETER Private
    建成私有仓库；默认公开。

.PARAMETER Login
    先用 Git Credential Manager 做一次登录（设备码/浏览器），再继续上传。

.PARAMETER NoPush
    只干本地活（建分支 / 补提交），不联网。

.EXAMPLE
    pwsh -File .\push_to_github.ps1 -Token ghp_xxxxxxxxxxxxxxxx

.EXAMPLE
    $env:GITHUB_TOKEN = 'github_pat_xxx'; pwsh -File .\push_to_github.ps1 -Repo NeverloseUI

.EXAMPLE
    pwsh -File .\push_to_github.ps1 -Login
#>
[CmdletBinding()]
param(
    [string]$Repo        = 'NeverloseUI',
    [string]$Owner       = '',
    [string]$Token       = $env:GITHUB_TOKEN,
    [string]$Description = 'Neverlose 风格交互界面：Dear ImGui(docking) + DirectX 11 + Win32，外置覆盖层 / 注入版 DLL',
    [string]$Branch      = 'main',
    [switch]$Private,
    [switch]$Login,
    [switch]$NoPush
)

$ErrorActionPreference = 'Stop'
$env:GIT_TERMINAL_PROMPT = '0'
$env:GCM_INTERACTIVE     = 'never'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location -LiteralPath $Root

function Say([string]$m, [string]$c = 'Gray') { Write-Host $m -ForegroundColor $c }

function Get-GcmToken {
    try {
        $out = ("protocol=https`nhost=github.com`n`n" | git credential fill 2>$null)
        foreach ($line in $out) {
            if ($line -match '^password=(.+)$') { return $Matches[1] }
        }
    } catch { }
    return $null
}

function Api([string]$Method, [string]$Path, $Body) {
    $h = @{
        Authorization          = "Bearer $script:Token"
        Accept                 = 'application/vnd.github+json'
        'User-Agent'           = 'neverlose-ui-push'
        'X-GitHub-Api-Version' = '2022-11-28'
    }
    $uri = if ($Path -like 'http*') { $Path } else { "https://api.github.com$Path" }
    if ($Body) {
        return Invoke-RestMethod -Method $Method -Uri $uri -Headers $h -Body ($Body | ConvertTo-Json -Depth 5) -ContentType 'application/json'
    }
    Invoke-RestMethod -Method $Method -Uri $uri -Headers $h
}

# ---------------------------------------------------------------- 0. 前置检查
Say "== 1/6 检查本地仓库 ==" 'Cyan'
if (-not (Test-Path (Join-Path $Root '.git'))) { throw "当前目录还不是 git 仓库：$Root（先 git init）" }
$count = (git rev-list --count HEAD 2>$null)
if (-not $count -or [int]$count -lt 1) { throw '还没有任何提交，先 git add -A && git commit' }
Say "  仓库根目录 : $Root"
Say "  当前分支   : $(git branch --show-current)"
Say "  已有提交   : $count"
Say "  受控文件   : $((git ls-files | Measure-Object).Count) 个"

if ($NoPush) { Say "`n-NoPush：仅本地处理，结束。" 'Yellow'; return }

# ---------------------------------------------------------------- 1. 拿令牌
Say "`n== 2/6 准备 GitHub 凭据 ==" 'Cyan'
if ($Login) {
    Say '  启动 Git Credential Manager 登录（按提示在浏览器完成）...' 'Yellow'
    git credential-manager github login --device
    $Token = Get-GcmToken
}
if (-not $Token) { $Token = Get-GcmToken }
if (-not $Token) {
    throw '拿不到 GitHub 令牌。请用 -Token <PAT>，或先设 $env:GITHUB_TOKEN，或加 -Login。'
}
$script:Token = $Token

# ---------------------------------------------------------------- 2. 校验身份
$me = Api 'GET' '/user'
$Login2 = $me.login
if (-not $Owner) { $Owner = $Login2 }
Say "  已登录账号 : $Login2"
Say "  目标仓库   : $Owner/$Repo"
if ($Owner -ne $Login2 -and -not (Test-Path variable:me.organizations)) {
    Say '  注意：目标是别的账号/组织，令牌需要有该组织的建仓权限。' 'Yellow'
}

# ---------------------------------------------------------------- 3. 身份 & 提交兜底
Say "`n== 3/6 补齐提交身份与本地提交 ==" 'Cyan'
if (-not (git config user.name))  { git config user.name  $Login2; Say "  已设置 user.name  = $Login2" }
if (-not (git config user.email)) { git config user.email "$Login2@users.noreply.github.com"; Say "  已设置 user.email = $Login2@users.noreply.github.com" }
$dirty = git status --porcelain
if ($dirty) {
    Say '  检测到未提交改动，先补一次提交 ...' 'Yellow'
    git add -A
    git -c core.safecrlf=false commit -q -m "chore: 上传前同步($(Get-Date -Format 'yyyy-MM-dd HH:mm'))"
}
Say "  HEAD = $(git rev-parse --short HEAD)  $(git log -1 --pretty=%s)"

# ---------------------------------------------------------------- 4. 建仓库
Say "`n== 4/6 在 GitHub 上创建仓库 ==" 'Cyan'
$created = $false
try {
    $body = @{
        name        = $Repo
        description = $Description
        private     = [bool]$Private
        has_issues  = $true
        has_wiki    = $false
        auto_init   = $false
    }
    $apiPath = if ($Owner -eq $Login2) { '/user/repos' } else { "/orgs/$Owner/repos" }
    $info = Api 'POST' $apiPath $body
    $created = $true
    Say "  已创建：$($info.full_name)  ($(if ($Private) {'private'} else {'public'}))" 'Green'
} catch {
    $code = $_.Exception.Response.StatusCode.value__
    if ($code -eq 422) {
        Say '  仓库已存在，直接复用。' 'Yellow'
        $info = Api 'GET' "/repos/$Owner/$Repo"
    } elseif ($code -eq 404 -and $Owner -ne $Login2) {
        throw "组织 $Owner 不存在或令牌无权访问（404）。"
    } else {
        throw
    }
}

# ---------------------------------------------------------------- 5. 推送
Say "`n== 5/6 推送 $Branch ==" 'Cyan'
$cleanUrl = "https://github.com/$Owner/$Repo.git"
$authedUrl = "https://x-access-token:$Token@github.com/$Owner/$Repo.git"

git remote remove origin 2>$null | Out-Null
git remote add origin $cleanUrl

$headBranch = git branch --show-current
if (-not $headBranch) { $headBranch = $Branch }
if ($headBranch -ne $Branch) { git branch -M $headBranch $Branch; $headBranch = $Branch }

# 用 -c url.<token>@.insteadOf 临时重写，令牌不落盘
git -c "url.$authedUrl.insteadOf=https://github.com/" push -u origin $Branch 2>&1 | ForEach-Object { Say "  $_" }
if ($LASTEXITCODE -ne 0) { throw "git push 失败（exit $LASTEXITCODE）" }

# ---------------------------------------------------------------- 6. 校验
Say "`n== 6/6 校验远端 ==" 'Cyan'
$info = Api 'GET' "/repos/$Owner/$Repo"
$remoteHead = (git ls-remote origin "refs/heads/$Branch").Split("`t")[0]
$localHead  = git rev-parse HEAD
Say "  远端地址   : $($info.html_url)"
Say "  默认分支   : $($info.default_branch)"
Say "  远端 HEAD  : $remoteHead"
Say "  本地 HEAD  : $localHead"
Say "  提交数     : $(git rev-list --count HEAD)"
if ($remoteHead -eq $localHead) {
    Say "`n完成：代码已经在你自己的 GitHub 上了 -> $($info.html_url)" 'Green'
} else {
    Say "`n远端 HEAD 与本地不一致，请检查。" 'Red'
}
