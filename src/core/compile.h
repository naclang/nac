#ifndef NAC_COMPILE_H
#define NAC_COMPILE_H

/* Implements `nac build <script.nac> [-o <output>]`: bundles the given
 * script's source together with the interpreter runtime into a single,
 * standalone native executable (no separate .nac file or `nac` install
 * needed to run it afterwards).
 *
 * This is NOT a from-scratch compiler -- NaC has no bytecode/codegen
 * target. It works by embedding the script's source as a C string
 * literal, generating a tiny stub main() that runs it directly, and
 * compiling that alongside the same interpreter sources build.sh/
 * build.bat already compile (minus the original main.c). This requires
 * a C compiler (gcc) on the machine running `nac build`, and requires
 * nac's own src/ directory to be present next to the nac executable
 * (the same layout left behind by build.sh/build.bat) -- see compile.c
 * for details and the exact error messages shown when that's missing.
 *
 * Returns a process exit code (0 on success).
 */
int cmd_build(int argc, char **argv);

#endif
