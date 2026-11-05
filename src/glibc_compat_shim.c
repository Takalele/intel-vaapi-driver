/*
 * The host build toolchain is glibc 2.43 (>= 2.38), whose <stdlib.h>
 * redirects strtol/strtoll to the newer __isoc23_* symbols (GLIBC_2.38).
 * The target (Brave snap / gnome-platform) ships glibc 2.35, which lacks
 * those symbols, so dlopen() of this driver would fail with
 * "version `GLIBC_2.38' not found".
 *
 * Provide the newer symbols, forwarding to the stable strtol/strtoll
 * (GLIBC_2.2.5), which every glibc >= 2.2.5 exports. The __asm__ label
 * binds to the stable symbol name, bypassing the header redirect.
 */
long int __isoc23_strtol(const char *__restrict nptr,
                         char **__restrict endptr, int base) {
    extern long int _stable_strtol(const char *, char **, int) __asm__("strtol");
    return _stable_strtol(nptr, endptr, base);
}

long long int __isoc23_strtoll(const char *__restrict nptr,
                               char **__restrict endptr, int base) {
    extern long long int _stable_strtoll(const char *, char **, int)
        __asm__("strtoll");
    return _stable_strtoll(nptr, endptr, base);
}
