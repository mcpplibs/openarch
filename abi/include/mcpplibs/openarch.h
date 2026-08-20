/* mcpplibs/openarch.h — openarch's C face, entire.
 *
 * A consumer writes one dependency line
 *
 *     [dependencies]
 *     openarch = "0.4.0"
 *
 * and then either
 *
 *     #include <mcpplibs/openarch.h>          this file, for C and for C++
 *     import mcpplibs.openarch;               the module, for C++
 *
 * The two are the same library seen from two sides, not two libraries: the
 * module's enumerations are defined from the enumerators below, its types are
 * `using` declarations of the types below, and its functions are inline calls
 * to the functions below. `tests/faces.cpp` states that as `static_assert`.
 *
 * ⭐ WHY THE C FACE IS A FIRST-CLASS FACE AND NOT A LEFTOVER.
 *
 * openarch is the layer a kernel's earliest code uses, and that code is
 * frequently not C++: a boot stub written in C, a vendor's board file, a
 * language runtime that speaks the C ABI and nothing else. A machine layer that
 * could only be reached from a C++ module would exclude its own earliest
 * callers, and would also exclude the second kind of backend the split was made
 * to allow — an implementation that is not a C++ module.
 *
 * ⚠️ THIS HEADER ADDS NOTHING. It exists so that the name a consumer writes
 * matches the name of the package and of the module, and so that the contract
 * header — which a backend author reads and a consumer does not — keeps its own
 * name. If a declaration ever appears here that is not in `openarch/abi.h`, the
 * C face and the ABI have started to differ, and a backend built against one
 * will link against the other.
 */
#ifndef MCPPLIBS_OPENARCH_H
#define MCPPLIBS_OPENARCH_H

#include <openarch/abi.h>   /* which includes <openarch/types.h> */

#endif /* MCPPLIBS_OPENARCH_H */
