$ErrorActionPreference = 'Stop'

function Invoke-Utility {
<#
.SYNOPSIS
Invokes an external utility, ensuring successful execution.

.DESCRIPTION
Invokes an external utility (program) and, if the utility indicates failure by 
way of a nonzero exit code, throws a script-terminating error.

* Pass the command the way you would execute the command directly.
* Do NOT use & as the first argument if the executable name is not a literal.

.EXAMPLE
Invoke-Utility git push

Executes `git push` and throws a script-terminating error if the exit code
is nonzero.
#>
  $exe, $argsForExe = $Args
  $ErrorActionPreference = 'Continue' # to prevent 2> redirections from triggering a terminating error.
  try { & $exe $argsForExe } catch { Throw } # catch is triggered ONLY if $exe can't be found, never for errors reported by $exe itself
  if ($LASTEXITCODE) { Throw "$exe indicated failure (exit code $LASTEXITCODE; full command: $Args)." }
}

invoke-utility py -$Env:PYVERSION-$Env:CI_ARCH -m pip install cython setuptools wheel scikit-build
invoke-utility py -$Env:PYVERSION-$Env:CI_ARCH synthizer-c/vendor.py synthizer-vendored
invoke-utility py -$Env:PYVERSION-$Env:CI_ARCH setup.py bdist_wheel
