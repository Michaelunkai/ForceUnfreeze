#pragma once

// Compatibility shim for incomplete Windows SDK installations where
// specstrings.h references driverspecs.h but the file is absent. ForceUnfreeze
// does not use kernel-driver SAL annotations, so empty definitions are enough
// for the Win32 user-mode headers used by this project.

#ifndef __drv_aliasesMem
#define __drv_aliasesMem
#endif

#ifndef __drv_freesMem
#define __drv_freesMem(kind)
#endif

#ifndef __drv_preferredFunction
#define __drv_preferredFunction(func, why)
#endif
