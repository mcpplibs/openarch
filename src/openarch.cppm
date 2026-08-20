// mcpplibs.openarch — openarch's C++ face, entire.
//
// A consumer writes one dependency line and one import:
//
//     import mcpplibs.openarch;
//
// and has `arch::context`, `arch::pte`, `arch::trap_frame`, `arch::percpu` and
// the rest. The four modules below remain importable on their own for a
// consumer that wants only one of them — a boot path that needs `openarch.pte`
// and has no traps yet — but nothing requires knowing they exist.
//
// ⭐ WHY AN UMBRELLA AND NOT FOUR NAMES.
//
// The four are one layer. A kernel that switches contexts also takes traps, and
// a caller forced to enumerate the parts is being asked to know the layer's
// internal division — which is exactly the knowledge a machine-abstraction
// layer exists to remove. The division is real and is worth keeping in the
// source; it is not worth putting in every consumer's manifest.
//
// ⚠️ `export import` AND NOT `import`. A plain import would make the names
// visible while compiling this module and invisible to whoever imports it,
// which compiles cleanly here and fails at every call site.
export module mcpplibs.openarch;

export import mcpplibs.openarch.context;
export import mcpplibs.openarch.pte;
export import mcpplibs.openarch.trap;
export import mcpplibs.openarch.cpu;
