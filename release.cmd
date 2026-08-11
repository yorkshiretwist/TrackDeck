@echo off
setlocal enabledelayedexpansion

rem =============================================================
rem  release.cmd
rem  Commits current changes, tags the commit, and pushes both
rem  to trigger the .github\workflows\cmake.yml build + release.
rem
rem  Run this from the repo root (or anywhere inside the repo).
rem =============================================================

rem --- Make sure we're actually inside a git repo -----------------
git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
    echo ERROR: This doesn't look like a git repository.
    pause
    exit /b 1
)

rem --- Prompt for commit message -----------------------------------
set "COMMIT_MSG="
set /p COMMIT_MSG="Commit message: "
if "%COMMIT_MSG%"=="" (
    echo ERROR: Commit message cannot be empty.
    pause
    exit /b 1
)

rem --- Prompt for version number ------------------------------------
set "VERSION="
set /p VERSION="Version number (e.g. 0.1.0 or v0.1.0): "
if "%VERSION%"=="" (
    echo ERROR: Version number cannot be empty.
    pause
    exit /b 1
)

rem --- Normalize to a "vX.Y.Z" tag (add the "v" if the user left it off) ---
set "TAG=%VERSION%"
if /i not "%TAG:~0,1%"=="v" set "TAG=v%TAG%"

rem --- Figure out the current branch so we push the right thing -----
for /f "delims=" %%b in ('git rev-parse --abbrev-ref HEAD') do set "BRANCH=%%b"

rem --- Bail out early if this tag already exists ---------------------
git rev-parse "%TAG%" >nul 2>&1
if not errorlevel 1 (
    echo ERROR: Tag %TAG% already exists. Choose a different version.
    pause
    exit /b 1
)

echo.
echo ------------------------------------------------------------
echo  Branch:  %BRANCH%
echo  Commit:  %COMMIT_MSG%
echo  Tag:     %TAG%
echo ------------------------------------------------------------
set /p CONFIRM="Proceed? (y/n): "
if /i not "%CONFIRM%"=="y" (
    echo Cancelled.
    pause
    exit /b 0
)

rem --- Stage everything and commit ------------------------------------
git add -A

git diff --cached --quiet
if errorlevel 1 (
    git commit -m "%COMMIT_MSG%"
    if errorlevel 1 (
        echo ERROR: git commit failed.
        pause
        exit /b 1
    )
) else (
    echo No staged changes to commit — tagging the current HEAD as-is.
)

rem --- Tag it -----------------------------------------------------------
git tag -a "%TAG%" -m "%COMMIT_MSG%"
if errorlevel 1 (
    echo ERROR: git tag failed.
    pause
    exit /b 1
)

rem --- Push the branch, then the tag (tag push is what fires the release job) ---
echo.
echo Pushing branch %BRANCH%...
git push origin "%BRANCH%"
if errorlevel 1 (
    echo ERROR: git push of branch failed.
    pause
    exit /b 1
)

echo Pushing tag %TAG%...
git push origin "%TAG%"
if errorlevel 1 (
    echo ERROR: git push of tag failed.
    pause
    exit /b 1
)

echo.
echo Done. Pushing tag %TAG% should trigger the build + release workflow.
echo Check the Actions tab on GitHub to watch it run.
pause